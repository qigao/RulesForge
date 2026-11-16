cmake_add_source(scenario_tests DIRS ${CMAKE_CURRENT_SOURCE_DIR} PATTERNS "*.cpp")
add_executable(quality_release_test ${scenario_tests})
target_link_libraries(quality_release_test PRIVATE rulesforge_example_support Salts::TinyTest)
target_include_directories(quality_release_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(quality_release_test PRIVATE
    RF_SCENARIO_BUNDLE="${scenario_bundle_dir}/bundle-interpreter.json"
    RF_SCENARIO_JIT_BUNDLE="${scenario_bundle_dir}/bundle-jit.json")
cmake_configure_target(quality_release_test FOLDER examples/quality_release/tests)
add_dependencies(quality_release_test quality_release_bundle)
add_test(NAME quality_release_test COMMAND quality_release_test)
add_test(NAME quality_release_demo.example COMMAND quality_release_demo ${scenario_bundle_dir}/bundle-interpreter.json)
set_tests_properties(quality_release_test quality_release_demo.example PROPERTIES LABELS scenario TIMEOUT 120)
