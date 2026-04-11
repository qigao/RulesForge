# RulesForge Router with Object Pools

cmake_minimum_required(VERSION 3.10)
project(RulesForgeRouter C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Source grouping for IDEs
source_group(
    "Header Files" FILES session_pool.h buffer_pool.h route_engine.h
                         plugin_loader.h
)
source_group(
    "Source Files" FILES session_pool.c buffer_pool.c route_engine.c
                         plugin_loader.c
)
source_group("Tests" REGEX "tests/.*")

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../tbe/data_bind
)

# ============================================================
# Session Pool Library
# ============================================================

add_library(session_pool STATIC session_pool.c session_pool.h)

target_include_directories(
    session_pool
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PUBLIC ${CMAKE_SOURCE_DIR}/plugins/
)
target_link_libraries(session_pool PUBLIC TurboNet::Utils)

# ============================================================
# Buffer Pool Library
# ============================================================

add_library(buffer_pool STATIC buffer_pool.c buffer_pool.h)

target_include_directories(buffer_pool PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(buffer_pool PUBLIC TurboNet::Utils)

# ============================================================
# Route Engine Library
# ============================================================

add_library(
    route_engine STATIC route_engine.c route_engine.h plugin_loader.c
                        plugin_loader.h
)

target_include_directories(
    route_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                        ${CMAKE_SOURCE_DIR}/plugins/
)
target_link_libraries(
    route_engine
    PRIVATE session_pool buffer_pool
    PUBLIC TurboNet::Utils rule_forge data_bind TurboScript::ExprTk ${CMAKE_DL_LIBS}
)

# ============================================================
# Benchmark Tool
# ============================================================

add_executable(pool_benchmark tests/pool_benchmark.c)

target_link_libraries(
    pool_benchmark
    PRIVATE session_pool buffer_pool
    PUBLIC TurboNet::Utils rule_forge
)

# ============================================================
# Installation
# ============================================================

install(
    TARGETS session_pool buffer_pool route_engine
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(FILES session_pool.h buffer_pool.h route_engine.h
        DESTINATION include/ruleforge/router
)

# ============================================================
# Testing
# ============================================================

if(ENABLE_TESTS)
    enable_testing()

    add_test(NAME pool_benchmark COMMAND pool_benchmark)

    # Plugin loading test
    add_executable(test_route_engine_plugins tests/test_route_engine_plugins.c)

    target_include_directories(
        test_route_engine_plugins
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../plugins
                ${CMAKE_SOURCE_DIR}/include
    )

    target_link_libraries(
        test_route_engine_plugins route_engine TurboNet::TinyTest data_bind
        TurboNet::Utils
    )

    target_compile_definitions(
        test_route_engine_plugins
        PRIVATE
            FILE_SOURCE_DLL="$<TARGET_FILE:file_source_plugin>"
            HTTP_SINK_DLL="$<TARGET_FILE:http_sink_plugin>"
            ROUTING_RULES_FILE="${CMAKE_CURRENT_SOURCE_DIR}/tests/routing_rules.rfl"
    )

    add_dependencies(test_route_engine_plugins file_source_plugin http_sink_plugin)

    add_test(NAME route_engine_plugin_test COMMAND test_route_engine_plugins)
    set_tests_properties(
        route_engine_plugin_test PROPERTIES WORKING_DIRECTORY
                                            ${CMAKE_CURRENT_BINARY_DIR}
    )

    add_custom_command(
        TARGET test_route_engine_plugins
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/tests
        COMMAND
            ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/routing_rules.rfl
            ${CMAKE_CURRENT_BINARY_DIR}/tests/routing_rules.rfl
    )

    # Core logic test (mock plugins, no DLL dependency)
    add_executable(test_route_engine_core tests/test_route_engine_core.c)

    target_include_directories(
        test_route_engine_core
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../plugins
                ${CMAKE_SOURCE_DIR}/include
                ${CMAKE_CURRENT_SOURCE_DIR}/../tbe/data_bind
    )

    target_link_libraries(
        test_route_engine_core route_engine TurboNet::TinyTest data_bind
    )

    target_compile_definitions(
        test_route_engine_core
        PRIVATE
            ROUTING_RULES_FILE="${CMAKE_CURRENT_SOURCE_DIR}/tests/routing_rules.rfl"
            PAYMENTS_RULES_FILE="${CMAKE_SOURCE_DIR}/capi/examples/payments.rfl"
    )

    add_test(NAME route_engine_core_test COMMAND test_route_engine_core)

    set_tests_properties(
        route_engine_core_test PROPERTIES WORKING_DIRECTORY
                                          ${CMAKE_CURRENT_BINARY_DIR}
    )

    add_custom_command(
        TARGET test_route_engine_core
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/tests
        COMMAND
            ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/routing_rules.rfl
            ${CMAKE_CURRENT_BINARY_DIR}/tests/routing_rules.rfl
    )

    set_target_properties(
        test_route_engine_plugins test_route_engine_core PROPERTIES FOLDER router
    )
endif()

set_target_properties(
    session_pool buffer_pool route_engine pool_benchmark PROPERTIES FOLDER router
)
