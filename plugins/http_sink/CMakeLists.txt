# HTTP Sink Plugin

cmake_minimum_required(VERSION 3.10)
project(http_sink_plugin C)

set(CMAKE_C_STANDARD 11)

# Find libcurl

# Build plugin as shared library
add_library(http_sink_plugin SHARED http_sink_plugin.c)

target_include_directories(
    http_sink_plugin PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../
                            ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(http_sink_plugin TurboNet::HttpClient TurboNet::Parser)

target_compile_definitions(http_sink_plugin PRIVATE SHARED_CXX)

# Set output directory
set_target_properties(http_sink_plugin PROPERTIES FOLDER plugins/http)

# Installation
install(
    TARGETS http_sink_plugin
    LIBRARY DESTINATION lib/ruleforge/plugins
    RUNTIME DESTINATION lib/ruleforge/plugins
)

if(ENABLE_TESTS)
    add_subdirectory(tests)
endif()
