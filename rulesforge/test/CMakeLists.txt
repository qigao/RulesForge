file(GLOB_RECURSE TEST_SOURCES engine/*_test.cpp data/*_test.cpp api/*_test.cpp codec/*.cpp)

set(folder_name ${TARGET_NAME}/test)

foreach(test_source ${TEST_SOURCES})
    get_filename_component(test_name ${test_source} NAME_WE)

    set(executable_name "${test_name}.${TARGET_NAME}")
    add_executable(${executable_name} ${test_source})
    target_link_libraries(
        ${executable_name} PRIVATE TurboNet::TinyTest ${TARGET_NAME} data_bind
    )
    target_compile_definitions(
        ${executable_name}
        PRIVATE RFL_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test"
    )
    add_test(NAME ${executable_name} COMMAND ${executable_name})
    target_include_directories(
        ${executable_name}
        PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
               $<INSTALL_INTERFACE:include/rulesforge>
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                ${CMAKE_SOURCE_DIR}/tbe/data_bind
    )
    set_target_properties(${executable_name} PROPERTIES FOLDER ${folder_name})
endforeach()
