#include "script/script_service.h"
#include <cmeta/service_host.h>
#include <cstring>
#include <tinytest.hpp>

namespace {
struct Fixture {
  cmeta_plugin_registry registry{};
  cmeta_plugin_ref plugin{};
  salts_service_host_generation generation{};
  salts_service_host_runtime runtime{};
  salts_service_host_scope scope{};
  salts_configurator_deployment deployment[1]{};
  salts_configurator_instance instance[1]{};
  size_t order[1]{};
  salts_service_host_module module[1]{};
  rf_script_service service{};
  rf_script_session *session = nullptr;
  bool loaded = false, built = false, published = false;
  Fixture() {
    if (!cmeta_interface_borrowed_service_abi_valid(rf_script_service_interface()))
      throw std::runtime_error("script service ABI invalid");
    cmeta_plugin_registry_config config{1};
    if (cmeta_plugin_registry_init(&registry, &config) != CMETA_PLUGIN_OK)
      throw std::runtime_error("registry init");
    auto load = cmeta_plugin_registry_load(&registry, RF_TOAD_PROVIDER_PATH, &plugin);
    if (load != CMETA_PLUGIN_OK)
      throw std::runtime_error(std::string("provider load: ") + cmeta_plugin_status_string(load));
    loaded = true;
    if (cmeta_plugin_registry_start(&registry, plugin) != CMETA_PLUGIN_OK)
      throw std::runtime_error("provider start");
    salts_service_host_generation_storage storage{deployment, 1,     instance, 1,      nullptr,
                                                  0,          order, 1,        module, 1};
    salts_service_host_source source{plugin, RF_SCRIPT_PROVIDER_EXPORT, nullptr, nullptr};
    auto status = salts_service_host_generation_build(&generation, 1, &registry, &storage, nullptr,
                                                      0, &source, 1, nullptr, 0);
    if (status != SALTS_SERVICE_HOST_OK)
      throw std::runtime_error(std::string("generation build: ") +
                               salts_service_host_status_string(status));
    built = true;
    if (salts_service_host_runtime_init(&runtime) != SALTS_SERVICE_HOST_OK)
      throw std::runtime_error("runtime init");
    salts_service_host_generation *previous = nullptr;
    if (salts_service_host_runtime_publish(&runtime, &generation, &previous) !=
        SALTS_SERVICE_HOST_OK)
      throw std::runtime_error("publish");
    published = true;
    if (salts_service_host_scope_acquire(&runtime, &scope) != SALTS_SERVICE_HOST_OK)
      throw std::runtime_error("scope");
    salts_configurator_service found{};
    if (salts_service_host_scope_find_service(&scope, rf_script_service_interface(), &found) !=
        SALTS_SERVICE_HOST_OK)
      throw std::runtime_error("service lookup");
    if (rf_script_service_borrow_from_object(found.object, found.interfaces, &service) != CMETA_OK)
      throw std::runtime_error("service projection");
  }
  ~Fixture() {
    if (session) {
      rf_script_close r{sizeof(rf_script_close), session, {}};
      if (rf_script_service_close(&service, &r))
        std::terminate();
    }
    if (scope.live && salts_service_host_scope_release(&scope) != SALTS_SERVICE_HOST_OK)
      std::terminate();
    salts_service_host_generation *previous = nullptr;
    if (published) {
      salts_service_host_runtime_close(&runtime, &previous);
      if (salts_service_host_generation_drain(&runtime, &generation))
        std::terminate();
    } else if (built)
      salts_service_host_generation_discard(&generation);
    if (runtime.initialized)
      salts_service_host_runtime_destroy(&runtime);
    if (loaded) {
      bool quiet = false;
      cmeta_plugin_registry_request_stop(&registry, plugin);
      cmeta_plugin_registry_poll_quiescent(&registry, plugin, &quiet);
      if (!quiet || cmeta_plugin_registry_unload(&registry, plugin))
        std::terminate();
    }
    cmeta_plugin_registry_destroy(&registry);
  }
  int open(char const *text, rf_script_diagnostic &d) {
    rf_script_module_source source{{"test", 4}, {text, std::strlen(text)}};
    rf_script_export e{0, {"calculate", 9}, 1};
    rf_script_open r{sizeof(rf_script_open),
                     &source,
                     1,
                     &e,
                     1,
                     {4 * 1024 * 1024, 4 * 1024 * 1024, 65536, 64, 100000, 10000, 100,
                      128 * 1024 * 1024, 1024 * 1024, TOAD_SCRIPT_EXEC_INTERPRETER},
                     nullptr,
                     {}};
    int status = rf_script_service_open(&service, &r);
    session = r.session;
    d = r.diagnostic;
    return status;
  }
};
} // namespace

suite("Released Toad provider") {
  it("admits the real DSO and returns a typed result") {
    Fixture f;
    rf_script_diagnostic d{};
    int status = f.open("func calculate(x){return x+7;};export(\"calculate\");", d);
    info("%s", d.message);
    check_equal(status, 0);
    double answer = 0;
    toad_script_value_view_t arg{};
    arg.kind = TOAD_SCRIPT_VALUE_NUMBER;
    arg.as.number = 35;
    rf_script_call call{sizeof(rf_script_call),
                        f.session,
                        0,
                        &arg,
                        1,
                        [](void *p, const toad_script_value_view_t *v) -> int {
                          if (v->kind != TOAD_SCRIPT_VALUE_NUMBER)
                            return TOAD_SCRIPT_STATUS_VALIDATION_ERROR;
                          *static_cast<double *>(p) = v->as.number;
                          return 0;
                        },
                        &answer,
                        {}};
    status = rf_script_service_invoke(&f.service, &call);
    info("%s", call.diagnostic.message);
    check_equal(status, 0);
    check_equal(answer, 42.0);
  }
  it("rejects I/O and imports in the pure computation profile") {
    for (auto source :
         {"func calculate(x){return import(\"rulesforge-policy-probe\");};export(\"calculate\");",
          "func calculate(x){return "
          "file_exists(\"rulesforge-policy-probe-does-not-exist\");};export(\"calculate\");"}) {
      Fixture f;
      rf_script_diagnostic d{};
      int status = f.open(source, d);
      if (!status) {
        toad_script_value_view_t arg{};
        arg.kind = TOAD_SCRIPT_VALUE_INT64;
        rf_script_call request{sizeof(rf_script_call),
                               f.session,
                               0,
                               &arg,
                               1,
                               [](void *, const toad_script_value_view_t *) -> int { return 0; },
                               nullptr,
                               {}};
        status = rf_script_service_invoke(&f.service, &request);
      }
      check_not_equal(status, 0);
    }
  }
  it("rejects a mismatched native request size") {
    Fixture f;
    rf_script_open request{};
    check_equal(rf_script_service_open(&f.service, &request), TOAD_SCRIPT_STATUS_INVALID_ARGUMENT);
    check_null(request.session);
  }
}
