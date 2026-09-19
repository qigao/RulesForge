set(TARGET_NAME RulesForge)

# Collect the internal C++ engine sources (excluding the separate parser).
file(GLOB_RECURSE SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h*
     ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c*
)

# Compile the engine once for the shared C ABI and the in-tree C++ tests without
# producing a standalone C++ static library.
add_library(${RULESFORGE_ENGINE_TARGET} OBJECT ${SOURCES})


# Link dependencies

target_link_libraries(
    ${RULESFORGE_ENGINE_TARGET}
    PUBLIC Salts::Core Threads::Threads
    PRIVATE Salts::CSTL DataBind::Runtime
)
target_include_directories(
    ${RULESFORGE_ENGINE_TARGET}
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
           $<INSTALL_INTERFACE:include/rulesforge>
    PRIVATE ${CMAKE_SOURCE_DIR}/parser/include

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
add_subdirectory(test)
add_subdirectory(example)
add_subdirectory(benchmark)
