file(GLOB_RECURSE TEST_SOURCES engine/*_test.cpp data/*_test.cpp core/*_test.cpp api/*_test.cpp codec/*.cpp)

set(folder_name ${TARGET_NAME}/test)

foreach(test_source ${TEST_SOURCES})
    get_filename_component(test_name ${test_source} NAME_WE)

    set(executable_name "${test_name}.${TARGET_NAME}")
    add_executable(${executable_name} ${test_source})
    target_link_libraries(
        ${executable_name}
        PRIVATE TurboUtils::TinyTest rfl_parser ${TARGET_NAME} Threads::Threads
    )
    target_compile_definitions(
        ${executable_name}
        PRIVATE RFL_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test"
    )
    if(test_name STREQUAL "fraud_example_test")
        target_compile_definitions(
            ${executable_name}
            PRIVATE RULESFORGE_FRAUD_EXAMPLE_RFL="${CMAKE_SOURCE_DIR}/docs/examples/fraud-detection/fraud-detection.rfl"
        )
    endif()
    add_test(NAME ${executable_name} COMMAND ${executable_name})
    target_include_directories(
        ${executable_name}
        PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
               $<INSTALL_INTERFACE:include/rulesforge>
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    )
    set_target_properties(${executable_name} PROPERTIES FOLDER ${folder_name})
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
