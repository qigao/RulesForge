# Parser Tests
# Each .cpp in this directory becomes its own test executable.
# All tests link only against rfl_parser (no engine/session dependency).

file(GLOB PARSER_TEST_SOURCES *.cpp)
set(folder_name parser/tests)

foreach(test_source ${PARSER_TEST_SOURCES})
    get_filename_component(test_name ${test_source} NAME_WE)
    set(executable_name "${test_name}.parser")
    add_executable(${executable_name} ${test_source})
    target_link_libraries(${executable_name} PRIVATE TurboUtils::TinyTest rfl_parser)
    target_compile_definitions(${executable_name} PRIVATE RFL_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/rulesforge/test")
    add_test(NAME ${executable_name} COMMAND ${executable_name})
    target_include_directories(${executable_name} PRIVATE
        # Core domain types (parser_state, parsed_rule, errors, etc.)
        ${CMAKE_SOURCE_DIR}/rulesforge/include
        ${CMAKE_SOURCE_DIR}/rulesforge/src
        # Internal parser headers: rfl_parser_impl.hpp, ast_transformer.hpp,
        # semantic_analyzer.hpp, etc.  These are in parser/ itself (not include/).
        ${CMAKE_SOURCE_DIR}/parser
        # Public parser API headers (rfl_parser.hpp, rfl_lexer.hpp, …)
        ${CMAKE_SOURCE_DIR}/parser/include
    )
    set_target_properties(${executable_name} PROPERTIES
        FOLDER ${folder_name}
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
    )
    if(WIN32)
        add_custom_command(
            TARGET ${executable_name}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_RUNTIME_DLLS:${executable_name}>
                    $<TARGET_FILE_DIR:${executable_name}>
            COMMAND_EXPAND_LISTS
        )
    endif()
endforeach()
