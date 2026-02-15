file(GLOB SOURCES *.cpp)
foreach(example_source ${SOURCES})
    get_filename_component(source_name ${example_source} NAME_WE)
    set(EXAMPLE_NAME ${source_name})
    add_executable(${EXAMPLE_NAME} ${example_source})
    target_link_libraries(
        ${EXAMPLE_NAME}
        PRIVATE ${TARGET_NAME}
                pubcxx
    )
    target_include_directories(${EXAMPLE_NAME} PRIVATE ../include)

    set_target_properties(
        ${EXAMPLE_NAME} PROPERTIES FOLDER ${TARGET_NAME}/example
    )

endforeach()
configure_file(valid_rules.rfl ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/valid_rules.rfl COPYONLY)
configure_file(invalid_rules.rfl ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/invalid_rules.rfl COPYONLY)
