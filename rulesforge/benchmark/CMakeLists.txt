# PROD-005: Production Load Testing Benchmarks Build with: cmake --build .
# --target production_benchmark.RulesForge Run with:
# ./production_benchmark.RulesForge.exe

set(folder_name ${TARGET_NAME}/benchmark)

# Production benchmark executable
add_executable(production_benchmark.${TARGET_NAME} production_benchmark.cpp)
target_link_libraries(
    production_benchmark.${TARGET_NAME} PRIVATE TurboNet::TinyTest
                                                ${TARGET_NAME}
)

set_target_properties(
    production_benchmark.${TARGET_NAME} PROPERTIES FOLDER ${folder_name}
)

# Add as a test (but exclude from normal test runs due to duration) Run
# explicitly with: ctest -R production_benchmark
add_test(NAME production_benchmark.${TARGET_NAME}
         COMMAND production_benchmark.${TARGET_NAME}
)

# Mark as a long-running test
set_tests_properties(
    production_benchmark.${TARGET_NAME} PROPERTIES LABELS "benchmark;long"
                                                   TIMEOUT 600 # 10 minutes max
)

# Fact/Rule interaction benchmark executable
add_executable(fact_rule_benchmark.${TARGET_NAME} fact_rule_benchmark.cpp)
target_link_libraries(
    fact_rule_benchmark.${TARGET_NAME} PRIVATE TurboNet::TinyTest
                                               ${TARGET_NAME}
)

set_target_properties(
    fact_rule_benchmark.${TARGET_NAME} PROPERTIES FOLDER ${folder_name}
)

add_test(NAME fact_rule_benchmark.${TARGET_NAME}
         COMMAND fact_rule_benchmark.${TARGET_NAME}
)

set_tests_properties(
    fact_rule_benchmark.${TARGET_NAME} PROPERTIES LABELS "benchmark;long"
                                                  TIMEOUT 600
)

# RETE profiling benchmark executable (diagnostic-only)
add_executable(rete_profile_benchmark.${TARGET_NAME} rete_profile_benchmark.cpp)
target_link_libraries(
    rete_profile_benchmark.${TARGET_NAME} PRIVATE TurboNet::TinyTest
                                                  ${TARGET_NAME}
)

set_target_properties(
    rete_profile_benchmark.${TARGET_NAME} PROPERTIES FOLDER ${folder_name}
)

add_test(NAME rete_profile_benchmark.${TARGET_NAME}
         COMMAND rete_profile_benchmark.${TARGET_NAME}
)

set_tests_properties(
    rete_profile_benchmark.${TARGET_NAME}
    PROPERTIES LABELS "benchmark;diagnostic" TIMEOUT 300
)
