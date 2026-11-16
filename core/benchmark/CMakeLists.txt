# PROD-005: Production Load Testing Benchmarks Build with: cmake --build .
# --target production_benchmark.RulesForge Run with:
# ./production_benchmark.RulesForge.exe

set(folder_name ${TARGET_NAME}/benchmark)

function(add_rulesforge_benchmark source)
    get_filename_component(benchmark_name ${source} NAME_WE)
    set(executable_name "${benchmark_name}.${TARGET_NAME}")

    add_executable(${executable_name} ${source})
    target_link_libraries(
        ${executable_name}
        PRIVATE Salts::TinyTest Salts::SIMD rfl_parser ${RULESFORGE_ENGINE_TARGET}
    )

    set_target_properties(${executable_name} PROPERTIES FOLDER ${folder_name})

    add_test(NAME ${executable_name} COMMAND ${executable_name})
    set_tests_properties(${executable_name} PROPERTIES LABELS "benchmark;long" TIMEOUT 600)

endfunction()

add_rulesforge_benchmark(fact_rule_benchmark.cpp)
add_rulesforge_benchmark(agenda_benchmark.cpp)
add_rulesforge_benchmark(token_pool_benchmark.cpp)
add_rulesforge_benchmark(string_map_benchmark.cpp)
add_rulesforge_benchmark(text_compare_benchmark.cpp)
add_rulesforge_benchmark(scalar_program_benchmark.cpp)
add_rulesforge_benchmark(continuous_session_benchmark.cpp)
