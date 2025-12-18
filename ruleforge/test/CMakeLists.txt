file(GLOB TEST_SOURCES *_test.cpp)

set(folder_name ${TARGET_NAME}/test)
find_package(catch2 CONFIG REQUIRED)
foreach(test_source ${TEST_SOURCES})
    get_filename_component(test_name ${test_source} NAME_WE)

    set(executable_name "${test_name}.${TARGET_NAME}")
    add_executable(${executable_name} ${test_source})
    target_link_libraries(
        ${executable_name} PRIVATE Catch2::Catch2WithMain ${TARGET_NAME} pubcxx
                                   taocpp::pegtl
    )
    add_test(NAME ${executable_name} COMMAND ${executable_name})

    set_target_properties(${executable_name} PROPERTIES FOLDER ${folder_name})
endforeach()

# Copy test RFL files at build time (not configure time)
file(GLOB TEST_RFL_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.rfl")
add_custom_target(
    copy_test_rfl_files ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${TEST_RFL_FILES}
            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    COMMAND
        ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/test"
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/test"
    COMMENT "Copying test RFL files to output directory"
)
