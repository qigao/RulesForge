# =============================================================================
# RFL Parser Static Library
# =============================================================================
# This library contains the RulesForge DSL rulesforge/parser (lexer + grammar + impl) It
# can be used by both RulesForge engine and external tools.
set(TARGET_NAME rfl_parser)

# --- re2c + Lemon code generation ---
set(PARSER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/parser)
set(PARSER_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/parser_gen)
file(MAKE_DIRECTORY ${PARSER_GEN_DIR})

foreach(lexer rfl_lexer rhs_lexer native_words scalar_lexer)
add_custom_command(
    OUTPUT ${PARSER_GEN_DIR}/${lexer}.cpp
    COMMAND ${RE2C_EXECUTABLE} -W --no-debug-info -o
            ${PARSER_GEN_DIR}/${lexer}.cpp ${PARSER_DIR}/${lexer}.re
    DEPENDS ${PARSER_DIR}/${lexer}.re ${RE2C_EXECUTABLE}
    COMMENT "Generating ${lexer} with re2c"
    VERBATIM
)
endforeach()

# Lemon: .y -> .c + .h
add_custom_command(
    OUTPUT ${PARSER_GEN_DIR}/rfl_grammar_lemon.c
           ${PARSER_GEN_DIR}/rfl_grammar_lemon.h
    COMMAND ${CMAKE_COMMAND} -E copy ${PARSER_DIR}/rfl_grammar_lemon.y
            ${PARSER_GEN_DIR}/rfl_grammar_lemon.y
    COMMAND ${LEMON_EXECUTABLE} -T${LEMPAR}
            ${PARSER_GEN_DIR}/rfl_grammar_lemon.y
    DEPENDS ${PARSER_DIR}/rfl_grammar_lemon.y ${LEMON_DEPENDS} ${LEMPAR}
    COMMENT "Generating RFL rulesforge/parser with Lemon"
)

# Compile Lemon-generated C as C++
set_source_files_properties(
    ${PARSER_GEN_DIR}/rfl_grammar_lemon.c PROPERTIES LANGUAGE CXX
)
file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c*
     ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h*
)
file(GLOB MODEL_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/core/src/model/*.cpp)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCE_FILES})
source_group(TREE ${CMAKE_SOURCE_DIR}/core/src/model PREFIX model FILES ${MODEL_SOURCES})
# Parser library sources (only lexer + grammar + impl, no high-level API)
set(PARSER_SOURCES
    ${PARSER_GEN_DIR}/rfl_lexer.cpp
    ${PARSER_GEN_DIR}/rhs_lexer.cpp
    ${PARSER_GEN_DIR}/native_words.cpp
    ${PARSER_GEN_DIR}/scalar_lexer.cpp
    ${PARSER_GEN_DIR}/rfl_grammar_lemon.c
    ${MODEL_SOURCES}
    ${SOURCE_FILES} # needed by rulesforge/parser
)

add_library(${TARGET_NAME} STATIC ${PARSER_SOURCES})
target_link_libraries(${TARGET_NAME} PUBLIC Toad::Script)

target_include_directories(
    ${TARGET_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/core/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_BINARY_DIR}/parser_gen
)

target_link_libraries(
    ${TARGET_NAME}
    PUBLIC Salts::Core
    PRIVATE CMeta::CSTL Salts::CsvParser Salts::DataBind MIR::MIR
)

if(MSVC)
    target_compile_options(${TARGET_NAME} PRIVATE /bigobj)
    target_compile_definitions(
        ${TARGET_NAME} PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN
    )
endif()

set_target_properties(
    ${TARGET_NAME} PROPERTIES FOLDER "rulesforge/parser" LINKER_LANGUAGE CXX
)

# Export for use by other targets
add_library(RulesForge::Parser ALIAS ${TARGET_NAME})

# Tests
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
