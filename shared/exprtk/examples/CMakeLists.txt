# exprtk examples

set(EXPRTK_EXAMPLES
    exprtk_simple_example_01
    exprtk_simple_example_02
    exprtk_simple_example_03
    exprtk_simple_example_04
    exprtk_simple_example_05
    exprtk_simple_example_06
    exprtk_simple_example_07
    exprtk_simple_example_08
    exprtk_simple_example_09
    exprtk_simple_example_10
    exprtk_simple_example_11
    exprtk_simple_example_12
    exprtk_simple_example_13
    exprtk_simple_example_14
    exprtk_simple_example_15
    exprtk_simple_example_16
    exprtk_simple_example_17
    exprtk_simple_example_18
    exprtk_simple_example_19
    exprtk_simple_example_20
    exprtk_simple_example_21
    exprtk_simple_example_22
    exprtk_simple_example_23
    exprtk_simple_example_24
    exprtk_benchmark
)

foreach(example IN LISTS EXPRTK_EXAMPLES)
    add_executable(${example} ${example}.cpp)
    target_link_libraries(${example} PRIVATE exprtk::exprtk)
    set_target_properties(${example} PROPERTIES FOLDER "shared/exprtk/examples")
    # MSVC: exprtk_test.cpp is huge, needs /bigobj
    if(MSVC)
        target_compile_options(${example} PUBLIC /bigobj)
    endif()
endforeach()
