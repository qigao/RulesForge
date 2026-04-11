find_package(cxxopts REQUIRED)
find_package(jsoncons REQUIRED)

add_library(native_table_plugin SHARED native_table_plugin.cpp)
target_include_directories(native_table_plugin PRIVATE ${CMAKE_SOURCE_DIR}/include)
set_target_properties(native_table_plugin PROPERTIES
    OUTPUT_NAME ruleforge_native_plugin
    FOLDER plugins/examples)
target_link_libraries(native_table_plugin PUBLIC TurboNet::Utils)

add_executable(native_function_table_demo native_function_table_demo.cpp)
target_link_libraries(native_function_table_demo PRIVATE rule_forge TurboNet::Utils)
add_dependencies(native_function_table_demo native_table_plugin)
add_custom_command(TARGET native_function_table_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:native_table_plugin>
            $<TARGET_FILE_DIR:native_function_table_demo>/$<TARGET_FILE_NAME:native_table_plugin>)

add_executable(native_functions_demo native_functions_demo.cpp)
target_link_libraries(native_functions_demo PRIVATE rule_forge TurboNet::Utils)

set_target_properties(native_function_table_demo native_functions_demo PROPERTIES FOLDER plugins/examples)
