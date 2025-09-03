add_executable(capi_demo capi_demo.cpp)
target_link_libraries(capi_demo PRIVATE rule_forge)

set_target_properties(capi_demo PROPERTIES FOLDER capi/examples)
