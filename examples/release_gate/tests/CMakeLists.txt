cmake_add_source(scenario_tests DIRS ${CMAKE_CURRENT_SOURCE_DIR} PATTERNS "*.cpp")
add_executable(release_gate_test ${scenario_tests})
target_link_libraries(release_gate_test PRIVATE rulesforge_example_support Salts::TinyTest)
target_include_directories(release_gate_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(release_gate_test PRIVATE
    RF_SCENARIO_BUNDLE="${scenario_bundle_dir}/bundle-interpreter.json"
    RF_SCENARIO_JIT_BUNDLE="${scenario_bundle_dir}/bundle-jit.json")
cmake_configure_target(release_gate_test FOLDER examples/release_gate/tests)
add_dependencies(release_gate_test release_gate_bundle)
add_test(NAME release_gate_test COMMAND release_gate_test)
add_test(NAME release_gate_demo.example COMMAND release_gate_demo ${scenario_bundle_dir}/bundle-interpreter.json)
set_tests_properties(release_gate_test release_gate_demo.example PROPERTIES LABELS scenario TIMEOUT 120)
