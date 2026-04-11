find_package(cxxopts REQUIRED)
find_package(jsoncons REQUIRED)

add_executable(capi_demo capi_demo.cpp)
target_link_libraries(capi_demo PRIVATE rule_forge cxxopts::cxxopts jsoncons TurboNet::Utils)
set_target_properties(capi_demo PROPERTIES FOLDER capi/examples)

# Copy sample files to output directory
configure_file(payments.rfl ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/payments.rfl COPYONLY)
configure_file(payments_test_data.csv ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/payments_test_data.csv COPYONLY)
configure_file(payments_test_data.json ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/payments_test_data.json COPYONLY)
