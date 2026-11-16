#include "script/script_program.hpp"
#include "script/script_catalog.hpp"
#include "script/script_service.h"
#include "script/script_values.hpp"
#include "runtime/stateful_session.hpp"
#include "rfl_parser.hpp"
#include <array>
#include <cmeta/service_host.h>
#include <cstring>
#include <exception>
#include <mutex>
#include <sstream>
#include <thread>

namespace rulesforge {
namespace {
#define RF_PROGRAM_METHODS(X, I)                                                                   \
  X(I, FR0, void *, root, value, &cmeta_type_void_ptr, CMETA_ABI_OBJECT_POINTER,                   \
    CMETA_RESULT_BORROWED)
CMETA_INTERFACE(rf_program_root, RF_PROGRAM_METHODS);
CMETA_OBJECT_INTERFACE_ADAPTER(rf_program_root);
cmeta_component(RulesForgeProgram,
                cmeta_provides(rf_program_root) cmeta_requires(rf_script_service));

[[noreturn]] void host_error(salts_service_host_status status) {
  if (status == SALTS_SERVICE_HOST_BUSY || status == SALTS_SERVICE_HOST_CAPACITY_EXCEEDED)
    throw std::length_error("Program generation capacity/busy");
  throw std::runtime_error(std::string("ACE: ") + salts_service_host_status_string(status));
}
void host(salts_service_host_status status) {
  if (status)
    host_error(status);
}
void plugin(cmeta_plugin_status status) {
  if (status)
    throw std::runtime_error(std::string("Plugin: ") + cmeta_plugin_status_string(status));
}
void script(int status, rf_script_diagnostic const &d) {
  if (!status)
    return;
  std::ostringstream message;
  message << "Toad status=" << status << " phase=" << d.phase << " at " << d.line << ":" << d.column
          << ": " << d.message;
  if (status == TOAD_SCRIPT_STATUS_OUT_OF_MEMORY)
    throw std::bad_alloc();
  if (status == TOAD_SCRIPT_STATUS_LIMIT_EXCEEDED)
    throw std::length_error(message.str());
  throw ScriptStatusError(status, message.str());
}
rf_script_open open_request(ScriptCatalog const &c, std::vector<rf_script_module_source> &modules,
                            std::vector<rf_script_export> &exports,
                            cmeta_plugin_registry *native_registry,
                            std::vector<cmeta_plugin_ref> const &native_plugins) {
  for (auto const &m : c.modules)
    modules.push_back({{m.name.data(), m.name.size()}, {m.source.data(), m.source.size()}});
  for (auto const &e : c.bindings)
    exports.push_back(
        {e.module, {e.export_name.data(), e.export_name.size()}, e.parameters.size()});
  rf_script_open request{sizeof(rf_script_open), modules.data(), modules.size(), exports.data(),
                         exports.size(),         c.limits,       nullptr,        {}};
  request.native_registry = native_registry;
  request.native_plugins = native_plugins.data();
  request.native_plugin_count = native_plugins.size();
  return request;
}

/* Address-stable generation owner. Context leases end before generation drain. */
struct NativeModules {
  cmeta_plugin_registry registry{};
  std::vector<cmeta_plugin_ref> refs;
  size_t loaded = 0, started = 0;
  bool initialized = false;
  NativeModules() = default;
  NativeModules(NativeModules const &) = delete;
  NativeModules &operator=(NativeModules const &) = delete;
  void open(std::vector<std::string> const &paths) {
    if (paths.empty())
      return;
    refs.resize(paths.size()); // Allocate before acquiring any native resources.
    cmeta_plugin_registry_config config{};
    config.capacity = paths.size();
    plugin(cmeta_plugin_registry_init(&registry, &config));
    initialized = true;
    for (size_t i = 0; i < paths.size(); ++i) {
      plugin(cmeta_plugin_registry_load(&registry, paths[i].c_str(), &refs[i]));
      ++loaded;
      plugin(cmeta_plugin_registry_start(&registry, refs[i]));
      ++started;
    }
  }
  ~NativeModules() {
    if (!initialized)
      return;
    for (size_t i = loaded; i > 0; --i) {
      size_t index = i - 1;
      if (index < started) {
        bool quiet = false;
        if (cmeta_plugin_registry_request_stop(&registry, refs[index]) != CMETA_PLUGIN_OK ||
            cmeta_plugin_registry_poll_quiescent(&registry, refs[index], &quiet) !=
                CMETA_PLUGIN_OK ||
            !quiet)
          std::terminate();
      }
      if (cmeta_plugin_registry_unload(&registry, refs[index]) != CMETA_PLUGIN_OK)
        std::terminate();
    }
    if (cmeta_plugin_registry_destroy(&registry) != CMETA_PLUGIN_OK)
      std::terminate();
  }
};

struct Generation {
  salts_service_host_generation host{};
  salts_configurator_deployment deployments[2]{};
  salts_configurator_instance instances[2]{};
  salts_configurator_dependency dependencies[1]{};
  size_t order[2]{};
  salts_service_host_module modules[1]{};
  salts_configurator_provider_binding root_binding{};
  cmeta_object_interface_provider root_interfaces{};
  cmeta_object_lifecycle root_lifecycle{};
  std::shared_ptr<ScriptCatalog const> catalog;
  NativeModules native;
  std::shared_ptr<KnowledgeBase> kb;
  rf_script_service service{};
  rf_script_session *validation = nullptr;
  size_t sessions = 0;
  int marker = 1;
  std::exception_ptr failure;
};

void *root(void *self) { return self; }
CMETA_IMPLEMENTS(rf_program_root, root_impl, 0u, .root = root);
cmeta_status project(void *context, const cmeta_object_ref *, const cmeta_interface_desc *expected,
                     cmeta_interface_projection *out) {
  if (!cmeta_interface_desc_equal(expected, rf_program_root_interface()))
    return CMETA_TRAIT_MISSING;
  out->size = sizeof(*out);
  out->interface = rf_program_root_interface();
  out->self = context;
  out->dispatch = &root_impl_vtable;
  return CMETA_OK;
}
cmeta_status create_root(void *context, const cmeta_data_desc *, const void *,
                         const salts_configurator_dependency *deps, size_t count,
                         cmeta_object_ref *out) {
  auto &g = *static_cast<Generation *>(context);
  auto admitted = cmeta_object_borrow(out, &g.marker, &cmeta_data_int, nullptr);
  if (admitted != CMETA_OK)
    return admitted;
  admitted = cmeta_object_take(out, &g.root_lifecycle);
  if (admitted != CMETA_OK)
    return admitted;
  try {
    salts_configurator_dependency const *dependency = nullptr;
    if (salts_configurator_dependency_find(deps, count, rf_script_service_interface(), &dependency))
      throw std::runtime_error("Missing Toad service dependency");
    if (rf_script_service_borrow_from_object(dependency->provider_instance,
                                             dependency->provider_interfaces, &g.service))
      throw std::runtime_error("Toad service projection failed");
    std::vector<rf_script_module_source> modules;
    std::vector<rf_script_export> exports;
    auto request = open_request(*g.catalog, modules, exports, &g.native.registry, g.native.refs);
    int status = rf_script_service_open(&g.service, &request);
    g.validation = request.session;
    if (g.validation) {
      rf_script_close close{sizeof(rf_script_close), g.validation, {}};
      int closed = rf_script_service_close(&g.service, &close);
      g.validation = close.session;
      script(closed, close.diagnostic);
    }
    script(status, request.diagnostic);
    ParsingResult result;
    g.kb = build_knowledge_base(g.catalog->rule_source, result, g.catalog->rule_path, false,
                                g.catalog);
    if (!result.success || !g.kb)
      throw std::invalid_argument(result.errors.empty() ? "Rule compilation failed"
                                                        : result.errors.front().to_string());
    return CMETA_OK;
  } catch (...) {
    g.failure = std::current_exception();
    return CMETA_CALLBACK_ERROR;
  }
}
void destroy_root(void *context, void *) {
  auto &g = *static_cast<Generation *>(context);
  if (g.validation) {
    rf_script_close close{sizeof(rf_script_close), g.validation, {}};
    // A non-reporting ObjectRef finalizer cannot release its enclosing lease
    // after an ownership invariant failure. Ordinary script errors never enter
    // this fatal path; their partial objects are closed on the creating thread.
    if (rf_script_service_close(&g.service, &close))
      std::terminate();
    g.validation = nullptr;
  }
  g.kb.reset();
  g.service = {};
}
} // namespace

struct ScriptProgram::State {
  std::mutex mutex;
  cmeta_plugin_registry registry{};
  cmeta_plugin_ref provider{};
  salts_service_host_runtime runtime{};
  std::array<std::unique_ptr<Generation>, 2> generations;
  std::string provider_path;
  uint64_t next_generation = 1;
  bool registry_ready = false, loaded = false, started = false, closed = false, destroyed = false;

