cmake_add_source(scenario_tests DIRS ${CMAKE_CURRENT_SOURCE_DIR} PATTERNS "*.cpp")
add_executable(cold_chain_test ${scenario_tests})
target_link_libraries(cold_chain_test PRIVATE rulesforge_example_support Salts::TinyTest)
target_include_directories(cold_chain_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(cold_chain_test PRIVATE
    RF_SCENARIO_BUNDLE="${scenario_bundle_dir}/bundle-interpreter.json"
    RF_SCENARIO_JIT_BUNDLE="${scenario_bundle_dir}/bundle-jit.json")
cmake_configure_target(cold_chain_test FOLDER examples/cold_chain/tests)
add_dependencies(cold_chain_test cold_chain_bundle)
add_test(NAME cold_chain_test COMMAND cold_chain_test)
add_test(NAME cold_chain_demo.example COMMAND cold_chain_demo ${scenario_bundle_dir}/bundle-interpreter.json)
set_tests_properties(cold_chain_test cold_chain_demo.example PROPERTIES LABELS scenario TIMEOUT 120)
