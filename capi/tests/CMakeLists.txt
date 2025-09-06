cmake_minimum_required(VERSION 3.15)

project(capi_tests CXX)

find_package(Catch2 CONFIG REQUIRED)

add_executable(capi_test capi_test.cpp)
target_link_libraries(capi_test PRIVATE rule_forge Catch2::Catch2WithMain)
add_test(NAME capi_test COMMAND capi_test)

set_target_properties(capi_test PROPERTIES FOLDER capi/tests)
