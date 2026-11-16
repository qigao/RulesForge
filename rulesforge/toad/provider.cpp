#include "script/script_service.h"
#include <cmeta/plugin_decl.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

struct rf_script_session {
  std::thread::id owner = std::this_thread::get_id();
  toad_script_ctx_t *context = nullptr;
  toad_script_result_t *result = nullptr;
  std::vector<toad_script_module_t *> modules;
  struct Export {
    size_t module;
    std::string name;
    size_t arity;
  };
  std::vector<Export> exports;
  rf_script_limits limits{};
  bool active = false;
  toad_script_instance_t *pending_instance = nullptr;
};

namespace {
int diagnostic(rf_script_diagnostic &d, int status, const char *message) {
  d = {};
  d.status = status;
  std::snprintf(d.message, sizeof(d.message), "%s", message);
  return status;
}

int error(rf_script_session &s, rf_script_diagnostic &d, int status) {
  diagnostic(d, status, "Toad operation failed");
  toad_script_error_info_t e{};
  e.struct_size = sizeof(e);
  if (s.result && toad_script_result_get_error(s.result, &e) == TOAD_SCRIPT_STATUS_OK) {
    d.phase = e.phase;
    d.line = e.line;
    d.column = e.column;
    std::snprintf(d.message, sizeof(d.message), "module=%.*s function=%.*s: %.*s",
                  static_cast<int>(std::min(e.module_name.size, size_t(128))),
                  e.module_name.data ? e.module_name.data : "",
                  static_cast<int>(std::min(e.function_name.size, size_t(128))),
                  e.function_name.data ? e.function_name.data : "",
                  static_cast<int>(std::min(e.message.size, size_t(700))),
                  e.message.data ? e.message.data : "");
  }
  return status;
}

bool deny_plugin(const char *, void *) { return false; }

int cleanup(rf_script_session &s, rf_script_diagnostic &d) {
  if (s.pending_instance) {
    int status = toad_script_instance_destroy(s.pending_instance, s.result);
    if (status)
      return error(s, d, status);
    s.pending_instance = nullptr;
  }
  for (auto it = s.modules.rbegin(); it != s.modules.rend(); ++it) {
    if (!*it)
      continue;
    int status = toad_script_module_destroy(*it, s.result);
    if (status)
      return error(s, d, status);
    *it = nullptr;
  }
  if (s.result)
    toad_script_result_destroy(s.result);
  s.result = nullptr;
  if (s.context)
    toad_script_free(s.context);
  s.context = nullptr;
  return 0;
}

int create_instance(rf_script_session &s, size_t binding, toad_script_instance_t **instance,
                    toad_script_export_handle_t *handle, rf_script_diagnostic &d) {
  auto &e = s.exports.at(binding);
  toad_script_instance_options_t options;
  toad_script_instance_options_init(&options);
  options.mode = s.limits.mode;
  options.max_retained_bytes = s.limits.retained_bytes;
  options.max_stack_bytes = s.limits.stack_bytes;
  options.max_recursion = s.limits.recursion;
  options.max_value_depth = s.limits.value_depth;
  options.max_value_nodes = s.limits.value_nodes;
  options.max_result_bytes = s.limits.value_bytes;
  int status = toad_script_instance_create(s.modules.at(e.module), &options, s.result, instance);
  if (status)
    return error(s, d, status);
  status = toad_script_instance_resolve_export(*instance, {e.name.data(), e.name.size()}, s.result,
                                               handle);
  if (status)
    return error(s, d, status);
  toad_script_export_info_t info{};
  info.struct_size = sizeof(info);
  status = toad_script_instance_get_export_info(*instance, *handle, s.result, &info);
  if (status)
    return error(s, d, status);
  if (e.arity < info.min_arity || e.arity > info.max_arity)
    return diagnostic(d, TOAD_SCRIPT_STATUS_VALIDATION_ERROR,
                      "Export arity does not match binding");
  return 0;
}

int open_impl(void *, rf_script_open *request) {
  if (!request || request->struct_size != sizeof(*request))
    return TOAD_SCRIPT_STATUS_INVALID_ARGUMENT;
  request->session = nullptr;
  std::unique_ptr<rf_script_session> s(new (std::nothrow) rf_script_session);
  if (!s)
    return diagnostic(request->diagnostic, TOAD_SCRIPT_STATUS_OUT_OF_MEMORY,
                      "Script session allocation failed");
  s->limits = request->limits;
  try {
    if (!request->modules || !request->module_count || !request->exports || !request->export_count)
      return diagnostic(request->diagnostic, TOAD_SCRIPT_STATUS_INVALID_ARGUMENT,
                        "Empty script deployment");
    if (request->native_plugin_count > RF_SCRIPT_NATIVE_MODULE_LIMIT ||
        (request->native_plugin_count && (!request->native_registry || !request->native_plugins)))
      return diagnostic(request->diagnostic, TOAD_SCRIPT_STATUS_INVALID_ARGUMENT,
                        "Invalid native module deployment");
    s->context =
        toad_script_init_with_plugin_authorizer(TOAD_SCRIPT_INIT_BARE, deny_plugin, nullptr);
    if (!s->context)
      throw std::bad_alloc();
    toad_script_memory_policy_t memory;
    if (toad_script_memory_policy_init(TOAD_SCRIPT_MEMORY_SERVICE, &memory))
      throw std::runtime_error("Cannot initialize Toad memory policy");
    memory.max_context_bytes = s->limits.retained_bytes;
    memory.max_task_bytes = s->limits.retained_bytes;
    memory.max_external_value_bytes = s->limits.value_bytes;
    memory.scratch_trim_threshold_bytes =
        std::min(memory.scratch_trim_threshold_bytes, memory.max_context_bytes);
    if (toad_script_set_memory_policy(s->context, &memory))
      throw std::length_error("Toad context memory policy rejected");
    int status = toad_script_result_create(s->context, &s->result);
    if (status)
      throw std::bad_alloc();
    for (size_t i = 0; i < request->native_plugin_count; ++i) {
      status = toad_script_context_register_plugin(s->context, request->native_registry,
                                                   request->native_plugins[i], s->result);
      if (status) {
        error(*s, request->diagnostic, status);
        break;
      }
    }
    s->modules.resize(request->module_count);
    for (size_t i = 0; !status && i < request->module_count; ++i) {
      toad_script_module_options_t options;
      toad_script_module_options_init(&options);
      options.module_name = request->modules[i].name;
      options.flags = TOAD_SCRIPT_MODULE_EXPLICIT_CALLS;
      options.max_source_bytes = s->limits.source_bytes;
      status = toad_script_module_compile(s->context, request->modules[i].source, &options,
                                          s->result, &s->modules[i]);
      if (status) {
        error(*s, request->diagnostic, status);
        break;
      }
    }
    if (!status) {
      for (size_t i = 0; i < request->export_count; ++i) {
        auto const &e = request->exports[i];
        if (e.module_index >= s->modules.size()) {
          status = diagnostic(request->diagnostic, TOAD_SCRIPT_STATUS_INVALID_ARGUMENT,
                              "Invalid module index");
          break;
        }
        s->exports.push_back({e.module_index, std::string(e.name.data, e.name.size), e.arity});
        auto &instance = s->pending_instance;
        toad_script_export_handle_t handle = 0;
        status = create_instance(*s, i, &instance, &handle, request->diagnostic);
        if (instance) {
          int closed = toad_script_instance_destroy(instance, s->result);
          if (closed) {
            status = error(*s, request->diagnostic, closed);
            break;
          }
          instance = nullptr;
        }
        if (status)
          break;
      }
    }
    if (!status) {
      request->session = s.release();
      return 0;
    }
    auto saved = request->diagnostic;
    if (cleanup(*s, request->diagnostic)) {
      request->session = s.release();
      return request->diagnostic.status;
    }
    request->diagnostic = saved;
    return status;
  } catch (std::bad_alloc const &) {
    diagnostic(request->diagnostic, TOAD_SCRIPT_STATUS_OUT_OF_MEMORY,
               "Script session allocation failed");
  } catch (std::length_error const &e) {
    diagnostic(request->diagnostic, TOAD_SCRIPT_STATUS_LIMIT_EXCEEDED, e.what());
  } catch (std::exception const &e) {
    diagnostic(request->diagnostic, TOAD_SCRIPT_STATUS_RUNTIME_ERROR, e.what());
  }
  auto saved = request->diagnostic;
  if (cleanup(*s, request->diagnostic)) {
    request->session = s.release();
    return request->diagnostic.status;
  }
  request->diagnostic = saved;
  return saved.status;
}

int invoke_impl(void *, rf_script_call *r) {
  if (!r || r->struct_size != sizeof(*r) || !r->session || !r->sink)
    return TOAD_SCRIPT_STATUS_INVALID_ARGUMENT;
  auto &s = *r->session;
  if (s.owner != std::this_thread::get_id())
    return diagnostic(r->diagnostic, TOAD_SCRIPT_STATUS_WRONG_THREAD,
                      "Script owner thread mismatch");
  if (s.active)
    return diagnostic(r->diagnostic, TOAD_SCRIPT_STATUS_REENTRANT_CALL, "Reentrant script call");
  if (r->binding >= s.exports.size() || r->argument_count != s.exports[r->binding].arity)
    return diagnostic(r->diagnostic, TOAD_SCRIPT_STATUS_INVALID_ARGUMENT,
                      "Invalid script binding/arguments");
  s.active = true;
  struct Active {
    bool &v;
    ~Active() { v = false; }
  } active{s.active};
  auto &instance = s.pending_instance;
  if (instance)
    return diagnostic(r->diagnostic, TOAD_SCRIPT_STATUS_INVALID_STATE,
                      "Previous instance cleanup failed");
  int status = 0;
  try {
    toad_script_export_handle_t handle = 0;
    status = create_instance(s, r->binding, &instance, &handle, r->diagnostic);
    if (!status) {
      toad_script_call_options_t options;
      toad_script_call_options_init(&options);
      options.max_steps = s.limits.steps;
      options.max_loop_iterations = s.limits.loops;
      options.max_recursion = s.limits.recursion;
      options.max_result_bytes = s.limits.value_bytes;
      status = toad_script_instance_call(instance, handle, r->arguments, r->argument_count,
                                         &options, s.result);
      if (status)
        error(s, r->diagnostic, status);
      else {
        toad_script_value_view_t value{};
        status = toad_script_result_get_value(s.result, &value);
        if (!status)
          status = r->sink(r->sink_context, &value);
        if (status)
          diagnostic(r->diagnostic, status, "Script result rejected by host");
      }
    }
  } catch (std::bad_alloc const &) {
    status = diagnostic(r->diagnostic, TOAD_SCRIPT_STATUS_OUT_OF_MEMORY,
                        "Script call allocation failed");
  } catch (std::exception const &e) {
    status = diagnostic(r->diagnostic, TOAD_SCRIPT_STATUS_RUNTIME_ERROR, e.what());
  }
  if (instance) {
    int closed = toad_script_instance_destroy(instance, s.result);
    if (closed)
      return error(s, r->diagnostic, closed);
    instance = nullptr;
  }
  toad_script_result_reset(s.result);
  return status;
}

int close_impl(void *, rf_script_close *r) {
  if (!r || r->struct_size != sizeof(*r) || !r->session)
    return TOAD_SCRIPT_STATUS_INVALID_ARGUMENT;
  auto &s = *r->session;
  if (s.owner != std::this_thread::get_id())
    return diagnostic(r->diagnostic, TOAD_SCRIPT_STATUS_WRONG_THREAD,
                      "Script owner thread mismatch");
  if (s.active)
    return diagnostic(r->diagnostic, TOAD_SCRIPT_STATUS_REENTRANT_CALL, "Script call is active");
  int status = cleanup(s, r->diagnostic);
  if (!status) {
    delete r->session;
    r->session = nullptr;
  }
  return status;
}

CMETA_IMPLEMENTS(rf_script_service, script_impl, 0u, .open = open_impl, .invoke = invoke_impl,
                 .close = close_impl);
cmeta_component(RulesForgeToad, cmeta_provides(rf_script_service));
int marker;
cmeta_status project(void *, const cmeta_object_ref *o, const cmeta_interface_desc *expected,
                     cmeta_interface_projection *out) {
  if (!cmeta_interface_desc_equal(expected, rf_script_service_interface()))
    return CMETA_TRAIT_MISSING;
  out->size = sizeof(*out);
  out->interface = rf_script_service_interface();
  out->self = o->object;
  out->dispatch = &script_impl_vtable;
  return CMETA_OK;
}
cmeta_status create(void *, const cmeta_data_desc *, const void *,
                    const salts_configurator_dependency *, size_t count, cmeta_object_ref *out) {
  if (count)
    return CMETA_INVALID_ARGUMENT;
  return cmeta_object_borrow(out, &marker, &cmeta_data_int, nullptr);
}
const cmeta_object_interface_provider interfaces = {sizeof(interfaces), nullptr, project};
const salts_configurator_provider_binding binding = {
    sizeof(binding),
    SALTS_CONFIGURATOR_PROVIDER_BINDING_ABI_VERSION,
    cmeta_component_meta(RulesForgeToad),
    nullptr,
    &interfaces,
    create,
    nullptr,
    nullptr};
const salts_configurator_provider_binding *get_binding(void *) { return &binding; }
CMETA_IMPLEMENTS(salts_service_host_provider, provider_impl, 0u, .get_binding = get_binding);
salts_service_host_provider provider = {const_cast<salts_configurator_provider_binding *>(&binding),
                                        &provider_impl_vtable};
} // namespace

#define RF_TOAD_EXPORTS(X)                                                                         \
  X(interface, (salts_service_host_provider, &provider), RF_SCRIPT_PROVIDER_EXPORT,                \
    SALTS_SERVICE_HOST_PROVIDER_CONTRACT_ID, SALTS_SERVICE_HOST_PROVIDER_CONTRACT_VERSION, 0)
CMETA_PLUGIN_DECLARE(rulesforge_toad, "rulesforge.toad", (0, 1, 0), RF_TOAD_EXPORTS,
                     CMETA_PLUGIN_PASSIVE());
