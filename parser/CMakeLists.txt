# =============================================================================
# RFL Parser Static Library
# =============================================================================
# This library contains the RulesForge DSL parser (lexer + grammar + impl) It
# can be used by both RulesForge engine and external tools.
set(TARGET_NAME rfl_parser)

# --- re2c + Lemon code generation ---
set(PARSER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/parser)
set(PARSER_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/parser_gen)
file(MAKE_DIRECTORY ${PARSER_GEN_DIR})

# re2c: .re -> .cpp
add_custom_command(
    OUTPUT ${PARSER_GEN_DIR}/rfl_lexer.cpp
    COMMAND ${RE2C_EXECUTABLE} -W --no-debug-info -o
            ${PARSER_GEN_DIR}/rfl_lexer.cpp ${PARSER_DIR}/rfl_lexer.re
    DEPENDS ${PARSER_DIR}/rfl_lexer.re
    COMMENT "Generating RFL lexer with re2c"
)

# Lemon: .y -> .c + .h
add_custom_command(
    OUTPUT ${PARSER_GEN_DIR}/rfl_grammar_lemon.c
           ${PARSER_GEN_DIR}/rfl_grammar_lemon.h
    COMMAND ${CMAKE_COMMAND} -E copy ${PARSER_DIR}/rfl_grammar_lemon.y
            ${PARSER_GEN_DIR}/rfl_grammar_lemon.y
    COMMAND ${LEMON_EXECUTABLE} -T${LEMPAR}
            ${PARSER_GEN_DIR}/rfl_grammar_lemon.y
    DEPENDS ${PARSER_DIR}/rfl_grammar_lemon.y ${LEMON_DEPENDS}
    COMMENT "Generating RFL parser with Lemon"
)

# Compile Lemon-generated C as C++
set_source_files_properties(
    ${PARSER_GEN_DIR}/rfl_grammar_lemon.c PROPERTIES LANGUAGE CXX
)
file(GLOB_RECURSE SOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c*
     ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h*
)
# Parser library sources (only lexer + grammar + impl, no high-level API)
set(PARSER_SOURCES
    ${PARSER_GEN_DIR}/rfl_lexer.cpp
    ${PARSER_GEN_DIR}/rfl_grammar_lemon.c
    ${CMAKE_SOURCE_DIR}/rulesforge/src/core/rfl_rete_defs.cpp # Type
                                                              # implementations
    ${SOURCE_FILES} # needed by parser
)

add_library(${TARGET_NAME} STATIC ${PARSER_SOURCES})

target_include_directories(
    ${TARGET_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> # parser/include/
                                                               # (for
                                                               # parser/*.hpp)
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/rulesforge/include> # rulesforge/include/
                                                                  # (for core/*)
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/rulesforge/src> # rulesforge/src/
                                                              # (for core/*.cpp)
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} # For rfl_lexer.hpp, rfl_parser_impl.hpp
            ${CMAKE_CURRENT_BINARY_DIR}/parser_gen
)

target_link_libraries(
    ${TARGET_NAME}
    PUBLIC Salts::Core
    PRIVATE Salts::CSTL Salts::CsvParser DataBind::Runtime
)

if(MSVC)
    target_compile_options(${TARGET_NAME} PRIVATE /bigobj)
    target_compile_definitions(
        ${TARGET_NAME} PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN
    )
endif()

set_target_properties(
    ${TARGET_NAME} PROPERTIES FOLDER "parser" LINKER_LANGUAGE CXX
)

# Export for use by other targets
add_library(RulesForge::Parser ALIAS ${TARGET_NAME})

# Tests
if(BUILD_TESTING OR ENABLE_TESTS)
    add_subdirectory(tests)
endif()
