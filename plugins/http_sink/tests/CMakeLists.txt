# HTTP Sink Plugin Tests

cmake_minimum_required(VERSION 3.10)
project(http_sink_tests C)

set(CMAKE_C_STANDARD 11)

add_executable(test_http_sink test_http_sink.c)

target_include_directories(
    test_http_sink PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../
                           ${CMAKE_CURRENT_SOURCE_DIR}/../../
                           ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(test_http_sink TurboNet::TinyTest TurboNet::HttpClient TurboNet::Parser)

add_test(NAME http_sink_plugin_test COMMAND test_http_sink)

set_tests_properties(
    http_sink_plugin_test PROPERTIES WORKING_DIRECTORY
                                     ${CMAKE_CURRENT_BINARY_DIR}
)
set_target_properties(test_http_sink PROPERTIES FOLDER plugins/http)

# DLL test (loads plugin at runtime)
add_executable(test_http_sink_dll test_http_sink_dll.c)

target_include_directories(
    test_http_sink_dll PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../
                               ${CMAKE_CURRENT_SOURCE_DIR}/../../
                               ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(test_http_sink_dll TurboNet::TinyTest ${CMAKE_DL_LIBS})

target_compile_definitions(
    test_http_sink_dll PRIVATE
    HTTP_SINK_DLL="$<TARGET_FILE:http_sink_plugin>"
)

add_dependencies(test_http_sink_dll http_sink_plugin)

add_test(NAME http_sink_dll_test COMMAND test_http_sink_dll)

set_tests_properties(
    http_sink_dll_test PROPERTIES WORKING_DIRECTORY
                                  ${CMAKE_CURRENT_BINARY_DIR}
)
set_target_properties(test_http_sink_dll PROPERTIES FOLDER plugins/http)
