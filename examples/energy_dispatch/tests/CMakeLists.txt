cmake_add_source(scenario_tests DIRS ${CMAKE_CURRENT_SOURCE_DIR} PATTERNS "*.cpp")
add_executable(energy_dispatch_test ${scenario_tests})
target_link_libraries(energy_dispatch_test PRIVATE rulesforge_example_support Salts::TinyTest)
target_include_directories(energy_dispatch_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(energy_dispatch_test PRIVATE
    RF_SCENARIO_BUNDLE="${scenario_bundle_dir}/bundle-interpreter.json"
    RF_SCENARIO_JIT_BUNDLE="${scenario_bundle_dir}/bundle-jit.json")
cmake_configure_target(energy_dispatch_test FOLDER examples/energy_dispatch/tests)
add_dependencies(energy_dispatch_test energy_dispatch_bundle)
add_test(NAME energy_dispatch_test COMMAND energy_dispatch_test)
add_test(NAME energy_dispatch_demo.example COMMAND energy_dispatch_demo ${scenario_bundle_dir}/bundle-interpreter.json)
set_tests_properties(energy_dispatch_test energy_dispatch_demo.example PROPERTIES LABELS scenario TIMEOUT 120)
