cmake_minimum_required(VERSION 3.15)

project(capi_tests CXX)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME examples_cli_and_data COMMAND Python3::Interpreter
    ${CMAKE_CURRENT_SOURCE_DIR}/examples_test.py
    --root ${CMAKE_SOURCE_DIR} --demo $<TARGET_FILE:capi_demo>)

add_executable(maintenance_program_test maintenance_program_test.cpp)
cmake_configure_target(maintenance_program_test FOLDER rulesforge/tests)
target_link_libraries(maintenance_program_test PRIVATE RulesForge Salts::TinyTest Threads::Threads)
target_compile_definitions(maintenance_program_test PRIVATE
    RF_MAINTENANCE_PROVIDER_NAME="$<TARGET_FILE_NAME:rulesforge_toad>"
    RF_MAINTENANCE_PROVIDER_PATH="$<TARGET_FILE:rulesforge_toad>"
    RF_MAINTENANCE_BUNDLE="${CMAKE_BINARY_DIR}/examples/maintenance/bundle/bundle.json"
    RF_MAINTENANCE_CONSERVATIVE_BUNDLE="${CMAKE_BINARY_DIR}/examples/maintenance/bundle/bundle-conservative.json")
add_dependencies(maintenance_program_test maintenance_bundle)
add_test(NAME maintenance_program_test COMMAND maintenance_program_test)
add_test(NAME maintenance_demo.example COMMAND $<TARGET_FILE:maintenance_demo>
    ${CMAKE_BINARY_DIR}/examples/maintenance/bundle/bundle.json)

add_executable(toad_provider_test toad_provider_test.cpp)
cmake_configure_target(toad_provider_test FOLDER rulesforge/tests)
target_include_directories(toad_provider_test PRIVATE ${CMAKE_SOURCE_DIR}/core/include)
# The direct provider fixture needs DataBind's runtime path even though the
# Toad DSO keeps that dependency private in its imported target interface.
target_link_libraries(toad_provider_test PRIVATE CMeta::ACE Toad::Script Salts::DataBind Salts::TinyTest)
target_compile_definitions(toad_provider_test PRIVATE RF_TOAD_PROVIDER_PATH="$<TARGET_FILE:rulesforge_toad>")
add_dependencies(toad_provider_test rulesforge_toad)
add_test(NAME toad_provider_test COMMAND toad_provider_test)

set(program_fixture_dir ${CMAKE_CURRENT_BINARY_DIR}/program-fixture)
file(MAKE_DIRECTORY ${program_fixture_dir})
foreach(name order.schema pricing.rfl pricing.toad tax.toad)
    configure_file(fixtures/program/${name} ${program_fixture_dir}/${name} COPYONLY)
endforeach()
file(GENERATE OUTPUT ${program_fixture_dir}/bundle.json INPUT ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/program/bundle.json.in)
add_executable(script_program_test script_program_test.cpp)
target_include_directories(script_program_test PRIVATE ${CMAKE_SOURCE_DIR}/core/include)
cmake_configure_target(script_program_test FOLDER rulesforge/tests)
target_link_libraries(script_program_test PRIVATE RulesForge Salts::TinyTest Threads::Threads)
target_compile_definitions(script_program_test PRIVATE RF_PROGRAM_TEST_DIR="${program_fixture_dir}")
set(program_fixture_provider ${program_fixture_dir}/${CMAKE_SHARED_MODULE_PREFIX}rulesforge_toad${CMAKE_SHARED_MODULE_SUFFIX})
set(program_fixture_copy_commands)
if(WIN32)
    list(APPEND program_fixture_copy_commands
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:rulesforge_toad> ${program_fixture_dir})
endif()
add_custom_command(OUTPUT ${program_fixture_provider}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:rulesforge_toad> ${program_fixture_dir}
    ${program_fixture_copy_commands}
    DEPENDS rulesforge_toad $<TARGET_FILE:rulesforge_toad>
    COMMAND_EXPAND_LISTS)
add_custom_target(script_program_fixture DEPENDS ${program_fixture_provider})
add_dependencies(script_program_test script_program_fixture)
add_test(NAME script_program_test COMMAND script_program_test)
add_test(NAME script_program_demo.example COMMAND $<TARGET_FILE:script_program_demo>
    ${program_fixture_dir}/bundle.json Order "{\"amount\":15,\"total\":0,\"done\":false}")
set_tests_properties(script_program_test script_program_demo.example PROPERTIES
    RESOURCE_LOCK rulesforge_program_fixture)

add_executable(capi_test capi_test.cpp)
target_include_directories(capi_test PRIVATE ${CMAKE_SOURCE_DIR}/core/include)
target_link_libraries(capi_test PRIVATE RulesForge Salts::TinyTest)
add_test(NAME capi_test COMMAND capi_test)

set_target_properties(capi_test PROPERTIES FOLDER capi/tests)
if(WIN32)
    add_custom_command(
        TARGET capi_test
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:capi_test>
                $<TARGET_FILE_DIR:capi_test>
        COMMAND_EXPAND_LISTS
    )
endif()

# Value bridge test (standalone, doesn't depend on the RulesForge library)
add_executable(test_value_bridge
    test_value_bridge.c
    ${CMAKE_SOURCE_DIR}/rulesforge/rfl_value_bridge.cpp
)
target_include_directories(test_value_bridge PRIVATE
    ${CMAKE_SOURCE_DIR}/rulesforge
    ${CMAKE_SOURCE_DIR}/core/include
)
target_link_libraries(test_value_bridge PRIVATE rfl_parser)
add_test(NAME test_value_bridge COMMAND test_value_bridge)

set_target_properties(test_value_bridge PROPERTIES FOLDER capi/tests)
if(WIN32)
    add_custom_command(
        TARGET test_value_bridge
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:test_value_bridge>
                $<TARGET_FILE_DIR:test_value_bridge>
        COMMAND_EXPAND_LISTS
    )
endif()

add_executable(value_bridge_fact_test
    value_bridge_fact_test.cpp
    ${CMAKE_SOURCE_DIR}/rulesforge/rfl_value_bridge.cpp
)
target_include_directories(value_bridge_fact_test PRIVATE
    ${CMAKE_SOURCE_DIR}/rulesforge
    ${CMAKE_SOURCE_DIR}/core/include
)
target_link_libraries(value_bridge_fact_test PRIVATE rfl_parser)
add_test(NAME value_bridge_fact_test COMMAND value_bridge_fact_test)

set_target_properties(value_bridge_fact_test PROPERTIES FOLDER capi/tests)
if(WIN32)
    add_custom_command(
        TARGET value_bridge_fact_test
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:value_bridge_fact_test>
                $<TARGET_FILE_DIR:value_bridge_fact_test>
        COMMAND_EXPAND_LISTS
    )
endif()
