set(TARGET_NAME RulesForge)

# Collect RulesForge sources (excluding parser which is now separate)
file(GLOB_RECURSE SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h*
     ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c*
)

add_library(${TARGET_NAME} STATIC ${SOURCES})
# Find dependencies
find_package(xxHash CONFIG REQUIRED)
find_package(jsoncons CONFIG REQUIRED)

# Link dependencies

target_link_libraries(
    ${TARGET_NAME}
    PUBLIC xxHash::xxhash TurboNet::Utils rfl_parser
    PRIVATE jsoncons::jsoncons data_bind
)
target_include_directories(
    ${TARGET_NAME} PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                          $<INSTALL_INTERFACE:include/rulesforge>
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

# --- Install rules ---

install(
    DIRECTORY include/
    DESTINATION include/rulesforge
    FILES_MATCHING
    PATTERN "*.hpp"
    PATTERN "*.h"
)

# Tests
add_subdirectory(test)
add_subdirectory(example)
add_subdirectory(benchmark)
