project(file_source_tests C)

# Test executable
add_executable(test_file_source test_file_source.c)

target_include_directories(
    test_file_source PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../
                             ${CMAKE_CURRENT_SOURCE_DIR}/../../
                             ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(test_file_source TurboNet::TinyTest TurboNet::Utils TurboNet::Parser)

# Add test to CTest
add_test(NAME file_source_plugin_test COMMAND test_file_source)

# Set working directory for test
set_tests_properties(
    file_source_plugin_test PROPERTIES WORKING_DIRECTORY
                                       ${CMAKE_CURRENT_BINARY_DIR}
)
set_target_properties(test_file_source PROPERTIES FOLDER plugins/file)

# DLL test (loads plugin at runtime)
add_executable(test_file_source_dll test_file_source_dll.c)

target_include_directories(
    test_file_source_dll PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../
                                 ${CMAKE_CURRENT_SOURCE_DIR}/../../
                                 ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(
    test_file_source_dll
    TurboNet::TinyTest
    TurboNet::Utils
    TurboNet::Parser
    ${CMAKE_DL_LIBS}
)

target_compile_definitions(
    test_file_source_dll PRIVATE
    FILE_SOURCE_DLL="$<TARGET_FILE:file_source_plugin>"
)

add_dependencies(test_file_source_dll file_source_plugin)

add_test(NAME file_source_dll_test COMMAND test_file_source_dll)

set_tests_properties(
    file_source_dll_test PROPERTIES WORKING_DIRECTORY
                                    ${CMAKE_CURRENT_BINARY_DIR}
)
set_target_properties(test_file_source_dll PROPERTIES FOLDER plugins/file)
