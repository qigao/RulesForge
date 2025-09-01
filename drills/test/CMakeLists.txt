file(GLOB TEST_SOURCES *_test.cpp)
set(folder_name ${TARGET_NAME}/test)
find_package(catch2 CONFIG REQUIRED)
foreach(test_source ${TEST_SOURCES})
    get_filename_component(test_name ${test_source} NAME_WE)

    set(executable_name "${test_name}.${TARGET_NAME}")
    add_executable(${executable_name} ${test_source})
    target_link_libraries(${executable_name} PRIVATE Catch2::Catch2WithMain ${TARGET_NAME} pubcxx taocpp::pegtl)
    add_test(NAME ${executable_name} COMMAND ${executable_name})

    set_target_properties(${executable_name} PROPERTIES FOLDER ${folder_name})
    # Removed add_run_target as it is an unknown CMake command and not a standard configuration.
endforeach()
