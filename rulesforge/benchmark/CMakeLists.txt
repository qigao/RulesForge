# PROD-005: Production Load Testing Benchmarks Build with: cmake --build .
# --target production_benchmark.RulesForge Run with:
# ./production_benchmark.RulesForge.exe

set(folder_name ${TARGET_NAME}/benchmark)

option(
    RULESFORGE_ENABLE_BENCHMARK_TESTS
    "Register long-running benchmark executables with CTest"
    OFF
)

function(add_rulesforge_benchmark source)
    get_filename_component(benchmark_name ${source} NAME_WE)
    set(executable_name "${benchmark_name}.${TARGET_NAME}")

    add_executable(${executable_name} ${source})
    target_link_libraries(
        ${executable_name} PRIVATE TurboNet::TinyTest ${ARGN} ${TARGET_NAME}
    )

    set_target_properties(${executable_name} PROPERTIES FOLDER ${folder_name})

    if(BUILD_TESTING)
        add_test(NAME ${executable_name} COMMAND ${executable_name})
        set_tests_properties(
            ${executable_name} PROPERTIES LABELS "benchmark;long" TIMEOUT 600
        )
        if(NOT RULESFORGE_ENABLE_BENCHMARK_TESTS)
            set_tests_properties(${executable_name} PROPERTIES DISABLED TRUE)
        endif()
    endif()
endfunction()

add_rulesforge_benchmark(fact_rule_benchmark.cpp rfl_parser)
add_rulesforge_benchmark(agenda_benchmark.cpp)
add_rulesforge_benchmark(token_pool_benchmark.cpp)
