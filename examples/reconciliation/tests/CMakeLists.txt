cmake_add_source(scenario_tests DIRS ${CMAKE_CURRENT_SOURCE_DIR} PATTERNS "*.cpp")
add_executable(reconciliation_test ${scenario_tests})
target_link_libraries(reconciliation_test PRIVATE rulesforge_example_support Salts::TinyTest)
target_include_directories(reconciliation_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(reconciliation_test PRIVATE
    RF_SCENARIO_BUNDLE="${scenario_bundle_dir}/bundle-interpreter.json"
    RF_SCENARIO_JIT_BUNDLE="${scenario_bundle_dir}/bundle-jit.json")
cmake_configure_target(reconciliation_test FOLDER examples/reconciliation/tests)
add_dependencies(reconciliation_test reconciliation_bundle)
add_test(NAME reconciliation_test COMMAND reconciliation_test)
add_test(NAME reconciliation_demo.example COMMAND reconciliation_demo ${scenario_bundle_dir}/bundle-interpreter.json)
set_tests_properties(reconciliation_test reconciliation_demo.example PROPERTIES LABELS scenario TIMEOUT 120)
