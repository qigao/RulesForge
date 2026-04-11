add_executable(rulesforge_compiler
    compiler_core.cpp
    main.cpp
    template_model.cpp
)

target_link_libraries(rulesforge_compiler PRIVATE
    rfl_parser
    RulesForge
    data_bind
    TurboNet::Mustache
)

target_include_directories(rulesforge_compiler PRIVATE
    ${CMAKE_SOURCE_DIR}/parser/include
    ${CMAKE_SOURCE_DIR}/rulesforge/include
    ${CMAKE_SOURCE_DIR}/tbe/data_bind
)

set_target_properties(rulesforge_compiler PROPERTIES
    FOLDER tools
)

add_custom_command(
    TARGET rulesforge_compiler
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/templates
            $<TARGET_FILE_DIR:rulesforge_compiler>/templates
    COMMENT "Copying RulesForge compiler templates"
)

add_executable(test_rulesforge_compiler
    compiler_core.cpp
    template_model.cpp
    test_rulesforge_compiler.cpp
)

target_link_libraries(test_rulesforge_compiler PRIVATE
    rfl_parser
    RulesForge
    data_bind
    TurboNet::TinyTest
    TurboNet::Mustache
)

target_include_directories(test_rulesforge_compiler PRIVATE
    ${CMAKE_SOURCE_DIR}/parser/include
    ${CMAKE_SOURCE_DIR}/rulesforge/include
    ${CMAKE_SOURCE_DIR}/tbe/data_bind
)

target_compile_definitions(test_rulesforge_compiler PRIVATE
    RULESFORGE_COMPILER_TEMPLATE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/templates"
)

set_target_properties(test_rulesforge_compiler PROPERTIES
    FOLDER tools
)

add_test(NAME test_rulesforge_compiler COMMAND test_rulesforge_compiler)
set_tests_properties(
    test_rulesforge_compiler
    PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0;LSAN_OPTIONS=detect_leaks=0"
)
