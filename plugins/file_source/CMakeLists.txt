# File Source Plugin

cmake_minimum_required(VERSION 3.10)
project(file_source_plugin C)

set(CMAKE_C_STANDARD 11)

# Build plugin as shared library (no external dependencies)
add_library(file_source_plugin SHARED
    file_source_plugin.c
)

target_include_directories(file_source_plugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(file_source_plugin PUBLIC
 TurboNet::Utils
 TurboNet::Parser
)

target_compile_definitions(file_source_plugin PRIVATE SHARED_CXX)
# Set output directory
set_target_properties(file_source_plugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
    FOLDER plugins/file
)

# Installation
install(TARGETS file_source_plugin
    LIBRARY DESTINATION lib/ruleforge/plugins
    RUNTIME DESTINATION lib/ruleforge/plugins
)

if(ENABLE_TESTS)
    add_subdirectory(tests)
endif()
