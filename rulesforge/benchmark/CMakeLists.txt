# PROD-005: Production Load Testing Benchmarks Build with: cmake --build .
# --target production_benchmark.RulesForge Run with:
# ./production_benchmark.RulesForge.exe

set(folder_name ${TARGET_NAME}/benchmark)


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

 