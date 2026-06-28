cmake_minimum_required(VERSION 3.20)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
# Set additional project information
set(CMAKE_PROJECT_VERSION_MAJOR 0)
set(CMAKE_PROJECT_VERSION_MINOR 3)
set(CMAKE_PROJECT_VERSION_PATCH 0)

set(PROJECT_VERSION
    ${CMAKE_PROJECT_VERSION_MAJOR}.${CMAKE_PROJECT_VERSION_MINOR}.${CMAKE_PROJECT_VERSION_PATCH}
)

set(PROJECT_NAME "RulesForge")
set(PROJECT_DESCRIPTION "A Rete Network based rule engine")
set(PROJECT_WEBSITE "http://github.com/qigao/RulesForge")
set(APPLICATION_LICENSE "Apache2 License")

# Set the project name and language
project(
    ${PROJECT_NAME}
    VERSION ${PROJECT_VERSION}
    DESCRIPTION ${PROJECT_DESCRIPTION}
    HOMEPAGE_URL ${PROJECT_WEBSITE}
    LANGUAGES CXX C
)

include(CTest)
if(MSVC)
    add_compile_options(/experimental:c11atomics)
endif()
# Set the features of the project
include(ProjectOptions.cmake)

# Option to use high-performance Agenda (AgendaV2)
option(RULESFORGE_USE_AGENDA_V2
       "Use high-performance AgendaV2 (10x faster, MQTT optimized)" OFF
)
if(RULESFORGE_USE_AGENDA_V2)
    message(STATUS "Using AgendaV2 (high-performance, MQTT optimized)")
    add_compile_definitions(RULESFORGE_USE_AGENDA_V2)
else()
    message(STATUS "Using original Agenda (precise salience)")
endif()

find_package(c-ares CONFIG REQUIRED)
find_package(base64 CONFIG REQUIRED)
find_package(zstd CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

find_package(TurboNet CONFIG REQUIRED PATHS "${TURBONET_ROOT}")
find_package(TurboScript CONFIG REQUIRED PATHS "${TURBOSCRIPT_ROOT}")

# Output files directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_EXECUTABLE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Cmake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Sanitizers)
add_subdirectory(tools)
include(FindTools)
# Add subdirectories
add_subdirectory(parser)

add_subdirectory(rulesforge)
add_subdirectory(capi)

if(BUILD_TESTING OR ENABLE_TESTS)
    add_custom_target(
        rulesforge_release_smoke
        COMMAND
            ${CMAKE_CTEST_COMMAND}
            --test-dir ${CMAKE_BINARY_DIR}
            -R
            "^(engine_core_test\\.RulesForge|rhs_control_flow_test\\.RulesForge|rhs_invoke_test\\.RulesForge|parser_test\\.parser|parser_semantic_test\\.parser|rhs_parser_test\\.parser|expression_descriptor_test\\.parser|compiled_expression_compat_test\\.parser|capi_test|test_value_bridge|capi_demo_.*_smoke)$"
            --output-on-failure
        USES_TERMINAL
        VERBATIM
        COMMENT "Running RulesForge release smoke tests"
    )
    foreach(release_smoke_dep
            engine_core_test.RulesForge
            rhs_control_flow_test.RulesForge
            rhs_invoke_test.RulesForge
            parser_test.parser
            parser_semantic_test.parser
            rhs_parser_test.parser
            expression_descriptor_test.parser
            compiled_expression_compat_test.parser
            capi_test
            test_value_bridge
            capi_demo)
        if(TARGET ${release_smoke_dep})
            add_dependencies(rulesforge_release_smoke ${release_smoke_dep})
        endif()
    endforeach()
endif()
