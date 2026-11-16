set(TARGET_NAME engine)
add_subdirectory(parser)

set(RULESFORGE_ENGINE_TARGET engine)

# Model implementations belong to rfl_parser so parser-only tools and the engine
# share one definition. Runtime implementation headers remain private to this target.
cmake_add_source(
    SOURCES RECURSE
    DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
         ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime
         ${CMAKE_CURRENT_SOURCE_DIR}/src/execution
         ${CMAKE_CURRENT_SOURCE_DIR}/src/script
         ${CMAKE_CURRENT_SOURCE_DIR}/src/diagnostics
         ${CMAKE_CURRENT_SOURCE_DIR}/src/rete
)

# Compile the engine once for the shared C ABI and the in-tree C++ tests without
# producing a standalone C++ static library.
add_library(${RULESFORGE_ENGINE_TARGET} OBJECT ${SOURCES})


# Link dependencies

target_link_libraries(
    ${RULESFORGE_ENGINE_TARGET}
    PUBLIC rfl_parser Salts::Core Threads::Threads Toad::Script
    PRIVATE CMeta::CSTL Salts::DataBind CMeta::ACE Salts::JsonParser
)
target_include_directories(
    ${RULESFORGE_ENGINE_TARGET}
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/core/parser/include

)
if(MSVC)
    target_compile_options(${RULESFORGE_ENGINE_TARGET} PUBLIC /bigobj)
endif()
# Disable debug/info logging in release builds for performance
target_compile_definitions(
    ${RULESFORGE_ENGINE_TARGET}
    PUBLIC $<$<CONFIG:Release>:RULEFORGE_ACTIVE_LOG_LEVEL=2>
           $<$<CONFIG:RelWithDebInfo>:RULEFORGE_ACTIVE_LOG_LEVEL=2>
           $<$<CONFIG:MinSizeRel>:RULEFORGE_ACTIVE_LOG_LEVEL=2>
)

set_target_properties(
    ${RULESFORGE_ENGINE_TARGET}
    PROPERTIES FOLDER "${TARGET_NAME}" LINKER_LANGUAGE CXX
)
# Windows: disable min/max macros that conflict with std::min/std::max
if(WIN32)
    target_compile_definitions(
        ${RULESFORGE_ENGINE_TARGET} PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN
    )
endif()

# Tests
if(BUILD_TESTS)
    add_subdirectory(test)
endif()
add_subdirectory(example)
if(BUILD_BENCHMARK)
    add_subdirectory(benchmark)
endif()
