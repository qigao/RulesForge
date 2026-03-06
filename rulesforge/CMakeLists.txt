set(TARGET_NAME RulesForge)

file(GLOB_RECURSE SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h*
     ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c*
     ${CMAKE_CURRENT_SOURCE_DIR}/src/rete/*.c*
)

# --- re2c + Lemon code generation (always on) ---
set(PARSER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/parser)
set(PARSER_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/parser_gen)
file(MAKE_DIRECTORY ${PARSER_GEN_DIR})

# re2c: .re -> .cpp
add_custom_command(
    OUTPUT ${PARSER_GEN_DIR}/rfl_lexer.cpp
    COMMAND ${RE2C_EXECUTABLE} -W --no-debug-info -o
            ${PARSER_GEN_DIR}/rfl_lexer.cpp ${PARSER_DIR}/rfl_lexer.re
    DEPENDS ${PARSER_DIR}/rfl_lexer.re
    COMMENT "Generating lexer with re2c"
)

# Lemon: .y -> .c + .h (lemon outputs next to input, so copy .y to gen dir
# first)
add_custom_command(
    OUTPUT ${PARSER_GEN_DIR}/rfl_grammar_lemon.c
           ${PARSER_GEN_DIR}/rfl_grammar_lemon.h
    COMMAND ${CMAKE_COMMAND} -E copy ${PARSER_DIR}/rfl_grammar_lemon.y
            ${PARSER_GEN_DIR}/rfl_grammar_lemon.y
    COMMAND ${LEMON_EXECUTABLE} -T${LEMPAR}
            ${PARSER_GEN_DIR}/rfl_grammar_lemon.y
    DEPENDS ${PARSER_DIR}/rfl_grammar_lemon.y ${LEMON_DEPENDS}
    COMMENT "Generating parser with Lemon"
)

list(APPEND SOURCES ${PARSER_GEN_DIR}/rfl_lexer.cpp
     ${PARSER_GEN_DIR}/rfl_grammar_lemon.c ${PARSER_DIR}/rfl_parser_impl.cpp
)
# Compile Lemon-generated C as C++ so reduction actions can use C++ types
set_source_files_properties(
    ${PARSER_GEN_DIR}/rfl_grammar_lemon.c PROPERTIES LANGUAGE CXX
)

add_library(${TARGET_NAME} STATIC ${SOURCES})
# Find dependencies
find_package(xxHash CONFIG REQUIRED)
find_package(jsoncons CONFIG REQUIRED)

# Link dependencies
set(RULEFORGE_PUBLIC_LINK_LIBS jsoncons::jsoncons xxHash::xxhash
                               TurboNet::Utils TurboNet::Parser
)

target_link_libraries(${TARGET_NAME} PUBLIC ${RULEFORGE_PUBLIC_LINK_LIBS})
target_include_directories(
    ${TARGET_NAME}
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
           $<INSTALL_INTERFACE:include/rulesforge>
    PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/parser
            ${CMAKE_CURRENT_BINARY_DIR}/parser_gen
)
target_compile_definitions(${TARGET_NAME} PRIVATE RULEFORGE_USE_LEMON)
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
    TARGETS ${TARGET_NAME}
    EXPORT RulesForgeTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

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
