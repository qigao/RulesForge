find_package(cxxopts REQUIRED)
find_package(jsoncons REQUIRED)

add_executable(capi_demo capi_demo.cpp)
target_link_libraries(capi_demo PRIVATE rule_forge cxxopts::cxxopts jsoncons)

set_target_properties(capi_demo PROPERTIES FOLDER capi/examples)

# Copy sample files to output directory
configure_file(payments.rfl ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/payments.rfl COPYONLY)
