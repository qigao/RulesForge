set(TARGET_NAME RulesForge)

# Collect the internal C++ engine sources (excluding the separate parser).
file(GLOB_RECURSE SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h*
     ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c*
)

# Compile the engine once for the shared C ABI and the in-tree C++ tests without
# producing a standalone C++ static library.
add_library(${TARGET_NAME} OBJECT ${SOURCES})


# Link dependencies

target_link_libraries(
    ${TARGET_NAME}
    PUBLIC Salts::Core Threads::Threads
    PRIVATE Salts::CSTL Salts::DataBind
)
target_include_directories(
    ${TARGET_NAME} PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                          $<INSTALL_INTERFACE:include/rulesforge>
                   PRIVATE ${CMAKE_SOURCE_DIR}/parser/include

)
if(MSVC)
    target_compile_options(${TARGET_NAME} PUBLIC /bigobj)
endif()
# Disable debug/info logging in release builds for performance
target_compile_definitions(
    ${TARGET_NAME}
    PUBLIC $<$<CONFIG:Release>:RULEFORGE_ACTIVE_LOG_LEVEL=2>
           $<$<CONFIG:RelWithDebInfo>:RULEFORGE_ACTIVE_LOG_LEVEL=2>
           $<$<CONFIG:MinSizeRel>:RULEFORGE_ACTIVE_LOG_LEVEL=2>
)

set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "${TARGET_NAME}")
set_target_properties(${TARGET_NAME} PROPERTIES LINKER_LANGUAGE CXX)
# Windows: disable min/max macros that conflict with std::min/std::max
if(WIN32)
    target_compile_definitions(
        ${TARGET_NAME} PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN
    )
endif()

# Tests
add_subdirectory(test)
add_subdirectory(example)
add_subdirectory(benchmark)
