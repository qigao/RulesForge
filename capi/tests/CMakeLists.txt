cmake_minimum_required(VERSION 3.15)

project(capi_tests CXX)

add_executable(capi_test capi_test.cpp)
target_link_libraries(capi_test PRIVATE rule_forge TurboNet::TinyTest)
add_test(NAME capi_test COMMAND capi_test)

set_target_properties(capi_test PROPERTIES FOLDER capi/tests)

# Value bridge test (standalone, doesn't depend on rule_forge lib)
add_executable(test_value_bridge
    test_value_bridge.c
    ${CMAKE_SOURCE_DIR}/capi/rfl_value_bridge.cpp
    ${CMAKE_SOURCE_DIR}/capi/binary_splitters.c
)
target_include_directories(test_value_bridge PRIVATE
    ${CMAKE_SOURCE_DIR}/capi
    ${CMAKE_SOURCE_DIR}/rulesforge/include
)
target_link_libraries(test_value_bridge PRIVATE RulesForge)
add_test(NAME test_value_bridge COMMAND test_value_bridge)

set_target_properties(test_value_bridge PROPERTIES FOLDER capi/tests)