  void settle() {
    for (auto &g : generations) {
      if (!g)
        continue;
      if (g->sessions)
        throw std::length_error("Program still has live sessions");
      if (g->host.runtime_owner)
        host(salts_service_host_generation_drain(&runtime, &g->host));
      else if (g->host.state == SALTS_SERVICE_HOST_GENERATION_BUILT)
        host(salts_service_host_generation_discard(&g->host));
      else if (g->host.state == SALTS_SERVICE_HOST_GENERATION_FAILED && g->host.module_count)
        host(salts_service_host_generation_recover(&g->host));
      g.reset();
    }
    if (runtime.initialized)
      host(salts_service_host_runtime_destroy(&runtime));
    if (started) {
      plugin(cmeta_plugin_registry_request_stop(&registry, provider));
      started = false;
    }
    if (loaded) {
      bool quiet = false;
      plugin(cmeta_plugin_registry_poll_quiescent(&registry, provider, &quiet));
      if (!quiet)
        throw std::length_error("Toad provider is not quiescent");
      plugin(cmeta_plugin_registry_unload(&registry, provider));
      loaded = false;
    }
    if (registry_ready) {
      plugin(cmeta_plugin_registry_destroy(&registry));
      registry_ready = false;
    }
    destroyed = true;
  }
  ~State() {
    if (destroyed)
      return;
    try {
      if (runtime.initialized && runtime.current) {
        salts_service_host_generation *old = nullptr;
        host(salts_service_host_runtime_close(&runtime, &old));
      }
      settle();
    } catch (...) {
      std::terminate();
    }
  }
};

struct ScriptProgramSession::State {
  ScriptProgram::State *program;
  salts_service_host_scope scope{};
  Generation *generation = nullptr;
  rf_script_session *runtime = nullptr;
  bool counted = false;
  bool rhs_active = false;
  size_t rhs_result_bytes = 0;
  std::thread::id owner = std::this_thread::get_id();
  explicit State(ScriptProgram::State *p) : program(p) {}
  ~State() {
    if (runtime) {
      rf_script_close close{sizeof(rf_script_close), runtime, {}};
      if (rf_script_service_close(&generation->service, &close))
        std::terminate();
      runtime = nullptr;
    }
    if (scope.live) {
      std::lock_guard lock(program->mutex);
      if (salts_service_host_scope_release(&scope))
        std::terminate();
      if (counted)
        --generation->sessions;
    }
  }
};

ScriptProgram::ScriptProgram(std::string const &bundle) : state_(std::make_unique<State>()) {
  // Keep the selected Script runtime loaded for the host library lifetime;
  // provider generations only reload the adapter and immutable source bundles.
  if (std::strcmp(toad_script_version(), TOAD_SCRIPT_VERSION_STRING) != 0)
    throw std::runtime_error("Loaded Toad runtime does not match the selected SDK");
  auto catalog = load_script_catalog(bundle);
  auto &s = *state_;
  cmeta_plugin_registry_config config{1};
  plugin(cmeta_plugin_registry_init(&s.registry, &config));
  s.registry_ready = true;
  s.provider_path = catalog->provider_path;
  plugin(cmeta_plugin_registry_load(&s.registry, s.provider_path.c_str(), &s.provider));
  s.loaded = true;
  plugin(cmeta_plugin_registry_start(&s.registry, s.provider));
  s.started = true;
  host(salts_service_host_runtime_init(&s.runtime));
  reload(bundle);
}
ScriptProgram::~ScriptProgram() = default;

void ScriptProgram::reload(std::string const &bundle) {
  auto &s = *state_;
  size_t slot = 2;
  {
    std::lock_guard lock(s.mutex);
    if (s.closed)
      throw std::logic_error("Program is closed");
    for (size_t i = 0; i < s.generations.size(); ++i) {
      auto &g = s.generations[i];
      if (g && &g->host != s.runtime.current && !g->sessions) {
        host(salts_service_host_generation_drain(&s.runtime, &g->host));
        g.reset();
      }
      if (!g)
        slot = i;
    }
    if (slot == 2)
      throw std::length_error("Two Program generations are still attached");
    if (s.next_generation == UINT64_MAX)
      throw std::length_error("Program generation ID exhausted");
  }
  auto g = std::make_unique<Generation>();
  g->catalog = load_script_catalog(bundle);
  if (g->catalog->provider_path != s.provider_path)
    throw std::invalid_argument("Changing the native provider requires a new Program");
  g->native.open(g->catalog->native_modules);
  g->root_interfaces = {sizeof(g->root_interfaces), g.get(), project};
  g->root_lifecycle.size = sizeof(g->root_lifecycle);
  g->root_lifecycle.context = g.get();
  g->root_lifecycle.destroy = destroy_root;
  g->root_binding = {sizeof(g->root_binding),
                     SALTS_CONFIGURATOR_PROVIDER_BINDING_ABI_VERSION,
                     cmeta_component_meta(RulesForgeProgram),
                     g.get(),
                     nullptr,
                     create_root,
                     nullptr,
                     nullptr};
  g->root_binding.interfaces = &g->root_interfaces;
  salts_configurator_deployment root_source{&g->root_binding, nullptr, nullptr};
  salts_service_host_source source{s.provider, RF_SCRIPT_PROVIDER_EXPORT, nullptr, nullptr};
  salts_service_host_generation_storage storage{
      g->deployments, 2, g->instances, 2, g->dependencies, 1, g->order, 2, g->modules, 1};
  auto status = salts_service_host_generation_build(
      &g->host, s.next_generation, &s.registry, &storage, &root_source, 1, &source, 1, nullptr, 0);
  if (status) {
    if (g->host.module_count)
      host(salts_service_host_generation_recover(&g->host));
    if (g->failure)
      std::rethrow_exception(g->failure);
    throw std::invalid_argument(std::string("Program compilation failed: ") +
                                salts_service_host_status_string(status));
  }
  std::lock_guard lock(s.mutex);
  salts_service_host_generation *previous = nullptr;
  status = salts_service_host_runtime_publish(&s.runtime, &g->host, &previous);
  if (status) {
    host(salts_service_host_generation_discard(&g->host));
    host_error(status);
  }
  ++s.next_generation;
  s.generations[slot] = std::move(g);
}

std::unique_ptr<ScriptProgramSession> ScriptProgram::create_session() {
  auto &s = *state_;
  auto session = std::make_unique<ScriptProgramSession::State>(&s);
  {
    std::lock_guard lock(s.mutex);
    if (s.closed)
      throw std::logic_error("Program is closed");
    host(salts_service_host_scope_acquire(&s.runtime, &session->scope));
    salts_configurator_service service{};
    host(salts_service_host_scope_find_service(&session->scope, rf_program_root_interface(),
                                               &service));
    rf_program_root root_service{};
    if (rf_program_root_borrow_from_object(service.object, service.interfaces, &root_service))
      throw std::runtime_error("Program root projection failed");
    session->generation = static_cast<Generation *>(rf_program_root_root(&root_service));
    if (session->generation->sessions >= session->generation->catalog->max_sessions) {
      host(salts_service_host_scope_release(&session->scope));
      throw std::length_error("Program session capacity exceeded");
    }
    ++session->generation->sessions;
    session->counted = true;
  }
  std::vector<rf_script_module_source> modules;
  std::vector<rf_script_export> exports;
  auto &generation = *session->generation;
  auto request = open_request(*generation.catalog, modules, exports, &generation.native.registry,
                              generation.native.refs);
  int status = rf_script_service_open(&session->generation->service, &request);
  session->runtime = request.session;
  script(status, request.diagnostic);
  return std::unique_ptr<ScriptProgramSession>(new ScriptProgramSession(std::move(session)));
}
void ScriptProgram::close() {
  auto &s = *state_;
  std::lock_guard lock(s.mutex);
  if (s.closed)
    return;
  salts_service_host_generation *old = nullptr;
  host(salts_service_host_runtime_close(&s.runtime, &old));
  s.closed = true;
}
void ScriptProgram::destroy() {
  auto &s = *state_;
  std::lock_guard lock(s.mutex);
  if (!s.closed)
    throw std::logic_error("Close Program before destroy");
  s.settle();
}

ScriptProgramSession::ScriptProgramSession(std::unique_ptr<State> s) : state_(std::move(s)) {}
ScriptProgramSession::~ScriptProgramSession() = default;
bool ScriptProgramSession::owner_thread() const {
  return state_->owner == std::this_thread::get_id();
}
void ScriptProgramSession::begin_rhs() {
  if (!owner_thread() || state_->rhs_active)
    throw std::logic_error("Invalid script RHS admission");
  state_->rhs_result_bytes = 0;
  state_->rhs_active = true;
}
void ScriptProgramSession::end_rhs() noexcept {
  state_->rhs_result_bytes = 0;
  state_->rhs_active = false;
}
void ScriptProgramSession::close_runtime() {
  auto &s = *state_;
  if (!owner_thread())
    throw std::logic_error("Program session owner thread mismatch");
  if (!s.runtime)
    return;
  rf_script_close close{sizeof(rf_script_close), s.runtime, {}};
  script(rf_script_service_close(&s.generation->service, &close), close.diagnostic);
  s.runtime = close.session;
}
std::unique_ptr<StatefulSession> ScriptProgramSession::create_engine() {
  auto result = state_->generation->kb->create_session();
  result->set_script_invoker(this);
  return result;
}

ConstraintValue ScriptProgramSession::invoke(size_t binding,
                                             std::span<ConstraintValue const> arguments) {
  auto &s = *state_;
  if (!owner_thread())
    throw std::logic_error("Program session owner thread mismatch");
  auto const &catalog = *s.generation->catalog;
  auto const &spec = catalog.bindings.at(binding);
  auto label = "generation=" + std::to_string(s.scope.generation_id) + " binding=" + spec.name +
               " export=" + spec.export_name + ": ";
  try {
    if (arguments.size() != spec.parameters.size())
      throw std::invalid_argument("Script argument count mismatch");
    ScriptValues input(catalog.limits);
    std::vector<toad_script_value_view_t> views;
    views.reserve(arguments.size());
    for (size_t i = 0; i < arguments.size(); ++i) {
      try {
        views.push_back(input.view(arguments[i], spec.parameters[i]));
      } catch (std::runtime_error const &error) {
        throw std::runtime_error("Argument " + std::to_string(i + 1) + ": " + error.what());
      }
    }
    struct Sink {
      ScriptValues values;
      ScriptShape const &shape;
      ConstraintValue result;
      std::exception_ptr error;
      bool accepted = false;
    };
    auto remaining = catalog.limits;
    remaining.value_bytes -= s.rhs_result_bytes;
    Sink sink{ScriptValues(remaining), spec.result, NilValue{}, {}, false};
    rf_script_call request{sizeof(rf_script_call),
                           s.runtime,
                           binding,
                           views.data(),
                           views.size(),
                           [](void *context, toad_script_value_view_t const *value) -> int {
                             auto &sink = *static_cast<Sink *>(context);
                             try {
                               if (sink.accepted)
                                 throw std::logic_error("Duplicate script result");
                               sink.result = sink.values.copy(*value, sink.shape);
                               sink.accepted = true;
                               return 0;
                             } catch (...) {
                               sink.error = std::current_exception();
                               return TOAD_SCRIPT_STATUS_VALIDATION_ERROR;
                             }
                           },
                           &sink,
                           {}};
    int status = rf_script_service_invoke(&s.generation->service, &request);
    if (sink.error)
      std::rethrow_exception(sink.error);
    script(status, request.diagnostic);
    if (!sink.accepted)
      throw std::runtime_error("Script provider omitted its result");
    s.rhs_result_bytes += sink.values.bytes_used();
    return std::move(sink.result);
  } catch (std::bad_alloc const &) {
    throw;
  } catch (std::length_error const &e) {
    throw std::length_error(label + e.what());
  } catch (ScriptStatusError const &e) {
    throw ScriptStatusError(e.status, label + e.what());
  } catch (std::exception const &e) {
    throw ScriptStatusError(TOAD_SCRIPT_STATUS_VALIDATION_ERROR, label + e.what());
  }
}
} // namespace rulesforge
