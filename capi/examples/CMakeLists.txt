find_package(cxxopts REQUIRED)
find_package(jsoncons REQUIRED)

add_executable(capi_demo capi_demo.cpp)
target_link_libraries(capi_demo PRIVATE rule_forge cxxopts::cxxopts jsoncons  TurboNet::Utils)

add_executable(native_functions_demo native_functions_demo.cpp)
target_link_libraries(native_functions_demo PRIVATE rule_forge TurboNet::Utils)

add_library(native_table_plugin SHARED native_table_plugin.cpp)
target_include_directories(native_table_plugin PRIVATE ${CMAKE_SOURCE_DIR}/capi/include)
set_target_properties(native_table_plugin PROPERTIES
    OUTPUT_NAME ruleforge_native_plugin
    FOLDER capi/examples)

add_executable(native_function_table_demo native_function_table_demo.cpp)
target_link_libraries(native_function_table_demo PRIVATE rule_forge TurboNet::Utils)
add_dependencies(native_function_table_demo native_table_plugin)
add_custom_command(TARGET native_function_table_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:native_table_plugin>
            $<TARGET_FILE_DIR:native_function_table_demo>/$<TARGET_FILE_NAME:native_table_plugin>)

set_target_properties(capi_demo native_functions_demo native_function_table_demo PROPERTIES FOLDER capi/examples)

# Copy sample files to output directory
configure_file(payments.rfl ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/payments.rfl COPYONLY)
configure_file(payments_test_data.csv ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/payments_test_data.csv COPYONLY)
