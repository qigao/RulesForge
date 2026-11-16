cmake_add_source(scenario_tests DIRS ${CMAKE_CURRENT_SOURCE_DIR} PATTERNS "*.cpp")
add_executable(production_routing_test ${scenario_tests})
target_link_libraries(production_routing_test PRIVATE rulesforge_example_support Salts::TinyTest)
target_include_directories(production_routing_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(production_routing_test PRIVATE
    RF_SCENARIO_BUNDLE="${scenario_bundle_dir}/bundle-interpreter.json"
    RF_SCENARIO_JIT_BUNDLE="${scenario_bundle_dir}/bundle-jit.json")
cmake_configure_target(production_routing_test FOLDER examples/production_routing/tests)
add_dependencies(production_routing_test production_routing_bundle)
add_test(NAME production_routing_test COMMAND production_routing_test)
add_test(NAME production_routing_demo.example COMMAND production_routing_demo ${scenario_bundle_dir}/bundle-interpreter.json)
set_tests_properties(production_routing_test production_routing_demo.example PROPERTIES LABELS scenario TIMEOUT 120)
