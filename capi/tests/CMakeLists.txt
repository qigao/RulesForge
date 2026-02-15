cmake_minimum_required(VERSION 3.15)

project(capi_tests CXX)

add_executable(capi_test capi_test.cpp)
target_link_libraries(capi_test PRIVATE rule_forge TurboNet::TinyTest)
add_test(NAME capi_test COMMAND capi_test)

set_target_properties(capi_test PROPERTIES FOLDER capi/tests)
