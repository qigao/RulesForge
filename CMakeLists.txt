cmake_minimum_required(VERSION 3.20)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
# Set additional project information
set(CMAKE_PROJECT_VERSION_MAJOR 0)
set(CMAKE_PROJECT_VERSION_MINOR 9)
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

find_package(OpenSSL REQUIRED)
find_package(Salts CONFIG REQUIRED PATHS "$ENV{SALTS_ROOT}" NO_DEFAULT_PATH)
find_package(SaltsUtils CONFIG REQUIRED
             PATHS "$ENV{SALTS_UTILS_ROOT}" NO_DEFAULT_PATH)

if(NOT DEFINED ENV{DATABIND_ROOT} OR "$ENV{DATABIND_ROOT}" STREQUAL "")
    message(FATAL_ERROR "DATABIND_ROOT must name the installed DataBind profile")
endif()
if(NOT IS_DIRECTORY "$ENV{DATABIND_ROOT}")
    message(FATAL_ERROR
            "DATABIND_ROOT is not a directory: $ENV{DATABIND_ROOT}")
endif()
find_package(DataBind 3 CONFIG REQUIRED COMPONENTS Runtime
             PATHS "$ENV{DATABIND_ROOT}" NO_DEFAULT_PATH)
if(NOT TARGET DataBind::Runtime)
    message(FATAL_ERROR
            "DATABIND_ROOT package does not provide DataBind::Runtime")
endif()
if(TARGET Salts::DataBind OR TARGET Salts::DataBindCore OR
   TARGET Salts::DataBindCMeta OR TARGET Salts::DataBindCFlow OR
   TARGET Salts::TbeSchema)
    message(FATAL_ERROR
            "Legacy SaltsUtils-owned DataBind/TBE targets must not be present")
endif()

include(CheckCSourceCompiles)
set(RULESFORGE_MINIMUM_DATABIND_VERSION 30000)
set(RULESFORGE_REQUIRED_DATABIND_ABI 9)
set(_RULESFORGE_SAVED_REQUIRED_LIBRARIES "${CMAKE_REQUIRED_LIBRARIES}")
set(_RULESFORGE_SAVED_REQUIRED_DEFINITIONS "${CMAKE_REQUIRED_DEFINITIONS}")
set(CMAKE_REQUIRED_LIBRARIES DataBind::Runtime)
set(CMAKE_REQUIRED_DEFINITIONS
    "-DRULESFORGE_MINIMUM_DATABIND_VERSION=${RULESFORGE_MINIMUM_DATABIND_VERSION}"
    "-DRULESFORGE_REQUIRED_DATABIND_ABI=${RULESFORGE_REQUIRED_DATABIND_ABI}")
unset(RULESFORGE_HAS_DATABIND_3_0 CACHE)
check_c_source_compiles([[
  #include <data_bind.h>
  #if DATA_BIND_VERSION < RULESFORGE_MINIMUM_DATABIND_VERSION
  #error "RulesForge requires DataBind 3.0.0 or newer"
  #endif
  #if DATA_BIND_ABI_VERSION != RULESFORGE_REQUIRED_DATABIND_ABI
  #error "RulesForge requires DataBind ABI 9"
  #endif
  int main() {
    DataBindStreamConfig config = DATA_BIND_STREAM_CONFIG_INIT;
    data_bind_stream_t *stream = NULL;
    DataBindValue *value = NULL;
    config.format = DATA_BIND_FORMAT_JSON;
    config.selection = DATA_BIND_STREAM_SELECT_ROOT;
    config.type_name = "RulesForgeProbe";
    config.out_value = &value;
    return (int)data_bind_stream_create(NULL, &config, &stream, NULL);
  }
]] RULESFORGE_HAS_DATABIND_3_0)
set(CMAKE_REQUIRED_LIBRARIES "${_RULESFORGE_SAVED_REQUIRED_LIBRARIES}")
set(CMAKE_REQUIRED_DEFINITIONS "${_RULESFORGE_SAVED_REQUIRED_DEFINITIONS}")
unset(_RULESFORGE_SAVED_REQUIRED_LIBRARIES)
unset(_RULESFORGE_SAVED_REQUIRED_DEFINITIONS)
if(NOT RULESFORGE_HAS_DATABIND_3_0)
    message(FATAL_ERROR
            "RulesForge requires DataBind 3.0.0 or newer with ABI 9")
endif()

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

set(RULESFORGE_ENGINE_TARGET rulesforge_engine_objects)
add_subdirectory(rulesforge)
add_subdirectory(capi)

if(BUILD_TESTING OR ENABLE_TESTS)
    add_custom_target(
        rulesforge_release_smoke
        COMMAND
            ${CMAKE_CTEST_COMMAND}
            --test-dir ${CMAKE_BINARY_DIR}
            -R
            "^(engine_core_test\\.RulesForge|rhs_control_flow_test\\.RulesForge|parser_test\\.parser|parser_semantic_test\\.parser|rhs_parser_test\\.parser|expression_descriptor_test\\.parser|compiled_expression_compat_test\\.parser|capi_test|test_value_bridge|capi_demo_.*_smoke)$"
            --output-on-failure
        USES_TERMINAL
        VERBATIM
        COMMENT "Running RulesForge release smoke tests"
    )
    foreach(release_smoke_dep
            engine_core_test.RulesForge
            rhs_control_flow_test.RulesForge
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
