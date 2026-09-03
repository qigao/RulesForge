cmake_minimum_required(VERSION 3.15)

project(capi_tests CXX)

add_executable(capi_test capi_test.cpp)
target_include_directories(capi_test PRIVATE ${CMAKE_SOURCE_DIR}/rulesforge/include)
target_link_libraries(capi_test PRIVATE rules_forge Salts::TinyTest)
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

# Value bridge test (standalone, doesn't depend on rules_forge lib)
add_executable(test_value_bridge
    test_value_bridge.c
    ${CMAKE_SOURCE_DIR}/capi/rfl_value_bridge.cpp
    ${CMAKE_SOURCE_DIR}/capi/binary_splitters.c
)
target_include_directories(test_value_bridge PRIVATE
    ${CMAKE_SOURCE_DIR}/capi
    ${CMAKE_SOURCE_DIR}/rulesforge/include
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
    ${CMAKE_SOURCE_DIR}/capi/rfl_value_bridge.cpp
)
target_include_directories(value_bridge_fact_test PRIVATE
    ${CMAKE_SOURCE_DIR}/capi
    ${CMAKE_SOURCE_DIR}/rulesforge/include
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
