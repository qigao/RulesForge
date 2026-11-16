cmake_add_source(scenario_tests DIRS ${CMAKE_CURRENT_SOURCE_DIR} PATTERNS "*.cpp")
add_executable(service_entitlements_test ${scenario_tests})
target_link_libraries(service_entitlements_test PRIVATE rulesforge_example_support Salts::TinyTest)
target_include_directories(service_entitlements_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(service_entitlements_test PRIVATE
    RF_SCENARIO_BUNDLE="${scenario_bundle_dir}/bundle-interpreter.json"
    RF_SCENARIO_JIT_BUNDLE="${scenario_bundle_dir}/bundle-jit.json")
cmake_configure_target(service_entitlements_test FOLDER examples/service_entitlements/tests)
add_dependencies(service_entitlements_test service_entitlements_bundle)
add_test(NAME service_entitlements_test COMMAND service_entitlements_test)
add_test(NAME service_entitlements_demo.example COMMAND service_entitlements_demo ${scenario_bundle_dir}/bundle-interpreter.json)
set_tests_properties(service_entitlements_test service_entitlements_demo.example PROPERTIES LABELS scenario TIMEOUT 120)
