cmake_minimum_required(VERSION 3.15)

project(capi_examples CXX)

add_executable(capi_demo capi_demo.cpp)
target_link_libraries(capi_demo PRIVATE drills_capi)

set_target_properties(capi_demo PROPERTIES FOLDER capi/examples)
