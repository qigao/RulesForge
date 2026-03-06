file(GLOB_RECURSE TEST_SOURCES parser/*_test.cpp engine/*_test.cpp)

set(folder_name ${TARGET_NAME}/test)

foreach(test_source ${TEST_SOURCES})
    get_filename_component(test_name ${test_source} NAME_WE)

    set(executable_name "${test_name}.${TARGET_NAME}")
    add_executable(${executable_name} ${test_source})
    target_link_libraries(
        ${executable_name} PRIVATE TurboNet::TinyTest ${TARGET_NAME}
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
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/parser
                ${CMAKE_CURRENT_BINARY_DIR}/parser_gen
                ${CMAKE_CURRENT_SOURCE_DIR}
    )
    set_target_properties(${executable_name} PROPERTIES FOLDER ${folder_name})
endforeach()
