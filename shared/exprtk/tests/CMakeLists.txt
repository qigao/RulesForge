# exprtk tests

add_executable(exprtk_test exprtk_test.cpp)
target_link_libraries(exprtk_test PRIVATE exprtk::exprtk)
set_target_properties(exprtk_test PROPERTIES FOLDER "shared/exprtk/tests")

# Copy test data files
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/exprtk_functional_test.txt
    ${CMAKE_CURRENT_BINARY_DIR}/exprtk_functional_test.txt COPYONLY
)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/exprtk_functional_ext_test.txt
    ${CMAKE_CURRENT_BINARY_DIR}/exprtk_functional_ext_test.txt COPYONLY
)
# MSVC: exprtk_test.cpp is huge, needs /bigobj
if(MSVC)
    target_compile_options(exprtk_test PUBLIC /bigobj)
endif()
# Register test
add_test(NAME exprtk_test COMMAND exprtk_test)
set_tests_properties(
    exprtk_test PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
