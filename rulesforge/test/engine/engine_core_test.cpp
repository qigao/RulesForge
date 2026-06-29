#include "rfl_parser.hpp"
#include "engine/knowledge_base.hpp"
#include "engine/rhs_executor.hpp"
#include "engine/stateful_session.hpp"
#include "engine/turboscript_rhs_adapter.hpp"
#include "rete/rete_node_query.hpp"
#include "expression_descriptor.hpp"
#include "test_helpers.hpp"
#include "tinytest.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>

std::unique_ptr<StatefulSession> build_session(std::string const& drl) {
    ParsingResult result;
    auto kb = build_knowledge_base(drl, result);
    if (!result.success) throw_parse_failure(result);
    if (!kb) { throw std::runtime_error("KnowledgeBase is null"); }
    auto session = kb->create_session();
    if (!session) { throw std::runtime_error("Session is null"); }
    return session;
}

namespace {
std::unique_ptr<rulesforge::MirExecutionPlan>
fail_mir_execution_plan_compile(std::vector<ParsedRule> const&,
                                std::vector<ParsedQuery> const&,
                                std::vector<ParsedDeclaration> const&,
                                std::string* error_out) {
    if (error_out) {
        *error_out = "test injected MIR compile failure";
    }
    return nullptr;
}

std::unique_ptr<rulesforge::MirExecutionPlan>
compile_mismatched_rule_count_mir_plan(std::vector<ParsedRule> const&,
                                       std::vector<ParsedQuery> const& queries,
                                       std::vector<ParsedDeclaration> const& declarations,
                                       std::string* error_out) {
    std::vector<ParsedRule> empty_rules;
    return rulesforge::MirExecutionPlan::compile(empty_rules, queries, declarations, error_out);
}

std::unique_ptr<rulesforge::MirExecutionPlan>
compile_mismatched_metadata_mir_plan(std::vector<ParsedRule> const& rules,
                                     std::vector<ParsedQuery> const& queries,
                                     std::vector<ParsedDeclaration> const& declarations,
                                     std::string* error_out) {
    auto mutated_rules = rules;
    if (!mutated_rules.empty()) {
        mutated_rules.front().salience += 1;
        mutated_rules.front().enabled = !mutated_rules.front().enabled;
    }
    return rulesforge::MirExecutionPlan::compile(mutated_rules, queries, declarations, error_out);
}

bool fail_mir_compare_self_check(rulesforge::MirExecutionPlan const&,
                                 std::vector<ParsedRule> const&,
                                 std::string* error_out) {
    if (error_out) {
        *error_out = "test injected MIR compare self-check failure";
    }
    return false;
}

char* duplicate_c_string(char const* value) {
    std::size_t const size = std::strlen(value) + 1;
    auto* result = static_cast<char*>(std::malloc(size));
    if (result == nullptr) {
        return nullptr;
    }
    std::memcpy(result, value, size);
    return result;
}

int native_predicate_equals_first_arg(void* ctx, int argc, char const** argv, char** out_result) {
    if (out_result == nullptr) {
        return 1;
    }
    auto const* expected = static_cast<char const*>(ctx);
    bool const matches = argc > 0 && argv != nullptr && argv[0] != nullptr
        && expected != nullptr && std::string(argv[0]) == expected;
    *out_result = duplicate_c_string(matches ? "true" : "false");
    return *out_result == nullptr ? 1 : 0;
}

int native_predicate_equals_two_args(void*, int argc, char const** argv, char** out_result) {
    if (out_result == nullptr) {
        return 1;
    }
    bool const matches = argc == 2
        && argv != nullptr
        && argv[0] != nullptr
        && argv[1] != nullptr
        && std::string(argv[0]) == argv[1];
    *out_result = duplicate_c_string(matches ? "true" : "false");
    return *out_result == nullptr ? 1 : 0;
}

int native_predicate_first_arg_is_raw_gold(void*, int argc, char const** argv, char** out_result) {
    if (out_result == nullptr) {
        return 1;
    }
    bool const matches = argc > 0
        && argv != nullptr
        && argv[0] != nullptr
        && std::string(argv[0]) == "gold";
    *out_result = duplicate_c_string(matches ? "true" : "false");
    return *out_result == nullptr ? 1 : 0;
}

int native_predicate_numeric_equals_two_args(void*, int argc, char const** argv, char** out_result) {
    if (out_result == nullptr) {
        return 1;
    }
    bool matches = false;
    if (argc == 2 && argv != nullptr && argv[0] != nullptr && argv[1] != nullptr) {
        char* lhs_end = nullptr;
        char* rhs_end = nullptr;
        double const lhs = std::strtod(argv[0], &lhs_end);
        double const rhs = std::strtod(argv[1], &rhs_end);
        matches = lhs_end != argv[0]
            && rhs_end != argv[1]
            && lhs_end != nullptr
            && rhs_end != nullptr
            && *lhs_end == '\0'
            && *rhs_end == '\0'
            && std::abs(lhs - rhs) < 0.000001;
    }
    *out_result = duplicate_c_string(matches ? "true" : "false");
    return *out_result == nullptr ? 1 : 0;
}

struct ScopedMirExecutionPlanCompiler {
    explicit ScopedMirExecutionPlanCompiler(KnowledgeBase::MirExecutionPlanCompiler compiler)
        : previous(KnowledgeBase::set_mir_execution_plan_compiler_for_testing(compiler)) {}

    ~ScopedMirExecutionPlanCompiler() {
        KnowledgeBase::set_mir_execution_plan_compiler_for_testing(previous);
    }

    KnowledgeBase::MirExecutionPlanCompiler previous;
};

struct ScopedMirExecutionPlanSelfCheck {
    explicit ScopedMirExecutionPlanSelfCheck(KnowledgeBase::MirExecutionPlanSelfCheck self_check)
        : previous(KnowledgeBase::set_mir_execution_plan_self_check_for_testing(self_check)) {}

    ~ScopedMirExecutionPlanSelfCheck() {
        KnowledgeBase::set_mir_execution_plan_self_check_for_testing(previous);
    }

    KnowledgeBase::MirExecutionPlanSelfCheck previous;
};

void check_build_fails_with(std::string const& drl, std::string const& expected_message) {
    ParsingResult result;
    auto kb = build_knowledge_base(drl, result);
    check(kb == nullptr);
    check(!result.success);
    check(!result.errors.empty());
    if (!result.errors.empty()) {
        check(result.errors.front().message.find(expected_message) != std::string::npos);
    }
}

std::optional<bool> mir_runtime_compare(KnowledgeBase const& kb,
                                        CompareOp op,
                                        ConstraintValue lhs,
                                        ConstraintValue rhs) {
    auto predicate_id = kb.mir_compare_predicate_id(op);
    if (!predicate_id) {
        return std::nullopt;
    }
    return kb.mir_runtime_predicate(
        rulesforge::MirRuntimePredicateRef{rulesforge::MirRuntimePredicateKind::Compare, *predicate_id},
        {std::move(lhs), std::move(rhs)});
}

std::optional<bool> mir_runtime_unary(KnowledgeBase const& kb,
                                      rulesforge::MirRuntimePredicateKind kind,
                                      std::size_t predicate_id,
                                      ConstraintValue value) {
    return kb.mir_runtime_predicate(
        rulesforge::MirRuntimePredicateRef{kind, predicate_id},
        {std::move(value)});
}
} // namespace

suite("RFL Engine") {
    group("TurboScript schema import") {
        it("builds rule-visible declarations from TurboScript schema") {
            auto schema_path = std::filesystem::temp_directory_path()
                / "rulesforge_schema_import_engine_core.schema";
            {
                std::ofstream schema(schema_path, std::ios::binary);
                schema << "schema Market [id(7), version(1), byte_order(little)]; "
                          "enum Tier <uint8> { Bronze = 1; Silver = 2; } "
                          "message Customer { int32 age; Tier tier; list<string> tags; "
                          "map<string,int32> scores; string name; }";
            }

            std::string const drl = std::string("import \"")
                + schema_path.generic_string()
                + R"(";
                    rule "adult customer"
                    when
                        $c : Customer(age >= 18, name == "Ada")
                    then
                    end
                )";

            ParsingResult result;
            auto kb = build_knowledge_base(drl, result, "schema_import_test.rfl");
            if (!result.success) {
                throw_parse_failure(result);
            }

            check(kb != nullptr);
            auto const& declarations = kb->get_parser_state().parsed_declarations;
            auto it = std::find_if(declarations.begin(), declarations.end(),
                                   [](ParsedDeclaration const& decl) {
                                       return decl.type_name == "Customer";
                                   });
            check(it != declarations.end());
            if (it != declarations.end()) {
                check(it->fields.size() == 5);
                check(it->fields[0].name == "age");
                check(it->fields[0].type == FT_Int);
                check(it->fields[1].name == "tier");
                check(it->fields[1].type == FT_Int);
                check(it->fields[2].name == "tags");
                check(it->fields[2].type == FT_List);
                check(it->fields[3].name == "scores");
                check(it->fields[3].type == FT_Map);
                check(it->fields[4].name == "name");
                check(it->fields[4].type == FT_String);
            }
            check(kb->get_parser_state().parsed_enums.size() == 1);
        }

    }

    group("Enums") {
        it("uses bare enum symbols as string-compatible rule values") {
            auto session = build_session(R"(
                enum Currency
                    CNY
                    USD
                end

                declare Price
                    currency: Currency
                end

                declare Result
                    currency: Currency
                end

                rule "Seed CNY"
                when
                    not Price(currency == CNY)
                then
                    insert Price { currency = CNY }
                end

                rule "Copy CNY"
                when
                    $price : Price(currency == CNY)
                    not Result(currency == CNY)
                then
                    insert Result { currency = $price.currency }
                end

                query "Results"
                    $result : Result()
                end
            )");

            check(session->fire_all_rules() == 2);
            auto results = session->execute_query("Results");
            check(results.size() == 1);
            if (!results.empty()) {
                auto currency = results.single().getFieldAs<std::string>("$result", "currency");
                check(currency.has_value());
                check(*currency == "CNY");
            }
        }
    }

    group("MIR execution plan") {
        it("reports MIR unavailable lowering error for an empty knowledge base") {
            auto kb = KnowledgeBase::create_empty();
            check(kb != nullptr);
            check(!kb->has_mir_execution_plan());

            auto summary = kb->mir_summary();
            check(!summary.available);
            check(summary.rule_count == 0);
            check(summary.query_count == 0);
            check(summary.rule_coverage_count == 0);
            check(summary.query_coverage_count == 0);
            check(summary.rule_graph_count == 0);
            check(summary.rule_graph_predicate_count == 0);
            check(summary.query_graph_count == 0);
            check(summary.query_graph_predicate_count == 0);

            auto summary_text = kb->mir_summary_text();
            check(summary_text.find("available=false") != std::string::npos);
            check(summary_text.find("string_contains_support=false") != std::string::npos);
            check(summary_text.find("string_matches_support=false") != std::string::npos);
            check(summary_text.find("string_affix_support=false") != std::string::npos);
            check(summary_text.find("string_length_is_support=false") != std::string::npos);
            check(summary_text.find("lowering_errors=mir_unavailable") != std::string::npos);

            auto debug_dump = kb->mir_debug_dump();
            check(debug_dump.find("MIR rule coverage:") != std::string::npos);
            check(debug_dump.find("MIR query coverage:") != std::string::npos);
            check(debug_dump.find("MIR rule graphs:") != std::string::npos);
            check(debug_dump.find("MIR query graphs:") != std::string::npos);
            check(debug_dump.find("none") != std::string::npos);

            check(kb->mir_rule_count() == -1);
            check(kb->mir_rule_coverages().empty());
            check(kb->mir_query_coverages().empty());
            check(kb->mir_rule_graphs().empty());
            check(kb->mir_query_graphs().empty());
            auto unavailable_compare = kb->mir_runtime_predicate(
                rulesforge::MirRuntimePredicateRef{rulesforge::MirRuntimePredicateKind::Compare},
                {ConstraintValue{int64_t{1}}, ConstraintValue{int64_t{1}}});
            check(!unavailable_compare.has_value());
            check(!kb->mir_compare_predicate_id(CompareOp::EQ).has_value());
            check(!kb->mir_numeric_literal_predicate_id(CompareOp::EQ, ConstraintValue{int64_t(1)}).has_value());
        }

        it("dispatches runtime predicates to registered native helpers") {
            auto kb = KnowledgeBase::create_empty();
            check(kb != nullptr);

            char const expected[] = "gold";
            auto predicate_id = kb->register_native_predicate(
                "is_expected",
                native_predicate_equals_first_arg,
                const_cast<char*>(expected));
            auto lookup_id = kb->native_predicate_id("is_expected");
            check(lookup_id.has_value());
            check(*lookup_id == predicate_id);

            auto matched = kb->runtime_predicate(
                rulesforge::RuntimePredicateRef{
                    rulesforge::RuntimePredicateBackend::Native,
                    rulesforge::MirRuntimePredicateKind::External,
                    predicate_id},
                {ConstraintValue{std::string{"gold"}}});
            check(matched.has_value());
            check(*matched);

            auto missed = kb->runtime_predicate(
                rulesforge::RuntimePredicateRef{
                    rulesforge::RuntimePredicateBackend::Native,
                    rulesforge::MirRuntimePredicateKind::External,
                    predicate_id},
                {ConstraintValue{std::string{"silver"}}});
            check(missed.has_value());
            check(!*missed);

            auto missing = kb->runtime_predicate(
                rulesforge::RuntimePredicateRef{
                    rulesforge::RuntimePredicateBackend::Native,
                    rulesforge::MirRuntimePredicateKind::External,
                    predicate_id + 1},
                {ConstraintValue{std::string{"gold"}}});
            check(!missing.has_value());
        }

        it("fails non-empty knowledge base build when MIR compile fails") {
            ScopedMirExecutionPlanCompiler scoped_compiler(fail_mir_execution_plan_compile);
            ParsingResult result;
            auto kb = build_knowledge_base(R"(
                declare Customer
                    id: int
                    age: int
                    minSpend: double
                end
                declare Order
                    customerId: int
                    amount: double
                end
                rule "Find Adult High Spend"
                when
                    $c : Customer(age >= 18, $id : id, $min : minSpend)
                    Order(customerId == $id, amount > $min)
                then
                end
            )", result);
            check(!result.success);
            check(kb == nullptr);
            check(!result.errors.empty());
            check(result.errors.front().message.find("MIR execution plan unavailable") != std::string::npos);
        }

        it("fails build when rule count self-check fails") {
            ScopedMirExecutionPlanCompiler scoped_compiler(compile_mismatched_rule_count_mir_plan);
            ParsingResult result;
            auto kb = build_knowledge_base(R"(
                declare Person
                    age: int
                end
                rule "Find Adult"
                when
                    Person(age >= 18)
                then
                end
            )", result);
            check(!result.success);
            check(kb == nullptr);
            check(!result.errors.empty());
            check(result.errors.front().message.find("rule count self-check mismatch") != std::string::npos);
        }

        it("fails build when rule metadata self-check fails") {
            ScopedMirExecutionPlanCompiler scoped_compiler(compile_mismatched_metadata_mir_plan);
            ParsingResult result;
            auto kb = build_knowledge_base(R"(
                declare Person
                    age: int
                end
                rule "Find Adult"
                salience 7
                when
                    Person(age >= 18)
                then
                end
            )", result);
            check(!result.success);
            check(kb == nullptr);
            check(!result.errors.empty());
            check(result.errors.front().message.find("rule metadata self-check failed") != std::string::npos);
        }

        it("fails build when compare self-check fails") {
            ScopedMirExecutionPlanSelfCheck scoped_self_check(fail_mir_compare_self_check);
            ParsingResult result;
            auto kb = build_knowledge_base(R"(
                declare Person
                    age: int
                end
                rule "Find Adult"
                when
                    Person(age >= 18)
                then
                end
            )", result);
            check(!result.success);
            check(kb == nullptr);
            check(!result.errors.empty());
            check(result.errors.front().message.find("compare self-check failure") != std::string::npos);
        }

        it("reports RHS backend unavailable lowering error for an empty knowledge base") {
            auto kb = KnowledgeBase::create_empty();
            check(kb != nullptr);

            auto summary = kb->rhs_backend_summary();
            check(!summary.turboscript_available);
            check(summary.rule_coverage_count == 0);
            check(summary.turboscript_mir_rule_count == 0);
            check(summary.compile_error_rule_count == 0);
            check(summary.command_count == 0);
            check(summary.condition_count == 0);

            auto summary_text = kb->rhs_backend_summary_text();
            check(summary_text.find("turboscript_available=false") != std::string::npos);
            check(summary_text.find("commands=0") != std::string::npos);
            check(summary_text.find("conditions=0") != std::string::npos);
            check(summary_text.find("lowering_errors=rhs_backend_plan_unavailable") != std::string::npos);

            auto debug_dump = kb->rhs_backend_debug_dump();
            check(debug_dump.find("RHS backend coverage:") != std::string::npos);
            check(debug_dump.find("none") != std::string::npos);
            check(kb->rhs_backend_coverages().empty());
        }

        it("builds and runs MIR metadata for the knowledge base") {
            ParsingResult result;
            auto kb = build_knowledge_base(R"(
                declare Person
                    age: int
                end
                rule "Find Adults"
                salience 7
                when
                    Person(age >= 18)
                then
                end
                rule "Disabled"
                enabled false
                when
                    Person(age < 0)
                then
                end
            )", result);

            if (!result.success) throw_parse_failure(result);
            check(kb != nullptr);
            check(kb->has_mir_execution_plan());
            auto summary = kb->mir_summary();
            check(summary.available);
            check(summary.rule_count == 2);
            check(summary.query_count == 0);
            check(summary.fixed_i64_compare_count == 6);
            check(summary.fixed_double_compare_count == 6);
            check(summary.fixed_string_compare_count == 6);
            check(summary.compare_predicate_count == 6);
            check(summary.numeric_literal_predicate_count == 2);
            check(summary.value_list_predicate_count == 0);
            check(summary.eval_expression_predicate_count == 0);
            check(summary.has_generic_i64_compare);
            check(summary.has_generic_double_compare);
            check(summary.supports_numeric_compare_predicates);
            check(summary.supports_string_compare_predicates);
            check(summary.supports_string_compare);
            check(summary.supports_string_contains);
            check(summary.supports_string_matches);
            check(summary.supports_string_affix);
            check(summary.supports_string_length_is);
            check(summary.supports_numeric_literal_predicates);
            check(summary.rule_coverage_count == 2);
            check(summary.query_coverage_count == 0);
            check(summary.rule_graph_count == 2);
            check(summary.rule_graph_predicate_count == 2);
            check(summary.query_graph_count == 0);
            check(summary.query_graph_predicate_count == 0);
            check(summary.lowering_errors.size() == 12);
            auto summary_text = kb->mir_summary_text();
            check(summary_text.find("available=true") != std::string::npos);
            check(summary_text.find("rules=2") != std::string::npos);
            check(summary_text.find("fixed_i64_compare=6") != std::string::npos);
            check(summary_text.find("fixed_string_compare=6") != std::string::npos);
            check(summary_text.find("compare_predicates=6") != std::string::npos);
            check(summary_text.find("numeric_compare_predicates=true") != std::string::npos);
            check(summary_text.find("string_compare_predicates=true") != std::string::npos);
            check(summary_text.find("string_contains_support=true") != std::string::npos);
            check(summary_text.find("string_matches_support=true") != std::string::npos);
            check(summary_text.find("string_affix_support=true") != std::string::npos);
            check(summary_text.find("string_length_is_support=true") != std::string::npos);
            check(summary_text.find("numeric_literal_predicates=2") != std::string::npos);
            check(summary_text.find("value_list_predicates=0") != std::string::npos);
            check(summary_text.find("eval_expression_predicates=0") != std::string::npos);
            check(summary_text.find("rule_coverage=2") != std::string::npos);
            check(summary_text.find("query_coverage=0") != std::string::npos);
            check(summary_text.find("rule_graphs=2") != std::string::npos);
            check(summary_text.find("rule_graph_predicates=2") != std::string::npos);
            check(summary_text.find("query_graphs=0") != std::string::npos);
            check(summary_text.find("query_graph_predicates=0") != std::string::npos);
            check(summary_text.find("lowering_errors=unsupported_operator|unsupported_type|expression_unlowered|collection_helper_unlowered|map_helper_unlowered|dynamic_regex_unlowered|regex_literal_invalid|string_helper_unlowered|forall_pattern_unlowered|eval_pattern_unlowered|accumulate_unlowered|temporal_unlowered") != std::string::npos);
            auto const& rule_coverages = kb->mir_rule_coverages();
            check(rule_coverages.size() == 2);
            check(rule_coverages[0].rule_name == "Find Adults");
            check(rule_coverages[0].constraint_count == 1);
            check(rule_coverages[0].compare_predicate_count == 1);
            check(rule_coverages[0].literal_predicate_count == 1);
            check(rule_coverages[0].lowering_error_constraint_count == 0);
            check(rule_coverages[1].rule_name == "Disabled");
            check(rule_coverages[1].constraint_count == 1);
            check(rule_coverages[1].compare_predicate_count == 1);
            check(rule_coverages[1].literal_predicate_count == 1);
            check(rule_coverages[1].lowering_error_constraint_count == 0);
            auto const& rule_graphs = kb->mir_rule_graphs();
            check(rule_graphs.size() == 2);
            check(rule_graphs[0].rule_name == "Find Adults");
            check(rule_graphs[0].condition_group_index == 0);
            check(rule_graphs[0].nodes.size() == 1);
            check(rule_graphs[0].nodes[0].kind == rulesforge::MirRuleGraphNodeKind::Standard);
            check(rule_graphs[0].nodes[0].constraint_count == 1);
            check(rule_graphs[0].nodes[0].predicates.size() == 1);
            check(rule_graphs[0].nodes[0].predicates[0].role == rulesforge::MirRulePredicateRole::Alpha);
            check(rule_graphs[0].nodes[0].predicates[0].ref.kind == rulesforge::MirRuntimePredicateKind::NumericLiteral);
            check(rule_graphs[0].nodes[0].input_abi.reads_current_fact);
            check(rule_graphs[0].nodes[0].input_abi.reads_token_facts);
            check(rule_graphs[0].nodes[0].input_abi.uses_field_accessor);
            check(rule_graphs[0].nodes[0].input_abi.missing_field_is_non_match);
            check(rule_graphs[0].nodes[0].input_abi.explicit_nil_is_value);
            auto debug_dump = kb->mir_debug_dump();
            check(debug_dump.find("MIR summary: available=true") != std::string::npos);
            check(debug_dump.find("MIR rule coverage:") != std::string::npos);
            check(debug_dump.find("MIR query coverage:") != std::string::npos);
            check(debug_dump.find("MIR rule graphs:") != std::string::npos);
            check(debug_dump.find("MIR query graphs:") != std::string::npos);
            check(debug_dump.find("rule_graph[0:0] \"Find Adults\": patterns=1, predicates=1") != std::string::npos);
            check(debug_dump.find("predicate[0] role=alpha") != std::string::npos);
            check(debug_dump.find("rule[0] \"Find Adults\": constraints=1, compare_predicates=1, literal_predicates=1, lowering_error_constraints=0, lowering_errors=none") != std::string::npos);
            check(debug_dump.find("rule[1] \"Disabled\": constraints=1, compare_predicates=1, literal_predicates=1, lowering_error_constraints=0, lowering_errors=none") != std::string::npos);
            check(kb->mir_rule_count() == 2);
            check(kb->mir_rule_salience(0) == 7);
            check(kb->mir_rule_enabled(0));
            check(!kb->mir_rule_enabled(1));
            auto compare_i64_eq = mir_runtime_compare(*kb, CompareOp::EQ, ConstraintValue{int64_t{42}}, ConstraintValue{int64_t{42}});
            check(compare_i64_eq.has_value());
            check(*compare_i64_eq);
            auto compare_i64_gt = mir_runtime_compare(*kb, CompareOp::GT, ConstraintValue{int64_t{43}}, ConstraintValue{int64_t{42}});
            check(compare_i64_gt.has_value());
            check(*compare_i64_gt);
            auto compare_i64_le = mir_runtime_compare(*kb, CompareOp::LE, ConstraintValue{int64_t{43}}, ConstraintValue{int64_t{42}});
            check(compare_i64_le.has_value());
            check(!*compare_i64_le);
            auto compare_double_gt = mir_runtime_compare(*kb, CompareOp::GT, ConstraintValue{2.5}, ConstraintValue{2.0});
            check(compare_double_gt.has_value());
            check(*compare_double_gt);
            auto compare_double_le = mir_runtime_compare(*kb, CompareOp::LE, ConstraintValue{2.5}, ConstraintValue{2.0});
            check(compare_double_le.has_value());
            check(!*compare_double_le);
            auto compare_string_eq = mir_runtime_compare(*kb, CompareOp::EQ, ConstraintValue{std::string{"gold"}}, ConstraintValue{std::string{"gold"}});
            check(compare_string_eq.has_value());
            check(*compare_string_eq);
            auto compare_string_ne = mir_runtime_compare(*kb, CompareOp::NE, ConstraintValue{std::string{"gold"}}, ConstraintValue{std::string{"silver"}});
            check(compare_string_ne.has_value());
            check(*compare_string_ne);
            auto compare_string_gt = mir_runtime_compare(*kb, CompareOp::GT, ConstraintValue{std::string{"silver"}}, ConstraintValue{std::string{"gold"}});
            check(compare_string_gt.has_value());
            check(*compare_string_gt);
            auto compare_string_lt = mir_runtime_compare(*kb, CompareOp::LT, ConstraintValue{std::string{"gold"}}, ConstraintValue{std::string{"silver"}});
            check(compare_string_lt.has_value());
            check(*compare_string_lt);
            auto compare_string_ge = mir_runtime_compare(*kb, CompareOp::GE, ConstraintValue{std::string{"gold"}}, ConstraintValue{std::string{"gold"}});
            check(compare_string_ge.has_value());
            check(*compare_string_ge);
            auto compare_string_le = mir_runtime_compare(*kb, CompareOp::LE, ConstraintValue{std::string{"gold"}}, ConstraintValue{std::string{"silver"}});
            check(compare_string_le.has_value());
            check(*compare_string_le);
            auto compare_string_eq_false = mir_runtime_compare(*kb, CompareOp::EQ, ConstraintValue{std::string{"gold"}}, ConstraintValue{std::string{"silver"}});
            check(compare_string_eq_false.has_value());
            check(!*compare_string_eq_false);
            auto compare_string_gt_false = mir_runtime_compare(*kb, CompareOp::GT, ConstraintValue{std::string{"gold"}}, ConstraintValue{std::string{"silver"}});
            check(compare_string_gt_false.has_value());
            check(!*compare_string_gt_false);
            auto compare_nil_eq = mir_runtime_compare(*kb, CompareOp::EQ, ConstraintValue{NilValue{}}, ConstraintValue{NilValue{}});
            check(compare_nil_eq.has_value());
            check(*compare_nil_eq);
            auto compare_nil_ne = mir_runtime_compare(*kb, CompareOp::NE, ConstraintValue{NilValue{}}, ConstraintValue{std::string{"gold"}});
            check(compare_nil_ne.has_value());
            check(*compare_nil_ne);
            auto compare_nil_gt = mir_runtime_compare(*kb, CompareOp::GT, ConstraintValue{NilValue{}}, ConstraintValue{std::string{"gold"}});
            check(!compare_nil_gt.has_value());
            auto string_contains = kb->mir_runtime_predicate(
                rulesforge::MirRuntimePredicateRef{rulesforge::MirRuntimePredicateKind::StringContains, 0, CompareOp::Contains},
                {ConstraintValue{std::string{"gold customer"}}, ConstraintValue{std::string{"gold"}}});
            check(string_contains.has_value());
            check(*string_contains);
            auto string_not_contains = kb->mir_runtime_predicate(
                rulesforge::MirRuntimePredicateRef{rulesforge::MirRuntimePredicateKind::StringContains, 0, CompareOp::NotContains},
                {ConstraintValue{std::string{"gold customer"}}, ConstraintValue{std::string{"silver"}}});
            check(string_not_contains.has_value());
            check(*string_not_contains);
            auto string_matches = kb->mir_runtime_predicate(
                rulesforge::MirRuntimePredicateRef{rulesforge::MirRuntimePredicateKind::StringMatches, 0, CompareOp::Matches},
                {ConstraintValue{std::string{"gold-42"}}, ConstraintValue{std::string{"gold-[0-9]+"}}});
            check(string_matches.has_value());
            check(*string_matches);
            auto string_not_matches = kb->mir_runtime_predicate(
                rulesforge::MirRuntimePredicateRef{rulesforge::MirRuntimePredicateKind::StringMatches, 0, CompareOp::NotMatches},
                {ConstraintValue{std::string{"gold-42"}}, ConstraintValue{std::string{"silver-[0-9]+"}}});
            check(string_not_matches.has_value());
            check(*string_not_matches);
            auto invalid_string_matches = kb->mir_runtime_predicate(
                rulesforge::MirRuntimePredicateRef{rulesforge::MirRuntimePredicateKind::StringMatches, 0, CompareOp::Matches},
                {ConstraintValue{std::string{"gold-42"}}, ConstraintValue{std::string{"["}}});
            check(!invalid_string_matches.has_value());
            auto string_starts_with = kb->mir_runtime_predicate(
                rulesforge::MirRuntimePredicateRef{rulesforge::MirRuntimePredicateKind::StringAffix, 0, CompareOp::StartsWith},
                {ConstraintValue{std::string{"gold-42"}}, ConstraintValue{std::string{"gold"}}});
            check(string_starts_with.has_value());
            check(*string_starts_with);
            auto string_ends_with = kb->mir_runtime_predicate(
                rulesforge::MirRuntimePredicateRef{rulesforge::MirRuntimePredicateKind::StringAffix, 0, CompareOp::EndsWith},
                {ConstraintValue{std::string{"gold-42"}}, ConstraintValue{std::string{"-42"}}});
            check(string_ends_with.has_value());
            check(*string_ends_with);
            auto string_length_is = kb->mir_runtime_predicate(
                rulesforge::MirRuntimePredicateRef{rulesforge::MirRuntimePredicateKind::StringLengthIs},
                {ConstraintValue{std::string{"gold-42"}}, ConstraintValue{int64_t{7}}});
            check(string_length_is.has_value());
            check(*string_length_is);
            auto gt_predicate_id = kb->mir_numeric_compare_predicate_id(CompareOp::GT);
            check(gt_predicate_id.has_value());
            auto gt_i64_predicate = kb->mir_runtime_predicate(
                rulesforge::MirRuntimePredicateRef{rulesforge::MirRuntimePredicateKind::Compare, *gt_predicate_id},
                {ConstraintValue{static_cast<int64_t>(43)}, ConstraintValue{static_cast<int64_t>(42)}});
            check(gt_i64_predicate.has_value());
            check(*gt_i64_predicate);
            auto gt_double_predicate = kb->mir_runtime_predicate(
                rulesforge::MirRuntimePredicateRef{rulesforge::MirRuntimePredicateKind::Compare, *gt_predicate_id},
                {ConstraintValue{2.5}, ConstraintValue{2.0}});
            check(gt_double_predicate.has_value());
            check(*gt_double_predicate);
            auto gt_type_mismatch = kb->mir_runtime_predicate(
                rulesforge::MirRuntimePredicateRef{rulesforge::MirRuntimePredicateKind::NumericExpression, *gt_predicate_id},
                {ConstraintValue{std::string{"b"}}, ConstraintValue{std::string{"a"}}});
            check(!gt_type_mismatch.has_value());
            auto generic_gt_predicate_id = kb->mir_compare_predicate_id(CompareOp::GT);
            check(generic_gt_predicate_id.has_value());
            auto generic_string_gt_predicate = kb->mir_runtime_predicate(
                rulesforge::MirRuntimePredicateRef{rulesforge::MirRuntimePredicateKind::Compare, *generic_gt_predicate_id},
                {ConstraintValue{std::string{"silver"}}, ConstraintValue{std::string{"gold"}}});
            check(generic_string_gt_predicate.has_value());
            check(*generic_string_gt_predicate);
            auto generic_string_lt_predicate = kb->mir_runtime_predicate(
                rulesforge::MirRuntimePredicateRef{rulesforge::MirRuntimePredicateKind::Compare, *generic_gt_predicate_id},
                {ConstraintValue{std::string{"gold"}}, ConstraintValue{std::string{"silver"}}});
            check(generic_string_lt_predicate.has_value());
            check(!*generic_string_lt_predicate);
            auto runtime_compare = kb->mir_runtime_predicate(
                rulesforge::MirRuntimePredicateRef{rulesforge::MirRuntimePredicateKind::Compare, *generic_gt_predicate_id},
                {ConstraintValue{static_cast<int64_t>(43)}, ConstraintValue{static_cast<int64_t>(42)}});
            check(runtime_compare.has_value());
            check(*runtime_compare);
            auto runtime_string_contains = kb->mir_runtime_predicate(
                rulesforge::MirRuntimePredicateRef{
                    rulesforge::MirRuntimePredicateKind::StringContains,
                    0,
                    CompareOp::Contains,
                },
                {ConstraintValue{std::string{"gold customer"}}, ConstraintValue{std::string{"gold"}}});
            check(runtime_string_contains.has_value());
            check(*runtime_string_contains);
            auto adult_predicate_id = kb->mir_numeric_literal_predicate_id(
                CompareOp::GE, ConstraintValue{static_cast<int64_t>(18)});
            check(adult_predicate_id.has_value());
            auto adult_predicate = mir_runtime_unary(
                *kb,
                rulesforge::MirRuntimePredicateKind::NumericLiteral,
                *adult_predicate_id,
                ConstraintValue{static_cast<int64_t>(25)});
            check(adult_predicate.has_value());
            check(*adult_predicate);
            auto missing_predicate_id = kb->mir_numeric_literal_predicate_id(
                CompareOp::GE, ConstraintValue{static_cast<int64_t>(21)});
            auto missing_predicate = missing_predicate_id
                ? mir_runtime_unary(
                    *kb,
                    rulesforge::MirRuntimePredicateKind::NumericLiteral,
                    *missing_predicate_id,
                    ConstraintValue{static_cast<int64_t>(25)})
                : std::optional<bool>{};
            check(!missing_predicate.has_value());
            auto adult_predicate_by_id = mir_runtime_unary(
                *kb,
                rulesforge::MirRuntimePredicateKind::NumericLiteral,
                *adult_predicate_id,
                ConstraintValue{static_cast<int64_t>(25)});
            check(adult_predicate_by_id.has_value());
            check(*adult_predicate_by_id);
            auto runtime_literal = kb->mir_runtime_predicate(
                rulesforge::MirRuntimePredicateRef{
                    rulesforge::MirRuntimePredicateKind::NumericLiteral,
                    *adult_predicate_id,
                },
                {ConstraintValue{static_cast<int64_t>(25)}});
            check(runtime_literal.has_value());
            check(*runtime_literal);
            auto mixed_numeric_predicate_by_id = mir_runtime_unary(
                *kb,
                rulesforge::MirRuntimePredicateKind::NumericLiteral,
                *adult_predicate_id,
                ConstraintValue{25.0});
            check(mixed_numeric_predicate_by_id.has_value());
            check(*mixed_numeric_predicate_by_id);
            auto temporal_after = kb->mir_runtime_predicate(
                rulesforge::MirRuntimePredicateRef{
                    rulesforge::MirRuntimePredicateKind::Temporal,
                    0,
                    CompareOp::None,
                    TemporalOp::After,
                    -1,
                },
                {ConstraintValue{static_cast<int64_t>(2000)}, ConstraintValue{static_cast<int64_t>(1000)}});
            check(temporal_after.has_value());
            check(*temporal_after);
            auto runtime_temporal_after = kb->mir_runtime_predicate(
                rulesforge::MirRuntimePredicateRef{
                    rulesforge::MirRuntimePredicateKind::Temporal,
                    0,
                    CompareOp::None,
                    TemporalOp::After,
                    -1,
                },
                {ConstraintValue{static_cast<int64_t>(2000)}, ConstraintValue{static_cast<int64_t>(1000)}});
            check(runtime_temporal_after.has_value());
            check(*runtime_temporal_after);
            auto temporal_before = kb->mir_runtime_predicate(
                rulesforge::MirRuntimePredicateRef{
                    rulesforge::MirRuntimePredicateKind::Temporal,
                    0,
                    CompareOp::None,
                    TemporalOp::Before,
                    -1,
                },
                {ConstraintValue{static_cast<int64_t>(500)}, ConstraintValue{static_cast<int64_t>(1000)}});
            check(temporal_before.has_value());
            check(*temporal_before);
            auto temporal_within = kb->mir_runtime_predicate(
                rulesforge::MirRuntimePredicateRef{
                    rulesforge::MirRuntimePredicateKind::Temporal,
                    0,
                    CompareOp::None,
                    TemporalOp::Within,
                    600,
                },
                {ConstraintValue{static_cast<int64_t>(1500)}, ConstraintValue{static_cast<int64_t>(1000)}});
            check(temporal_within.has_value());
            check(*temporal_within);
            auto temporal_outside = kb->mir_runtime_predicate(
                rulesforge::MirRuntimePredicateRef{
                    rulesforge::MirRuntimePredicateKind::Temporal,
                    0,
                    CompareOp::None,
                    TemporalOp::Within,
                    600,
                },
                {ConstraintValue{static_cast<int64_t>(1700)}, ConstraintValue{static_cast<int64_t>(1000)}});
            check(temporal_outside.has_value());
            check(!*temporal_outside);
            auto temporal_coincides = kb->mir_runtime_predicate(
                rulesforge::MirRuntimePredicateRef{
                    rulesforge::MirRuntimePredicateKind::Temporal,
                    0,
                    CompareOp::None,
                    TemporalOp::Coincides,
                    -1,
                },
                {ConstraintValue{static_cast<int64_t>(1000)}, ConstraintValue{static_cast<int64_t>(1000)}});
            check(temporal_coincides.has_value());
            check(*temporal_coincides);
            auto temporal_during = kb->mir_runtime_predicate(
                rulesforge::MirRuntimePredicateRef{
                    rulesforge::MirRuntimePredicateKind::Temporal,
                    0,
                    CompareOp::None,
                    TemporalOp::During,
                    -1,
                },
                {ConstraintValue{static_cast<int64_t>(2000)}, ConstraintValue{static_cast<int64_t>(1000)}});
            check(temporal_during.has_value());
            check(*temporal_during);
            auto attrs = std::make_shared<ValueMap>();
            attrs->entries[std::string("tier")] = int64_t(1);
            ConstraintValue map_value = attrs;
            ConstraintValue map_key = std::string("tier");
            auto map_contains_key = kb->mir_runtime_predicate(
                rulesforge::MirRuntimePredicateRef{rulesforge::MirRuntimePredicateKind::MapContainsKey, 0, CompareOp::ContainsKey},
                {map_value, map_key});
            check(map_contains_key.has_value());
            check(*map_contains_key);
            auto map_not_contains_key = kb->mir_runtime_predicate(
                rulesforge::MirRuntimePredicateRef{rulesforge::MirRuntimePredicateKind::MapContainsKey, 0, CompareOp::NotContainsKey},
                {map_value, ConstraintValue{std::string{"missing"}}});
            check(map_not_contains_key.has_value());
            check(*map_not_contains_key);
        }

        it("reports rule LHS graph predicates for alpha and join condition groups") {
            ParsingResult result;
            auto kb = build_knowledge_base(R"(
                declare Customer
                    id: int
                    age: int
                end
                declare Order
                    customerId: int
                    amount: double
                end
                rule "Join Graph Rule"
                when
                    $c : Customer($id : id, age >= 18)
                    $o : Order(customerId == $id, amount > 100.0)
                then
                end
            )", result);
            if (!result.success) throw_parse_failure(result);
            check(kb != nullptr);
            check(kb->mir_summary().rule_graph_count == 1);
            check(kb->mir_summary().rule_graph_predicate_count == 3);

            auto const& graphs = kb->mir_rule_graphs();
            check(graphs.size() == 1);
            check(graphs[0].rule_name == "Join Graph Rule");
            check(graphs[0].nodes.size() == 2);
            check(graphs[0].nodes[0].predicates.size() == 1);
            check(graphs[0].nodes[0].predicates[0].role == rulesforge::MirRulePredicateRole::Alpha);
            check(graphs[0].nodes[1].predicates.size() == 2);
            bool saw_alpha = false;
            bool saw_join = false;
            for (auto const& predicate : graphs[0].nodes[1].predicates) {
                saw_alpha = saw_alpha || predicate.role == rulesforge::MirRulePredicateRole::Alpha;
                saw_join = saw_join || predicate.role == rulesforge::MirRulePredicateRole::Join;
            }
            check(saw_alpha);
            check(saw_join);
        }

        it("lowers numeric expression constraints to MIR") {
            ParsingResult result;
            auto kb = build_knowledge_base(R"(
                declare Order
                    amount: double
                    base: double
                end
                rule "Find Orders Above Multiplied Base"
                when
                    Order(amount > ($base * 1.2))
                then
                end
            )", result);

            if (!result.success) throw_parse_failure(result);
            check(kb != nullptr);
            check(kb->mir_summary().numeric_expression_predicate_count == 1);
            check(kb->mir_rule_coverages()[0].lowering_errors.empty());
        }

        it("lowers numeric expression math functions to MIR") {
            auto session = build_session(R"(
                declare Order
                    amount: double
                    base: double
                    delta: double
                    angle: double
                end
                rule "Find Math Orders"
                when
                    Order(amount == (floor($base) + ceil($base) + abs($delta) + sqrt($base) + sin($angle) + cos($angle) + tan($angle) + pow($base, 2) + log(exp(2)) + acos(1) + asin(0) + atan(0) + log10(100) + round(2.4) + trunc(2.9) + sinh(0) + cosh(0) + tanh(0) + log2(8) + log1p(0) + expm1(0) + erf(0) + fmod(5, 2) + atan2(0, 1) + hypot(3, 4) + root(27, 3) + sgn(0) + min($base, 3) + max($base, 5)))
                then
                end
            )");
            check(session->get_knowledge_base()->mir_summary().numeric_expression_predicate_count == 1);
            check(session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());

            auto matching = std::make_shared<Fact>();
            matching->type = "Order";
            matching->fields["amount"] = 59.0;
            matching->fields["base"] = 4.0;
            matching->fields["delta"] = -3.0;
            matching->fields["angle"] = 0.0;

            auto missing = std::make_shared<Fact>();
            missing->type = "Order";
            missing->fields["amount"] = 27.0;
            missing->fields["base"] = 4.0;
            missing->fields["delta"] = -3.0;
            missing->fields["angle"] = 0.0;

            session->add_fact(matching);
            session->add_fact(missing);

            check(session->fire_all_rules() == 1);
        }

        it("lowers wide numeric expression constraints to MIR") {
            auto session = build_session(R"(
                declare Order
                    amount: double
                    a: double
                    b: double
                    c: double
                    d: double
                    e: double
                    f: double
                    g: double
                    h: double
                    i: double
                end
                rule "Find Orders Above Wide Expression"
                when
                    Order(amount > ($a + $b + $c + $d + $e + $f + $g + $h + $i))
                then
                end
            )");
            check(session->get_knowledge_base()->mir_summary().numeric_expression_predicate_count == 1);
            check(session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());

            auto matching = std::make_shared<Fact>();
            matching->type = "Order";
            matching->fields["amount"] = 46.0;
            matching->fields["a"] = 1.0;
            matching->fields["b"] = 2.0;
            matching->fields["c"] = 3.0;
            matching->fields["d"] = 4.0;
            matching->fields["e"] = 5.0;
            matching->fields["f"] = 6.0;
            matching->fields["g"] = 7.0;
            matching->fields["h"] = 8.0;
            matching->fields["i"] = 9.0;

            auto missing = std::make_shared<Fact>();
            missing->type = "Order";
            missing->fields = matching->fields;
            missing->fields["amount"] = 45.0;

            session->add_fact(matching);
            session->add_fact(missing);

            check(session->fire_all_rules() == 1);
        }

        it("hard-fails build for unsupported expression shape") {
            check_build_fails_with(R"(
                declare Order
                    amount: double
                    base: double
                end
                rule "Find Orders Above Unsupported Expression"
                when
                    Order(amount > random())
                then
                end
            )", "expression_unlowered");
        }

        it("runs explicit nil literal compare constraints through MIR") {
            ParsingResult result;
            auto kb = build_knowledge_base(R"(
                declare Person
                    status: String
                end
                rule "Find Nil Status"
                when
                    Person(status == nil)
                then
                end
                rule "Find Present Status"
                when
                    Person(status != nil)
                then
                end
                query "nilStatus"
                    $p : Person(status == nil)
                end
            )", result);

            if (!result.success) throw_parse_failure(result);
            check(kb != nullptr);
            check(kb->mir_summary().compare_predicate_count >= 2);
            check(kb->mir_summary().query_coverage_count == 1);
            check(kb->mir_summary().rule_graph_count == 2);
            check(kb->mir_summary().rule_graph_predicate_count == 2);
            check(kb->mir_summary().query_graph_count == 1);
            check(kb->mir_summary().query_graph_predicate_count == 1);
            for (auto const& coverage : kb->mir_rule_coverages()) {
                check(coverage.constraint_count == 1);
                check(coverage.compare_predicate_count == 1);
                check(coverage.lowering_error_constraint_count == 0);
                check(coverage.lowering_errors.empty());
            }
            auto const& rule_graphs = kb->mir_rule_graphs();
            check(rule_graphs.size() == 2);
            check(rule_graphs[0].nodes.size() == 1);
            check(rule_graphs[0].nodes[0].predicates.size() == 1);
            check(rule_graphs[0].nodes[0].predicates[0].role == rulesforge::MirRulePredicateRole::Alpha);
            check(rule_graphs[0].nodes[0].predicates[0].ref.kind == rulesforge::MirRuntimePredicateKind::Compare);
            auto const& query_coverages = kb->mir_query_coverages();
            check(query_coverages.size() == 1);
            check(query_coverages[0].query_name == "nilStatus");
            check(query_coverages[0].constraint_count == 1);
            check(query_coverages[0].compare_predicate_count == 1);
            check(query_coverages[0].lowering_error_constraint_count == 0);
            check(query_coverages[0].lowering_errors.empty());
            auto const& query_graphs = kb->mir_query_graphs();
            check(query_graphs.size() == 1);
            check(query_graphs[0].query_name == "nilStatus");
            check(query_graphs[0].nodes.size() == 1);
            check(query_graphs[0].nodes[0].binding == "$p");
            check(query_graphs[0].nodes[0].fact_type == "Person");
            check(query_graphs[0].nodes[0].constraint_count == 1);
            check(query_graphs[0].nodes[0].predicates.size() == 1);
            check(query_graphs[0].nodes[0].predicates[0].kind == rulesforge::MirRuntimePredicateKind::Compare);
            check(query_graphs[0].nodes[0].predicates[0].compare_op == CompareOp::None);

            auto session = kb->create_session();
            check(session != nullptr);

            auto nil_status = std::make_shared<Fact>();
            nil_status->type = "Person";
            nil_status->fields["status"] = NilValue{};

            auto present_status = std::make_shared<Fact>();
            present_status->type = "Person";
            present_status->fields["status"] = std::string{"Gold"};

            auto missing_status = std::make_shared<Fact>();
            missing_status->type = "Person";

            session->add_fact(nil_status);
            session->add_fact(present_status);
            session->add_fact(missing_status);

            check(session->fire_all_rules() == 2);

            auto query_result = session->execute_query("nilStatus");
            check(query_result.success());
            check(query_result.size() == 1);
            auto nil_people = query_result.getColumn("$p");
            check(nil_people.size() == 1);
            check(nil_people[0] == nil_status.get());
        }

        it("filters compound fields with MIR-backed nil equality") {
            auto session = build_session(R"(
                declare Box
                    values: List<int>
                end
                rule "Find Nil Boxes"
                when
                    Box(values == nil)
                then
                end
            )");
            check(session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());

            auto nil_values = std::make_shared<Fact>();
            nil_values->type = "Box";
            nil_values->fields["values"] = NilValue{};

            auto present_values = std::make_shared<Fact>();
            present_values->type = "Box";
            present_values->fields["values"] = make_typed_list({int64_t(10)});

            auto missing_values = std::make_shared<Fact>();
            missing_values->type = "Box";

            session->add_fact(nil_values);
            session->add_fact(present_values);
            session->add_fact(missing_values);

            check(session->fire_all_rules() == 1);
        }

        it("executes query compound nil equality through MIR-backed compare") {
            ParsingResult result;
            auto kb = build_knowledge_base(R"(
                declare Box
                    values: List<int>
                end
                query "nilValues"
                    $b: Box(values == nil)
                end
            )", result);
            if (!result.success) throw_parse_failure(result);
            check(kb != nullptr);
            check(kb->mir_query_coverages().size() == 1);
            check(kb->mir_query_coverages()[0].lowering_errors.empty());

            auto session = kb->create_session();
            check(session != nullptr);

            auto nil_values = std::make_shared<Fact>();
            nil_values->type = "Box";
            nil_values->fields["values"] = NilValue{};

            auto present_values = std::make_shared<Fact>();
            present_values->type = "Box";
            present_values->fields["values"] = make_typed_list({int64_t(10)});

            auto missing_values = std::make_shared<Fact>();
            missing_values->type = "Box";

            session->add_fact(nil_values);
            session->add_fact(present_values);
            session->add_fact(missing_values);

            auto query_result = session->execute_query("nilValues");
            check(query_result.success());
            check(query_result.size() == 1);
            auto boxes = query_result.getColumn("$b");
            check(boxes.size() == 1);
            check(boxes[0] == nil_values.get());
        }

        it("filters compound bound fields with MIR-backed exact equality") {
            auto session = build_session(R"(
                declare Box
                    values: List<int>
                end
                declare Probe
                    values: List<int>
                end
                rule "Find Same Values"
                when
                    $b: Box($expected: values)
                    Probe(values == $expected)
                then
                end
            )");
            check(session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());

            auto box = std::make_shared<Fact>();
            box->type = "Box";
            box->fields["values"] = make_typed_list({int64_t(10), int64_t(20)});

            auto matching = std::make_shared<Fact>();
            matching->type = "Probe";
            matching->fields["values"] = make_typed_list({int64_t(10), int64_t(20)});

            auto missing = std::make_shared<Fact>();
            missing->type = "Probe";
            missing->fields["values"] = make_typed_list({int64_t(20), int64_t(10)});

            session->add_fact(box);
            session->add_fact(matching);
            session->add_fact(missing);

            check(session->fire_all_rules() == 1);
        }

        it("filters compound bound fields with MIR-backed exact inequality") {
            auto session = build_session(R"(
                declare Baseline
                    attrs: Map<String, int>
                end
                declare Profile
                    attrs: Map<String, int>
                end
                rule "Find Different Attributes"
                when
                    $b: Baseline($expected: attrs)
                    Profile(attrs != $expected)
                then
                end
            )");
            check(session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());

            auto expected_attrs = std::make_shared<ValueMap>();
            expected_attrs->entries[std::string("tier")] = int64_t(1);
            auto baseline = std::make_shared<Fact>();
            baseline->type = "Baseline";
            baseline->fields["attrs"] = expected_attrs;

            auto same_attrs = std::make_shared<ValueMap>();
            same_attrs->entries[std::string("tier")] = int64_t(1);
            auto same = std::make_shared<Fact>();
            same->type = "Profile";
            same->fields["attrs"] = same_attrs;

            auto different_attrs = std::make_shared<ValueMap>();
            different_attrs->entries[std::string("tier")] = int64_t(2);
            auto different = std::make_shared<Fact>();
            different->type = "Profile";
            different->fields["attrs"] = different_attrs;

            session->add_fact(baseline);
            session->add_fact(same);
            session->add_fact(different);

            check(session->fire_all_rules() == 1);
        }

        it("hard-fails build for invalid query regex literal") {
            check_build_fails_with(R"(
                declare Customer
                    code: String
                end
                query "invalidRegex"
                    Customer(code matches "[")
                end
            )", "MIR lowering failed for query 'invalidRegex': regex_literal_invalid");
        }

        it("covers MIR string and scalar value-list operators without lowering errors") {
            ParsingResult result;
            auto kb = build_knowledge_base(R"(
                declare Customer
                    status: String
                    code: String
                    score: double
                end
                rule "Find Gold Customers"
                when
                    Customer(status contains "Gold")
                then
                end
                rule "Find Target Status Customers"
                when
                    Customer(status in ("Gold", "Platinum"))
                then
                end
                rule "Find Gold Codes"
                when
                    Customer(code matches "Gold-[0-9]+")
                then
                end
                rule "Find Prefixed Codes"
                when
                    Customer(code startsWith "Gold")
                then
                end
                rule "Find Suffixed Codes"
                when
                    Customer(code endsWith "-42")
                then
                end
                rule "Find Fixed Length Codes"
                when
                    Customer(code lengthIs 7)
                then
                end
                rule "Find Unblocked Scores"
                when
                    Customer(score not in (10, 20.5))
                then
                end
            )", result);

            if (!result.success) throw_parse_failure(result);
            check(kb != nullptr);
            check(kb->mir_summary().value_list_predicate_count == 2);
            auto status_list_id = kb->mir_value_list_predicate_id(
                CompareOp::In,
                std::vector<ConstraintValue>{
                    ConstraintValue{std::string{"Gold"}},
                    ConstraintValue{std::string{"Platinum"}}});
            check(status_list_id.has_value());
            auto gold_status = mir_runtime_unary(
                *kb,
                rulesforge::MirRuntimePredicateKind::ValueList,
                *status_list_id,
                ConstraintValue{std::string{"Gold"}});
            check(gold_status.has_value());
            check(*gold_status);
            auto silver_status = mir_runtime_unary(
                *kb,
                rulesforge::MirRuntimePredicateKind::ValueList,
                *status_list_id,
                ConstraintValue{std::string{"Silver"}});
            check(silver_status.has_value());
            check(!*silver_status);
            auto runtime_gold_status = kb->mir_runtime_predicate(
                rulesforge::MirRuntimePredicateRef{
                    rulesforge::MirRuntimePredicateKind::ValueList,
                    *status_list_id,
                },
                {ConstraintValue{std::string{"Gold"}}});
            check(runtime_gold_status.has_value());
            check(*runtime_gold_status);
            auto score_list_id = kb->mir_value_list_predicate_id(
                CompareOp::NotIn,
                std::vector<ConstraintValue>{
                    ConstraintValue{static_cast<int64_t>(10)},
                    ConstraintValue{20.5}});
            check(score_list_id.has_value());
            auto blocked_score = mir_runtime_unary(
                *kb,
                rulesforge::MirRuntimePredicateKind::ValueList,
                *score_list_id,
                ConstraintValue{10.0});
            check(blocked_score.has_value());
            check(!*blocked_score);
            auto allowed_score = mir_runtime_unary(
                *kb,
                rulesforge::MirRuntimePredicateKind::ValueList,
                *score_list_id,
                ConstraintValue{21.0});
            check(allowed_score.has_value());
            check(*allowed_score);
            auto const& rule_coverages = kb->mir_rule_coverages();
            check(rule_coverages.size() == 7);
            for (auto const& coverage : rule_coverages) {
                check(coverage.constraint_count == 1);
                check(coverage.compare_predicate_count == 1);
                check(coverage.lowering_error_constraint_count == 0);
                check(coverage.lowering_errors.empty());
            }
        }

        it("filters mixed scalar value-list through MIR collection helper") {
            auto session = build_session(R"(
                declare Customer
                    status: String
                end
                rule "Find Mixed Status"
                when
                    Customer(status in ("Gold", 7, nil))
                then
                end
            )");
            check(session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());

            auto matching = std::make_shared<Fact>();
            matching->type = "Customer";
            matching->fields["status"] = std::string("Gold");

            auto numeric_string = std::make_shared<Fact>();
            numeric_string->type = "Customer";
            numeric_string->fields["status"] = std::string("7");

            auto missing = std::make_shared<Fact>();
            missing->type = "Customer";
            missing->fields["status"] = std::string("Silver");

            session->add_fact(matching);
            session->add_fact(numeric_string);
            session->add_fact(missing);

            check(session->fire_all_rules() == 1);
        }

        it("hard-fails build for non-string FactList memberOf") {
            check_build_fails_with(R"(
                declare Container
                    items: FactList
                end
                declare Probe
                    value: int
                end
                rule "Find FactList Numeric Member"
                when
                    $c: Container($items: items)
                    Probe(value memberOf $items)
                then
                end
            )", "collection_helper_unlowered");
        }

        it("hard-fails build for non-string negated FactList memberOf") {
            check_build_fails_with(R"(
                declare Container
                    items: FactList
                end
                declare Probe
                    value: int
                end
                rule "Find Non FactList Numeric Member"
                when
                    $c: Container($items: items)
                    Probe(value not memberOf $items)
                then
                end
            )", "collection_helper_unlowered");
        }

        it("hard-fails build for unsupported advanced pattern semantics") {
            check_build_fails_with(R"(
                declare Event
                    timestamp: long
                    kind: String
                end
                declare Purchase
                    value: double
                end
                declare Total
                    result: double
                end
                declare Customer
                    id: int
                end
                declare Order
                    customerId: int
                    shipped: boolean
                end
                declare Basket
                    items: FactList
                end
                declare Item
                    sku: String
                end
                query "findEvents"
                    $e : Event(kind == "target")
                end
                query "findOrders"(Customer $c)
                    $o : Order(customerId == $c.id)
                end
                rule "Temporal Rule"
                when
                    $e1 : Event(kind == "target")
                    $e2 : Event(timestamp after $e1.timestamp, within 10s of $e1)
                then
                end
                rule "Temporal Operators Rule"
                when
                    $e1 : Event(kind == "target")
                    $e2 : Event(
                        timestamp before $e1.timestamp,
                        timestamp coincides $e1.timestamp,
                        timestamp during $e1.timestamp
                    )
                then
                end
                rule "Accumulate Rule"
                when
                    $t : Total() from accumulate(
                        $p : Purchase(),
                        sum(random())
                    )
                then
                end
                rule "Unnest Rule"
                when
                    $b : Basket()
                    $i : Item() from unnest($b.items)
                then
                end
                rule "Window Rule"
                when
                    Event() over window:length(3)
                then
                end
                rule "Time Window Rule"
                when
                    Event() over window:time(10s)
                then
                end
                rule "Query Call Rule"
                when
                    "findEvents"()
                then
                end
                rule "Parameterized Query Call Rule"
                when
                    $c : Customer()
                    "findOrders"($c)
                then
                end
                rule "Forall Rule"
                when
                    $c : Customer($id : id)
                    forall (
                        Order(customerId == $id),
                        Order(shipped == true)
                    )
                then
                end
            )", "accumulate_unlowered");

            auto accumulate_expression_session = build_session(R"(
                declare Purchase
                    value: double
                end
                declare Total
                    result: double
                end
                rule "Accumulate Rule"
                when
                    $t : Total() from accumulate(
                        $p : Purchase(),
                        sum($p.value + tan(0) + round(1.2) + sinh(0) + fmod(4, 2) + root(8, 3) - 2 + sgn(0))
                    )
                then
                end
            )");
            check(accumulate_expression_session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());

            auto expression_purchase = std::make_shared<Fact>();
            expression_purchase->type = "Purchase";
            expression_purchase->fields["value"] = 4.0;
            accumulate_expression_session->add_fact(expression_purchase);

            Fact* expression_total = nullptr;
            for (int64_t id = 1; id <= accumulate_expression_session->get_next_fact_id(); ++id) {
                Fact* fact = accumulate_expression_session->get_fact_by_id(id);
                if (fact && fact->type == "Total") {
                    expression_total = fact;
                    break;
                }
            }
            check(expression_total != nullptr);
            check(expression_total->fields.find("result") != expression_total->fields.end());
            check(std::abs(std::get<double>(expression_total->fields["result"]) - 5.0) < 0.001);

            auto accumulate_session = build_session(R"(
                declare Purchase
                    value: double
                end
                declare Total
                    result: double
                end
                declare PurchaseCount
                    result: int
                end
                declare AveragePurchase
                    result: double
                end
                declare MinPurchase
                    result: double
                end
                declare MaxPurchase
                    result: double
                end
                declare PurchaseList
                    result: FactList
                end
                declare PurchaseSet
                    result: FactList
                end
                rule "Sum Purchases"
                when
                    $t : Total() from accumulate(
                        $p : Purchase(),
                        sum($p.value + 1)
                    )
                then
                end
                rule "Count Purchases"
                when
                    $c : PurchaseCount() from accumulate(
                        $p : Purchase(),
                        count()
                    )
                then
                end
                rule "Average Purchases"
                when
                    $a : AveragePurchase() from accumulate(
                        $p : Purchase(),
                        average($p.value)
                    )
                then
                end
                rule "Min Purchase"
                when
                    $m : MinPurchase() from accumulate(
                        $p : Purchase(),
                        min($p.value)
                    )
                then
                end
                rule "Max Purchase"
                when
                    $m : MaxPurchase() from accumulate(
                        $p : Purchase(),
                        max($p.value)
                    )
                then
                end
                rule "Collect Purchase List"
                when
                    $l : PurchaseList() from accumulate(
                        $p : Purchase(),
                        collectList($p)
                    )
                then
                end
                rule "Collect Purchase Set"
                when
                    $s : PurchaseSet() from accumulate(
                        $p : Purchase(),
                        collectSet($p)
                    )
                then
                end
            )");
            for (auto const& coverage : accumulate_session->get_knowledge_base()->mir_rule_coverages()) {
                check(coverage.lowering_errors.empty());
            }

            auto first_purchase = std::make_shared<Fact>();
            first_purchase->type = "Purchase";
            first_purchase->fields["value"] = 10.5;

            auto second_purchase = std::make_shared<Fact>();
            second_purchase->type = "Purchase";
            second_purchase->fields["value"] = 4.5;

            accumulate_session->add_fact(first_purchase);
            accumulate_session->add_fact(second_purchase);

            Fact* total_fact = nullptr;
            Fact* count_fact = nullptr;
            Fact* average_fact = nullptr;
            Fact* min_fact = nullptr;
            Fact* max_fact = nullptr;
            Fact* list_fact = nullptr;
            Fact* set_fact = nullptr;
            for (int64_t id = 1; id <= accumulate_session->get_next_fact_id(); ++id) {
                Fact* fact = accumulate_session->get_fact_by_id(id);
                if (fact && fact->type == "Total") {
                    total_fact = fact;
                } else if (fact && fact->type == "PurchaseCount") {
                    count_fact = fact;
                } else if (fact && fact->type == "AveragePurchase") {
                    average_fact = fact;
                } else if (fact && fact->type == "MinPurchase") {
                    min_fact = fact;
                } else if (fact && fact->type == "MaxPurchase") {
                    max_fact = fact;
                } else if (fact && fact->type == "PurchaseList") {
                    list_fact = fact;
                } else if (fact && fact->type == "PurchaseSet") {
                    set_fact = fact;
                }
            }
            check(total_fact != nullptr);
            check(count_fact != nullptr);
            check(average_fact != nullptr);
            check(min_fact != nullptr);
            check(max_fact != nullptr);
            check(list_fact != nullptr);
            check(set_fact != nullptr);
            check(total_fact->fields.find("result") != total_fact->fields.end());
            check(count_fact->fields.find("result") != count_fact->fields.end());
            check(average_fact->fields.find("result") != average_fact->fields.end());
            check(min_fact->fields.find("result") != min_fact->fields.end());
            check(max_fact->fields.find("result") != max_fact->fields.end());
            check(list_fact->fields.find("result") != list_fact->fields.end());
            check(set_fact->fields.find("result") != set_fact->fields.end());
            check(std::abs(std::get<double>(total_fact->fields["result"]) - 17.0) < 0.001);
            check(std::get<int64_t>(count_fact->fields["result"]) == 2);
            check(std::abs(std::get<double>(average_fact->fields["result"]) - 7.5) < 0.001);
            check(std::abs(std::get<double>(min_fact->fields["result"]) - 4.5) < 0.001);
            check(std::abs(std::get<double>(max_fact->fields["result"]) - 10.5) < 0.001);
            check(std::holds_alternative<FactList>(list_fact->fields["result"]));
            check(std::holds_alternative<FactList>(set_fact->fields["result"]));
            check(std::get<FactList>(list_fact->fields["result"]).facts.size() == 2);
            check(std::get<FactList>(set_fact->fields["result"]).facts.size() == 2);

            accumulate_session->retract_fact(first_purchase.get());
            check(std::abs(std::get<double>(total_fact->fields["result"]) - 5.5) < 0.001);
            check(std::get<int64_t>(count_fact->fields["result"]) == 1);
            check(std::abs(std::get<double>(average_fact->fields["result"]) - 4.5) < 0.001);
            check(std::abs(std::get<double>(min_fact->fields["result"]) - 4.5) < 0.001);
            check(std::abs(std::get<double>(max_fact->fields["result"]) - 4.5) < 0.001);
            check(std::get<FactList>(list_fact->fields["result"]).facts.size() == 1);
            check(std::get<FactList>(list_fact->fields["result"]).facts[0] == second_purchase.get());
            check(std::get<FactList>(set_fact->fields["result"]).facts.size() == 1);
            check(std::get<FactList>(set_fact->fields["result"]).facts[0] == second_purchase.get());

            auto unnest_session = build_session(R"(
                declare Basket
                    items: FactList
                end
                declare Item
                    sku: String
                end
                declare Other
                    sku: String
                end
                rule "Unnest Rule"
                when
                    $b : Basket()
                    $i : Item(sku == "target") from unnest($b.items)
                then
                end
            )");
            check(unnest_session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());

            auto matching_item = std::make_shared<Fact>();
            matching_item->type = "Item";
            matching_item->fields["sku"] = std::string("target");

            auto blocked_item = std::make_shared<Fact>();
            blocked_item->type = "Item";
            blocked_item->fields["sku"] = std::string("other");

            auto blocked_type = std::make_shared<Fact>();
            blocked_type->type = "Other";
            blocked_type->fields["sku"] = std::string("target");

            auto basket = std::make_shared<Fact>();
            basket->type = "Basket";
            basket->fields["items"] = FactList{{matching_item.get(), blocked_item.get(), blocked_type.get()}};

            unnest_session->add_fact(basket);
            check(unnest_session->fire_all_rules() == 1);

            auto window_session = build_session(R"(
                declare Event
                    kind: String
                end
                rule "Window Rule"
                when
                    Event(kind == "target") over window:length(2)
                then
                end
            )");
            check(window_session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());

            auto first_event = std::make_shared<Fact>();
            first_event->type = "Event";
            first_event->fields["kind"] = std::string("target");

            auto ignored_event = std::make_shared<Fact>();
            ignored_event->type = "Event";
            ignored_event->fields["kind"] = std::string("ignored");

            auto second_event = std::make_shared<Fact>();
            second_event->type = "Event";
            second_event->fields["kind"] = std::string("target");

            auto third_event = std::make_shared<Fact>();
            third_event->type = "Event";
            third_event->fields["kind"] = std::string("target");

            window_session->add_fact(first_event);
            window_session->add_fact(ignored_event);
            window_session->add_fact(second_event);
            window_session->add_fact(third_event);
            check(window_session->fire_all_rules() == 2);

            check_build_fails_with(R"(
                declare Event
                    kind: String
                end
                rule "Unknown Query Call Rule"
                when
                    "missingQuery"()
                then
                end
            )", "query call references unknown query 'missingQuery'");

            check_build_fails_with(R"(
                declare Event
                    kind: String
                end
                query "findEvents"
                    $e : Event(kind == "target")
                end
                rule "Bound Query Call Rule"
                when
                    $q : "findEvents"()
                then
                end
            )", "query call row binding is not supported");

            check_build_fails_with(R"(
                declare Event
                    kind: String
                end
                rule "Bound Eval Rule"
                when
                    $e : Event()
                    $ok : eval($e.kind == "target")
                then
                end
            )", "pattern binding is only supported for fact-producing patterns");

            check_build_fails_with(R"(
                declare Event
                    kind: String
                end
                query "findEvents"
                    $e : Event(kind == "target")
                end
                rule "Query Call Rule"
                when
                    $e : Event()
                    "findEvents"($e)
                then
                end
            )", "expects 0 argument(s) but got 1");

            auto forall_session = build_session(R"(
                declare Customer
                    id: int
                end
                declare Order
                    customerId: int
                    shipped: boolean
                end
                rule "Forall Rule"
                when
                    $c : Customer($id : id)
                    forall (
                        Order(customerId == $id),
                        Order(shipped == true)
                    )
                then
                end
            )");
            check(forall_session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());

            auto shipped_customer = std::make_shared<Fact>();
            shipped_customer->type = "Customer";
            shipped_customer->fields["id"] = int64_t(1);

            auto unshipped_customer = std::make_shared<Fact>();
            unshipped_customer->type = "Customer";
            unshipped_customer->fields["id"] = int64_t(2);

            auto shipped_order = std::make_shared<Fact>();
            shipped_order->type = "Order";
            shipped_order->fields["customerId"] = int64_t(1);
            shipped_order->fields["shipped"] = int64_t(1);

            auto unshipped_order = std::make_shared<Fact>();
            unshipped_order->type = "Order";
            unshipped_order->fields["customerId"] = int64_t(2);
            unshipped_order->fields["shipped"] = int64_t(0);

            forall_session->add_fact(shipped_customer);
            forall_session->add_fact(unshipped_customer);
            forall_session->add_fact(shipped_order);
            forall_session->add_fact(unshipped_order);

            check(forall_session->fire_all_rules() == 1);
        }

        it("covers conditional pattern orchestration with MIR-backed predicates") {
            ParsingResult result;
            auto kb = build_knowledge_base(R"(
                declare Customer
                    id: int
                end
                declare Order
                    customerId: int
                end
                rule "Not Pattern Rule"
                when
                    $c : Customer($id : id)
                    not (Order(customerId > $id))
                then
                end
                rule "Exists Pattern Rule"
                when
                    $c : Customer($id : id)
                    exists (Order(customerId > $id))
                then
                end
            )", result);
            if (!result.success) throw_parse_failure(result);
            check(kb != nullptr);
            auto const& coverages = kb->mir_rule_coverages();
            check(coverages.size() == 2);
            check(coverages[0].lowering_errors.empty());
            check(coverages[1].lowering_errors.empty());
        }

        it("lowers numeric eval pattern predicates to MIR") {
            auto session = build_session(R"(
                declare Customer
                    age: int
                end
                rule "Eval Rule"
                when
                    Customer($age : age)
                    eval($age > 18)
                then
                end
            )");
            check(session->get_knowledge_base()->mir_summary().eval_expression_predicate_count == 1);
            check(session->get_knowledge_base()->mir_summary().rule_graph_count == 1);
            check(session->get_knowledge_base()->mir_summary().rule_graph_predicate_count == 1);
            auto const& graphs = session->get_knowledge_base()->mir_rule_graphs();
            check(graphs.size() == 1);
            check(graphs[0].nodes.size() == 2);
            check(graphs[0].nodes[1].kind == rulesforge::MirRuleGraphNodeKind::Eval);
            check(graphs[0].nodes[1].predicates.size() == 1);
            check(graphs[0].nodes[1].predicates[0].role == rulesforge::MirRulePredicateRole::Eval);
            check(graphs[0].nodes[1].predicates[0].ref.kind == rulesforge::MirRuntimePredicateKind::EvalExpression);
            std::size_t const eval_predicate_id = 0;
            auto const* eval_variables =
                session->get_knowledge_base()->mir_eval_expression_predicate_variables(eval_predicate_id);
            check(eval_variables != nullptr);
            check(eval_variables->size() == 1);
            check((*eval_variables)[0] == "$age");
            auto runtime_eval_young = session->get_knowledge_base()->mir_runtime_predicate(
                rulesforge::MirRuntimePredicateRef{
                    rulesforge::MirRuntimePredicateKind::EvalExpression,
                    eval_predicate_id,
                },
                {ConstraintValue{static_cast<int64_t>(17)}});
            check(runtime_eval_young.has_value());
            check(!*runtime_eval_young);
            auto runtime_eval_adult = session->get_knowledge_base()->mir_runtime_predicate(
                rulesforge::MirRuntimePredicateRef{
                    rulesforge::MirRuntimePredicateKind::EvalExpression,
                    eval_predicate_id,
                },
                {ConstraintValue{static_cast<int64_t>(21)}});
            check(runtime_eval_adult.has_value());
            check(*runtime_eval_adult);

            auto young = std::make_shared<Fact>();
            young->type = "Customer";
            young->fields["age"] = int64_t(17);

            auto adult = std::make_shared<Fact>();
            adult->type = "Customer";
            adult->fields["age"] = int64_t(21);

            session->add_fact(young);
            session->add_fact(adult);

            check(session->fire_all_rules() == 1);
        }

        it("lowers numeric eval math functions to MIR") {
            auto session = build_session(R"(
                declare Customer
                    delta: double
                    value: double
                    angle: double
                end
                rule "Eval Function Rule"
                when
                    Customer($delta : delta, $value : value, $angle : angle)
                    eval(abs($delta) == 3 && sqrt($value) == 4 && sin($angle) == 0 && cos($angle) == 1 && tan($angle) == 0 && pow($value, 2) == 256 && ceil($delta) == -3 && log(exp(2)) == 2 && acos(1) == 0 && asin(0) == 0 && atan(0) == 0 && log10(100) == 2 && round(2.4) == 2 && trunc(2.9) == 2 && sinh(0) == 0 && cosh(0) == 1 && tanh(0) == 0 && log2(8) == 3 && log1p(0) == 0 && expm1(0) == 0 && erf(0) == 0 && fmod(5, 2) == 1 && atan2(0, 1) == 0 && hypot(3, 4) == 5 && root(27, 3) == 3 && sgn($delta) == -1 && min($value, 10) == 10 && max($value, 20) == 20)
                then
                end
            )");
            check(session->get_knowledge_base()->mir_summary().eval_expression_predicate_count == 1);
            check(session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());

            auto matching = std::make_shared<Fact>();
            matching->type = "Customer";
            matching->fields["delta"] = -3.0;
            matching->fields["value"] = 16.0;
            matching->fields["angle"] = 0.0;

            auto missing = std::make_shared<Fact>();
            missing->type = "Customer";
            missing->fields["delta"] = 2.0;
            missing->fields["value"] = 16.0;
            missing->fields["angle"] = 0.0;

            session->add_fact(matching);
            session->add_fact(missing);

            check(session->fire_all_rules() == 1);
        }

        it("lowers wide numeric eval predicates to MIR") {
            auto session = build_session(R"(
                declare Customer
                    a: double
                    b: double
                    c: double
                    d: double
                    e: double
                    f: double
                    g: double
                    h: double
                    i: double
                end
                rule "Eval Wide Rule"
                when
                    Customer($a : a, $b : b, $c : c, $d : d, $e : e, $f : f, $g : g, $h : h, $i : i)
                    eval(($a + $b + $c + $d + $e + $f + $g + $h + $i) == 45)
                then
                end
            )");
            check(session->get_knowledge_base()->mir_summary().eval_expression_predicate_count == 1);
            check(session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());

            auto matching = std::make_shared<Fact>();
            matching->type = "Customer";
            matching->fields["a"] = 1.0;
            matching->fields["b"] = 2.0;
            matching->fields["c"] = 3.0;
            matching->fields["d"] = 4.0;
            matching->fields["e"] = 5.0;
            matching->fields["f"] = 6.0;
            matching->fields["g"] = 7.0;
            matching->fields["h"] = 8.0;
            matching->fields["i"] = 9.0;

            auto missing = std::make_shared<Fact>();
            missing->type = "Customer";
            missing->fields = matching->fields;
            missing->fields["i"] = 10.0;

            session->add_fact(matching);
            session->add_fact(missing);

            check(session->fire_all_rules() == 1);
        }

        it("lowers boolean-composed numeric eval predicates to MIR") {
            auto session = build_session(R"(
                declare Customer
                    age: int
                    score: double
                end
                rule "Eval Boolean Rule"
                when
                    Customer($age : age, $score : score)
                    eval($age >= 18 && $score >= 10)
                then
                end
            )");
            check(session->get_knowledge_base()->mir_summary().eval_expression_predicate_count == 1);

            auto too_young = std::make_shared<Fact>();
            too_young->type = "Customer";
            too_young->fields["age"] = int64_t(17);
            too_young->fields["score"] = 20.0;

            auto low_score = std::make_shared<Fact>();
            low_score->type = "Customer";
            low_score->fields["age"] = int64_t(22);
            low_score->fields["score"] = 9.0;

            auto matching = std::make_shared<Fact>();
            matching->type = "Customer";
            matching->fields["age"] = int64_t(22);
            matching->fields["score"] = 10.0;

            session->add_fact(too_young);
            session->add_fact(low_score);
            session->add_fact(matching);

            check(session->fire_all_rules() == 1);
        }

        it("lowers eval disjunction to MIR") {
            auto session = build_session(R"(
                declare Customer
                    age: int
                    score: double
                end
                rule "Eval Or Rule"
                when
                    Customer($age : age, $score : score)
                    eval($age >= 65 || $score >= 10)
                then
                end
            )");
            check(session->get_knowledge_base()->mir_summary().eval_expression_predicate_count == 1);

            auto low_score = std::make_shared<Fact>();
            low_score->type = "Customer";
            low_score->fields["age"] = int64_t(30);
            low_score->fields["score"] = 9.0;

            auto score_match = std::make_shared<Fact>();
            score_match->type = "Customer";
            score_match->fields["age"] = int64_t(30);
            score_match->fields["score"] = 10.0;

            auto age_match = std::make_shared<Fact>();
            age_match->type = "Customer";
            age_match->fields["age"] = int64_t(70);
            age_match->fields["score"] = 1.0;

            session->add_fact(low_score);
            session->add_fact(score_match);
            session->add_fact(age_match);

            check(session->fire_all_rules() == 2);
        }

        it("lowers eval negation to MIR") {
            auto session = build_session(R"(
                declare Customer
                    age: int
                    score: double
                end
                rule "Eval Not Rule"
                when
                    Customer($age : age, $score : score)
                    eval(!($age < 18 || $score < 10))
                then
                end
            )");
            check(session->get_knowledge_base()->mir_summary().eval_expression_predicate_count == 1);
            check(session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());

            auto too_young = std::make_shared<Fact>();
            too_young->type = "Customer";
            too_young->fields["age"] = int64_t(17);
            too_young->fields["score"] = 20.0;

            auto low_score = std::make_shared<Fact>();
            low_score->type = "Customer";
            low_score->fields["age"] = int64_t(30);
            low_score->fields["score"] = 9.0;

            auto match = std::make_shared<Fact>();
            match->type = "Customer";
            match->fields["age"] = int64_t(30);
            match->fields["score"] = 10.0;

            session->add_fact(too_young);
            session->add_fact(low_score);
            session->add_fact(match);

            check(session->fire_all_rules() == 1);
        }

        it("hard-fails build for unsupported eval pattern semantics") {
            check_build_fails_with(R"(
                declare Customer
                    id: int
                end
                rule "Eval Rule"
                when
                    Customer($id : id)
                    eval(random() > 0)
                then
                end
            )", "eval_pattern_unlowered");
        }

        it("routes explicit native eval helpers through the native runtime backend") {
            ParsingResult result;
            auto kb = build_knowledge_base(R"(
                declare Customer
                    status: String
                end
                rule "Native Eval Rule"
                when
                    Customer($status : status)
                    eval(native.is_expected($status, "gold"))
                then
                end
            )", result);
            if (!result.success) throw_parse_failure(result);
            check(kb != nullptr);
            check(kb->mir_summary().eval_expression_predicate_count == 0);
            check(kb->mir_rule_coverages().size() == 1);
            check(kb->mir_rule_coverages()[0].lowering_errors.empty());
            check(kb->mir_rule_graphs().size() == 1);
            check(kb->mir_rule_graphs()[0].nodes.size() == 2);
            check(kb->mir_rule_graphs()[0].nodes[1].predicates.size() == 1);
            check(kb->mir_rule_graphs()[0].nodes[1].predicates[0].role
                == rulesforge::MirRulePredicateRole::Eval);
            auto const& ref = kb->mir_rule_graphs()[0].nodes[1].predicates[0].ref;
            check(ref.backend == rulesforge::RuntimePredicateBackend::Native);
            check(ref.kind == rulesforge::MirRuntimePredicateKind::External);
            check(ref.external_name == "is_expected");

            auto debug_dump = kb->mir_debug_dump();
            check(debug_dump.find("backend=native") != std::string::npos);
            check(debug_dump.find("external=is_expected") != std::string::npos);

            kb->register_native_predicate("is_expected", native_predicate_equals_two_args, nullptr);
            auto session = kb->create_session();
            check(session != nullptr);

            auto gold = std::make_shared<Fact>();
            gold->type = "Customer";
            gold->fields["status"] = std::string{"gold"};

            auto silver = std::make_shared<Fact>();
            silver->type = "Customer";
            silver->fields["status"] = std::string{"silver"};

            session->add_fact(gold);
            session->add_fact(silver);

            check(session->fire_all_rules() == 1);
        }

        it("routes namespaced native eval helpers through the native runtime backend") {
            ParsingResult result;
            auto kb = build_knowledge_base(R"(
                declare Customer
                    status: String
                end
                rule "Native Namespaced Eval Rule"
                when
                    Customer($status : status)
                    eval(native.fraud.is_expected($status, "gold"))
                then
                end
            )", result);
            if (!result.success) throw_parse_failure(result);
            check(kb != nullptr);
            check(kb->mir_rule_coverages().size() == 1);
            check(kb->mir_rule_coverages()[0].lowering_errors.empty());
            check(kb->mir_rule_graphs().size() == 1);
            check(kb->mir_rule_graphs()[0].nodes.size() == 2);
            check(kb->mir_rule_graphs()[0].nodes[1].predicates.size() == 1);

            auto const& ref = kb->mir_rule_graphs()[0].nodes[1].predicates[0].ref;
            check(ref.backend == rulesforge::RuntimePredicateBackend::Native);
            check(ref.kind == rulesforge::MirRuntimePredicateKind::External);
            check(ref.external_name == "fraud.is_expected");

            auto debug_dump = kb->mir_debug_dump();
            check(debug_dump.find("backend=native") != std::string::npos);
            check(debug_dump.find("external=fraud.is_expected") != std::string::npos);

            kb->register_native_predicate("fraud.is_expected", native_predicate_equals_two_args, nullptr);
            auto session = kb->create_session();
            check(session != nullptr);

            auto gold = std::make_shared<Fact>();
            gold->type = "Customer";
            gold->fields["status"] = std::string{"gold"};

            auto silver = std::make_shared<Fact>();
            silver->type = "Customer";
            silver->fields["status"] = std::string{"silver"};

            session->add_fact(gold);
            session->add_fact(silver);

            check(session->fire_all_rules() == 1);
        }

        it("passes string native eval helper arguments as raw scalar argv values") {
            ParsingResult result;
            auto kb = build_knowledge_base(R"(
                declare Customer
                    status: String
                end
                rule "Native Raw String Eval Rule"
                when
                    Customer($status : status)
                    eval(native.fraud.is_raw_gold($status))
                then
                end
            )", result);
            if (!result.success) throw_parse_failure(result);
            check(kb != nullptr);
            check(kb->mir_rule_coverages().size() == 1);
            check(kb->mir_rule_coverages()[0].lowering_errors.empty());

            auto const& ref = kb->mir_rule_graphs()[0].nodes[1].predicates[0].ref;
            check(ref.backend == rulesforge::RuntimePredicateBackend::Native);
            check(ref.external_name == "fraud.is_raw_gold");

            kb->register_native_predicate("fraud.is_raw_gold", native_predicate_first_arg_is_raw_gold, nullptr);
            auto session = kb->create_session();
            check(session != nullptr);

            auto gold = std::make_shared<Fact>();
            gold->type = "Customer";
            gold->fields["status"] = std::string{"gold"};

            auto quoted_gold = std::make_shared<Fact>();
            quoted_gold->type = "Customer";
            quoted_gold->fields["status"] = std::string{"\"gold\""};

            session->add_fact(gold);
            session->add_fact(quoted_gold);

            check(session->fire_all_rules() == 1);
        }

        it("routes dotted-field native eval helper arguments through the native runtime backend") {
            ParsingResult result;
            auto kb = build_knowledge_base(R"(
                declare Customer
                    status: String
                end
                rule "Native Dotted Eval Rule"
                when
                    $c : Customer()
                    eval(native.fraud.is_expected($c.status, "gold"))
                then
                end
            )", result);
            if (!result.success) throw_parse_failure(result);
            check(kb != nullptr);
            check(kb->mir_rule_coverages().size() == 1);
            check(kb->mir_rule_coverages()[0].lowering_errors.empty());
            check(kb->mir_rule_graphs().size() == 1);
            check(kb->mir_rule_graphs()[0].nodes.size() == 2);
            check(kb->mir_rule_graphs()[0].nodes[1].predicates.size() == 1);

            auto const& ref = kb->mir_rule_graphs()[0].nodes[1].predicates[0].ref;
            check(ref.backend == rulesforge::RuntimePredicateBackend::Native);
            check(ref.kind == rulesforge::MirRuntimePredicateKind::External);
            check(ref.external_name == "fraud.is_expected");

            kb->register_native_predicate("fraud.is_expected", native_predicate_equals_two_args, nullptr);
            auto session = kb->create_session();
            check(session != nullptr);

            auto gold = std::make_shared<Fact>();
            gold->type = "Customer";
            gold->fields["status"] = std::string{"gold"};

            auto silver = std::make_shared<Fact>();
            silver->type = "Customer";
            silver->fields["status"] = std::string{"silver"};

            session->add_fact(gold);
            session->add_fact(silver);

            check(session->fire_all_rules() == 1);
        }

        it("routes compound dotted-field native eval helper arguments through the native runtime backend") {
            ParsingResult result;
            auto kb = build_knowledge_base(R"(
                declare Profile
                    status: String
                end
                declare Customer
                    profile: FactList
                end
                rule "Native Compound Dotted Eval Rule"
                when
                    $c : Customer()
                    eval(native.fraud.is_expected($c.profile.status, "gold"))
                then
                end
            )", result);
            if (!result.success) throw_parse_failure(result);
            check(kb != nullptr);
            check(kb->mir_rule_coverages().size() == 1);
            check(kb->mir_rule_coverages()[0].lowering_errors.empty());
            check(kb->mir_rule_graphs().size() == 1);
            check(kb->mir_rule_graphs()[0].nodes.size() == 2);
            check(kb->mir_rule_graphs()[0].nodes[1].predicates.size() == 1);

            auto const& ref = kb->mir_rule_graphs()[0].nodes[1].predicates[0].ref;
            check(ref.backend == rulesforge::RuntimePredicateBackend::Native);
            check(ref.kind == rulesforge::MirRuntimePredicateKind::External);
            check(ref.external_name == "fraud.is_expected");

            kb->register_native_predicate("fraud.is_expected", native_predicate_equals_two_args, nullptr);
            auto session = kb->create_session();
            check(session != nullptr);

            auto gold_profile = std::make_shared<Fact>();
            gold_profile->type = "Profile";
            gold_profile->fields["status"] = std::string{"gold"};

            auto silver_profile = std::make_shared<Fact>();
            silver_profile->type = "Profile";
            silver_profile->fields["status"] = std::string{"silver"};

            auto gold = std::make_shared<Fact>();
            gold->type = "Customer";
            gold->fields["profile"] = FactList{{gold_profile.get()}};

            auto silver = std::make_shared<Fact>();
            silver->type = "Customer";
            silver->fields["profile"] = FactList{{silver_profile.get()}};

            session->add_fact(gold);
            session->add_fact(silver);

            check(session->fire_all_rules() == 1);
        }

        it("hard-fails runtime for unregistered native eval helper") {
            auto session = build_session(R"(
                declare Customer
                    status: String
                end
                rule "Native Eval Rule"
                when
                    Customer($status : status)
                    eval(native.is_expected($status, "gold"))
                then
                end
            )");

            check(session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());

            auto gold = std::make_shared<Fact>();
            gold->type = "Customer";
            gold->fields["status"] = std::string{"gold"};

            bool threw = false;
            try {
                session->add_fact(gold);
                (void)session->fire_all_rules();
            } catch (std::runtime_error const& e) {
                threw = true;
                auto const message = std::string(e.what());
                check(message.find("eval(...) predicate native 'is_expected' predicate failed at runtime")
                      != std::string::npos);
            }
            check(threw);
        }

        it("hard-fails build for deprecated dll eval helper prefix") {
            check_build_fails_with(R"(
                declare Customer
                    status: String
                end
                rule "Deprecated DLL Prefix Eval Rule"
                when
                    Customer($status : status)
                    eval(dll.fraud.is_expected($status, "gold"))
                then
                end
            )", "eval_pattern_unlowered");
        }

        it("routes native eval helper numeric expression arguments through MIR") {
            ParsingResult result;
            auto kb = build_knowledge_base(R"(
                declare Customer
                    age: int
                end
                rule "Native Numeric Eval Rule"
                when
                    Customer($age : age)
                    eval(native.numeric_equals($age + 1, 19))
                then
                end
            )", result);
            if (!result.success) throw_parse_failure(result);
            check(kb != nullptr);
            check(kb->mir_rule_coverages().size() == 1);
            check(kb->mir_rule_coverages()[0].lowering_errors.empty());
            check(kb->mir_summary().numeric_value_expression_count == 1);
            check(kb->mir_numeric_value_expression_id("$age + 1").has_value());

            kb->register_native_predicate("numeric_equals", native_predicate_numeric_equals_two_args, nullptr);
            auto session = kb->create_session();
            check(session != nullptr);

            auto matching = std::make_shared<Fact>();
            matching->type = "Customer";
            matching->fields["age"] = int64_t(18);

            auto missing = std::make_shared<Fact>();
            missing->type = "Customer";
            missing->fields["age"] = int64_t(20);

            session->add_fact(matching);
            session->add_fact(missing);

            check(session->fire_all_rules() == 1);
        }

        it("fails build for unsupported native eval helper argument shapes") {
            check_build_fails_with(R"(
                declare Customer
                    age: int
                end
                rule "Invalid Native Eval Rule"
                when
                    Customer($age : age)
                    eval(native.is_expected(random()))
                then
                end
            )", "eval_pattern_unlowered");
        }

        it("dumps MIR predicate ids in network DOT") {
            ParsingResult result;
            auto kb = build_knowledge_base(R"(
                declare Customer
                    status: String
                end
                declare Order
                    requiredStatus: String
                end
                rule "Find Higher Status Orders"
                when
                    $c : Customer(status > "Gold", $status : status)
                    $o : Order(requiredStatus > $status)
                then
                end
            )", result);

            if (!result.success) throw_parse_failure(result);
            check(kb != nullptr);
            auto dot = kb->network_dot();
            check(dot.find("MIR predicate:") != std::string::npos);
            check(dot.find("[mir#") != std::string::npos);
        }

        it("reports RHS TurboScript backend coverage for linear command scripts") {
            ParsingResult result;
            auto kb = build_knowledge_base(R"(
                declare Person
                    age: int
                end
                declare Adult
                    age: int
                end
                rule "Find Adults"
                when
                    $p : Person(age >= 18)
                then
                    insert Adult { age = $p.age }
                end
            )", result);

            if (!result.success) throw_parse_failure(result);
            check(kb != nullptr);
            auto summary = kb->rhs_backend_summary();
            check(summary.turboscript_available);
            check(summary.rule_coverage_count == 1);
            check(summary.turboscript_mir_rule_count == 1);
            check(summary.compile_error_rule_count == 0);
            check(summary.command_count == 1);
            check(summary.condition_count == 0);
            auto const& coverages = kb->rhs_backend_coverages();
            check(coverages.size() == 1);
            check(coverages[0].rule_name == "Find Adults");
            check(coverages[0].backend == rulesforge::RhsBackendKind::TurboScriptMir);
            check(coverages[0].command_count == 1);
            check(coverages[0].condition_count == 0);
            check(coverages[0].lowering_errors.empty());
            auto debug_dump = kb->rhs_backend_debug_dump();
            check(debug_dump.find("RHS backend summary: turboscript_available=true") != std::string::npos);
            check(debug_dump.find("backend=turboscript_mir") != std::string::npos);
            check(debug_dump.find("commands=1") != std::string::npos);
            check(debug_dump.find("conditions=0") != std::string::npos);
            check(debug_dump.find("lowering_errors=none") != std::string::npos);
        }

        it("reports RHS if branch control as TurboScript command script") {
            ParsingResult result;
            auto kb = build_knowledge_base(R"(
                declare Person
                    age: int
                    kind: String
                end
                rule "Classify"
                when
                    $p : Person(age >= 18)
                then
                    if $p.age > 65 {
                        update $p { kind = "senior" }
                    } else {
                        update $p { kind = "adult" }
                    }
                end
            )", result);

            if (!result.success) throw_parse_failure(result);
            check(kb != nullptr);
            auto summary = kb->rhs_backend_summary();
            check(summary.turboscript_available);
            check(summary.rule_coverage_count == 1);
            check(summary.turboscript_mir_rule_count == 1);
            check(summary.command_count == 2);
            check(summary.condition_count == 1);
            auto const& coverages = kb->rhs_backend_coverages();
            check(coverages.size() == 1);
            check(coverages[0].backend == rulesforge::RhsBackendKind::TurboScriptMir);
            check(coverages[0].command_count == 2);
            check(coverages[0].condition_count == 1);
            check(coverages[0].lowering_errors.empty());
            auto debug_dump = kb->rhs_backend_debug_dump();
            check(debug_dump.find("commands=2") != std::string::npos);
            check(debug_dump.find("conditions=1") != std::string::npos);
        }

        it("reports RHS else-if branch control as nested TurboScript conditions") {
            ParsingResult result;
            auto kb = build_knowledge_base(R"(
                declare Item
                    price: int
                    tier: String
                end
                rule "Classify"
                when
                    $item : Item(tier == "")
                then
                    if $item.price > 100 {
                        update $item { tier = "premium" }
                    } else if $item.price > 50 {
                        update $item { tier = "standard" }
                    } else {
                        update $item { tier = "budget" }
                    }
                end
            )", result);

            if (!result.success) throw_parse_failure(result);
            check(kb != nullptr);
            auto summary = kb->rhs_backend_summary();
            check(summary.turboscript_available);
            check(summary.rule_coverage_count == 1);
            check(summary.turboscript_mir_rule_count == 1);
            check(summary.command_count == 3);
            check(summary.condition_count == 2);
            auto const& coverages = kb->rhs_backend_coverages();
            check(coverages.size() == 1);
            check(coverages[0].backend == rulesforge::RhsBackendKind::TurboScriptMir);
            check(coverages[0].command_count == 3);
            check(coverages[0].condition_count == 2);
            check(coverages[0].lowering_errors.empty());
            auto summary_text = kb->rhs_backend_summary_text();
            check(summary_text.find("commands=3") != std::string::npos);
            check(summary_text.find("conditions=2") != std::string::npos);
        }

        it("reports simple RHS for loops as TurboScript command script") {
            ParsingResult result;
            auto kb = build_knowledge_base(R"(
                declare Item
                    value: int
                end
                declare Container
                    items: List<Item>
                end
                declare Result
                    value: int
                end
                rule "For Loop"
                when
                    $c : Container()
                then
                    for $item in $c.items {
                        insert Result { value = $item.value }
                    }
                end
            )", result);

            if (!result.success) throw_parse_failure(result);
            check(kb != nullptr);
            auto summary = kb->rhs_backend_summary();
            check(summary.turboscript_available);
            check(summary.rule_coverage_count == 1);
            check(summary.turboscript_mir_rule_count == 1);
            check(summary.compile_error_rule_count == 0);
            auto const& coverages = kb->rhs_backend_coverages();
            check(coverages.size() == 1);
            check(coverages[0].backend == rulesforge::RhsBackendKind::TurboScriptMir);
            check(coverages[0].command_count == 1);
            check(coverages[0].lowering_errors.empty());
        }

        it("reports RHS while loops as TurboScript command script") {
            ParsingResult result;
            auto kb = build_knowledge_base(R"(
                declare Counter
                    count: int
                end
                rule "Countdown"
                when
                    $c : Counter(count > 0)
                then
                    while $c.count > 0 {
                        update $c { count = $c.count - 1 }
                    }
                end
            )", result);

            if (!result.success) throw_parse_failure(result);
            check(kb != nullptr);
            auto summary = kb->rhs_backend_summary();
            check(summary.turboscript_available);
            check(summary.rule_coverage_count == 1);
            check(summary.turboscript_mir_rule_count == 1);
            check(summary.compile_error_rule_count == 0);
            auto const& coverages = kb->rhs_backend_coverages();
            check(coverages.size() == 1);
            check(coverages[0].backend == rulesforge::RhsBackendKind::TurboScriptMir);
            check(coverages[0].command_count == 1);
            check(coverages[0].lowering_errors.empty());
        }

        it("reports RHS switch control flow as TurboScript command script") {
            ParsingResult result;
            auto kb = build_knowledge_base(R"(
                declare Order
                    level: int
                    discount: int
                end
                rule "Switch Discount"
                when
                    $o : Order(discount == 0)
                then
                    switch $o.level {
                        case 1 {
                            update $o { discount = 10 }
                        }
                        case 2 {
                            update $o { discount = 20 }
                        }
                        default {
                            update $o { discount = 5 }
                        }
                    }
                end
            )", result);

            if (!result.success) throw_parse_failure(result);
            check(kb != nullptr);
            auto summary = kb->rhs_backend_summary();
            check(summary.turboscript_available);
            check(summary.rule_coverage_count == 1);
            check(summary.turboscript_mir_rule_count == 1);
            check(summary.compile_error_rule_count == 0);
            auto const& coverages = kb->rhs_backend_coverages();
            check(coverages.size() == 1);
            check(coverages[0].backend == rulesforge::RhsBackendKind::TurboScriptMir);
            check(coverages[0].command_count == 3);
            check(coverages[0].condition_count == 2);
            check(coverages[0].lowering_errors.empty());
        }

        it("reports RHS native invoke as an external side effect") {
            ParsingResult result;
            auto kb = build_knowledge_base(R"(
                declare Sensor
                    value: double
                end
                rule "Invoke"
                when
                    $s : Sensor(value > 10)
                then
                    invoke capture($s.value)
                end
            )", result);

            if (!result.success) throw_parse_failure(result);
            check(kb != nullptr);
            auto summary = kb->rhs_backend_summary();
            check(summary.external_side_effect_count == 1);
            check(summary.external_side_effect_names.size() == 1);
            check(summary.external_side_effect_names[0] == "capture");
            auto summary_text = kb->rhs_backend_summary_text();
            check(summary_text.find("external_side_effects=1") != std::string::npos);
            check(summary_text.find("external_side_effect_names=capture") != std::string::npos);
            auto const& coverages = kb->rhs_backend_coverages();
            check(coverages.size() == 1);
            check(coverages[0].external_side_effect_count == 1);
            check(coverages[0].external_side_effect_names.size() == 1);
            check(coverages[0].external_side_effect_names[0] == "capture");
            auto debug_dump = kb->rhs_backend_debug_dump();
            check(debug_dump.find("external_side_effects=1") != std::string::npos);
            check(debug_dump.find("external_side_effect_names=capture") != std::string::npos);
        }

        it("treats missing TurboScript condition callback as false at the adapter boundary") {
            CompiledAction then_action;
            then_action.type = RhsActionType::INSERT;
            then_action.target_type = "ThenBranch";

            CompiledAction else_action;
            else_action.type = RhsActionType::INSERT;
            else_action.target_type = "ElseBranch";

            CompiledAction if_action;
            if_action.type = RhsActionType::IF;
            if_action.condition = rulesforge::ExpressionDescriptor::compile("1 > 0");
            if_action.then_actions.push_back(then_action);
            if_action.else_actions.push_back(else_action);

            std::vector<CompiledAction> actions{if_action};
            rulesforge::TurboScriptRhsCommandScript script;
            std::string error;
            check(rulesforge::TurboScriptRhsAdapter::build_command_script(actions, script, &error));
            check(script.command_actions.size() == 2);
            check(script.condition_actions.size() == 1);

            std::vector<std::size_t> command_indices;
            check(rulesforge::TurboScriptRhsAdapter::execute_command_script(script, command_indices, &error));
            check(command_indices.size() == 1);
            check(command_indices[0] == 1);

            auto program = rulesforge::TurboScriptRhsProgram::compile(script.script, &error);
            check(program != nullptr);
            std::vector<rulesforge::TurboScriptRhsCommandEvent> events;
            std::function<bool(std::size_t)> no_eval;
            check(program->execute(events, no_eval, &error));
            check(events.size() == 1);
            check(events[0].type == rulesforge::TurboScriptRhsCommandEventType::Command);
            check(events[0].action_index == 1);
            check(events[0].temp_bindings.empty());
        }

        it("collects TurboScript RHS loop-control events without exposing them as command indices") {
            std::string error;
            auto program = rulesforge::TurboScriptRhsProgram::compile(
                "__rulesforge_rhs_cmd(7); __rulesforge_rhs_break(); __rulesforge_rhs_continue();",
                &error);
            check(program != nullptr);

            std::vector<rulesforge::TurboScriptRhsCommandEvent> events;
            std::function<bool(std::size_t)> no_eval;
            check(program->execute(events, no_eval, &error));
            check(events.size() == 3);
            check(events[0].type == rulesforge::TurboScriptRhsCommandEventType::Command);
            check(events[0].action_index == 7);
            check(events[1].type == rulesforge::TurboScriptRhsCommandEventType::Break);
            check(events[2].type == rulesforge::TurboScriptRhsCommandEventType::Continue);

            std::vector<std::size_t> command_indices;
            check(program->execute(command_indices, no_eval, &error));
            check(command_indices.size() == 1);
            check(command_indices[0] == 7);
        }

        it("clears partial TurboScript command script when adapter build fails") {
            CompiledAction insert_action;
            insert_action.type = RhsActionType::INSERT;
            insert_action.target_type = "AcceptedPrefix";

            CompiledAction break_action;
            break_action.type = RhsActionType::BREAK;

            std::vector<CompiledAction> actions{insert_action, break_action};
            rulesforge::TurboScriptRhsCommandScript script;
            std::string reason;

            check(!rulesforge::TurboScriptRhsAdapter::build_command_script(actions, script, &reason));
            check(reason == "control_flow_unsupported");
            check(script.script.empty());
            check(script.command_actions.empty());
            check(script.for_actions.empty());
            check(script.while_actions.empty());
            check(script.condition_actions.empty());
            check(script.condition_switch_case_indices.empty());
        }

        it("rejects unsupported TurboScript control-flow actions without partial output") {
            for (auto type : {RhsActionType::BREAK,
                              RhsActionType::CONTINUE}) {
                CompiledAction action;
                action.type = type;
                std::vector<CompiledAction> actions{action};
                rulesforge::TurboScriptRhsCommandScript script;
                std::string reason;

                check(!rulesforge::TurboScriptRhsAdapter::build_command_script(actions, script, &reason));
                check(reason == "control_flow_unsupported");
                check(script.script.empty());
                check(script.command_actions.empty());
                check(script.for_actions.empty());
                check(script.while_actions.empty());
                check(script.condition_actions.empty());
                check(script.condition_switch_case_indices.empty());
            }
        }

        it("builds TurboScript command script for simple for loops") {
            CompiledAction insert_action;
            insert_action.type = RhsActionType::INSERT;
            insert_action.target_type = "Result";

            CompiledAction for_action;
            for_action.type = RhsActionType::FOR;
            for_action.iter_var = "$item";
            for_action.iter_source_var = "$items";
            for_action.body_actions.push_back(insert_action);

            std::vector<CompiledAction> actions{for_action};
            rulesforge::TurboScriptRhsCommandScript script;
            std::string reason;

            check(rulesforge::TurboScriptRhsAdapter::build_command_script(actions, script, &reason));
            check(script.command_actions.empty());
            check(script.for_actions.size() == 1);
            check(script.while_actions.empty());
            check(script.script.find("__rulesforge_rhs_for(0)") != std::string::npos);
        }

        it("builds TurboScript command script for for loops with loop-control body actions") {
            CompiledAction break_action;
            break_action.type = RhsActionType::BREAK;

            CompiledAction for_action;
            for_action.type = RhsActionType::FOR;
            for_action.iter_var = "$item";
            for_action.iter_source_var = "$items";
            for_action.body_actions.push_back(break_action);

            std::vector<CompiledAction> actions{for_action};
            rulesforge::TurboScriptRhsCommandScript script;
            std::string reason;

            check(rulesforge::TurboScriptRhsAdapter::build_command_script(actions, script, &reason));
            check(script.command_actions.empty());
            check(script.for_actions.size() == 1);
            check(script.while_actions.empty());
            check(script.condition_actions.empty());
            check(script.condition_switch_case_indices.empty());
            check(script.script.find("__rulesforge_rhs_for(0)") != std::string::npos);
        }

        it("builds TurboScript command script for while loops") {
            CompiledAction update_action;
            update_action.type = RhsActionType::UPDATE;
            update_action.target_var = "$c";

            CompiledAction while_action;
            while_action.type = RhsActionType::WHILE;
            while_action.condition = rulesforge::ExpressionDescriptor::compile("$c.count < $c.limit");
            while_action.body_actions.push_back(update_action);

            std::vector<CompiledAction> actions{while_action};
            rulesforge::TurboScriptRhsCommandScript script;
            std::string reason;

            check(rulesforge::TurboScriptRhsAdapter::build_command_script(actions, script, &reason));
            check(script.command_actions.empty());
            check(script.for_actions.empty());
            check(script.while_actions.size() == 1);
            check(script.condition_actions.empty());
            check(script.condition_switch_case_indices.empty());
            check(script.script.find("__rulesforge_rhs_while(0)") != std::string::npos);
        }

        it("builds TurboScript command script for switch control flow") {
            CompiledAction case_action;
            case_action.type = RhsActionType::UPDATE;
            case_action.target_var = "$o";

            CompiledAction second_case_action;
            second_case_action.type = RhsActionType::UPDATE;
            second_case_action.target_var = "$o";

            CompiledAction default_action;
            default_action.type = RhsActionType::UPDATE;
            default_action.target_var = "$o";

            CompiledAction switch_action;
            switch_action.type = RhsActionType::SWITCH;
            switch_action.switch_expr = rulesforge::ExpressionDescriptor::compile("$o.level");
            SwitchCase case_one;
            case_one.value = rulesforge::ExpressionDescriptor::compile("1");
            case_one.actions.push_back(case_action);
            SwitchCase case_two;
            case_two.value = rulesforge::ExpressionDescriptor::compile("2");
            case_two.actions.push_back(second_case_action);
            SwitchCase default_case;
            default_case.is_default = true;
            default_case.actions.push_back(default_action);
            switch_action.switch_cases.push_back(case_one);
            switch_action.switch_cases.push_back(case_two);
            switch_action.switch_cases.push_back(default_case);

            std::vector<CompiledAction> actions{switch_action};
            rulesforge::TurboScriptRhsCommandScript script;
            std::string reason;

            check(rulesforge::TurboScriptRhsAdapter::build_command_script(actions, script, &reason));
            check(script.command_actions.size() == 3);
            check(script.condition_actions.size() == 2);
            check(script.condition_switch_case_indices.size() == 2);
            check(script.condition_switch_case_indices[0] == 0);
            check(script.condition_switch_case_indices[1] == 1);
            check(script.script.find("__rulesforge_rhs_eval(0)") != std::string::npos);
            check(script.script.find("__rulesforge_rhs_eval(1)") != std::string::npos);
        }

        it("clears TurboScript command indices when adapter execute fails") {
            rulesforge::TurboScriptRhsCommandScript script;
            script.script = "if (";

            std::vector<std::size_t> command_indices{42};
            std::string error;

            check(!rulesforge::TurboScriptRhsAdapter::execute_command_script(script, command_indices, &error));
            check(command_indices.empty());
        }

        it("clears TurboScript expression outputs when adapter evaluation fails") {
            auto resolver = [](std::string const&) -> ConstraintValue { return int64_t(1); };

            ConstraintValue expression_value = int64_t(42);
            std::string error;
            check(!rulesforge::TurboScriptRhsAdapter::evaluate_expression(
                "if (",
                {},
                resolver,
                expression_value,
                &error));
            check(std::holds_alternative<NilValue>(expression_value));

            bool boolean_value = true;
            check(!rulesforge::TurboScriptRhsAdapter::evaluate_boolean_expression(
                "if (",
                {},
                resolver,
                boolean_value,
                &error));
            check(!boolean_value);
        }
    }

    group("Simple Rule Fire") {
        it("fires rule and inserts fact") {
            auto session = build_session(R"(
                declare Person
                    name: String
                    age: int
                end
                declare Adult
                    name: String
                end
                rule "Find Adults"
                when
                    $p : Person(age >= 18)
                then
                    insert Adult { name = $p.name }
                end
            )");

            auto person = std::make_shared<Fact>();
            person->type = "Person";
            person->fields["name"] = "John";
            person->fields["age"] = (int64_t)25;

            session->add_fact(person);
            rulesforge::rhs_prof::reset_stats();
            int fired = session->fire_all_rules();
            auto stats = rulesforge::rhs_prof::get_stats();

            check(fired == 1);
            check(session->get_fact_count() == 2);
            check(stats.turboscript_command_exec_count == 1);
            check(stats.turboscript_command_error_count == 0);
        }

        it("fires linear multi-action RHS through TurboScript command script") {
            auto session = build_session(R"(
                declare Person
                    name: String
                    age: int
                end
                declare Adult
                    name: String
                end
                declare Audit
                    name: String
                end
                rule "Find Adults With Audit"
                when
                    $p : Person(age >= 18)
                then
                    insert Adult { name = $p.name }
                    insert Audit { name = $p.name }
                end
            )");

            auto person = std::make_shared<Fact>();
            person->type = "Person";
            person->fields["name"] = "John";
            person->fields["age"] = (int64_t)25;

            session->add_fact(person);
            rulesforge::rhs_prof::reset_stats();
            int fired = session->fire_all_rules();
            auto stats = rulesforge::rhs_prof::get_stats();

            check(fired == 1);
            check(session->get_fact_count() == 3);
            check(stats.turboscript_command_exec_count == 1);
            check(stats.turboscript_command_error_count == 0);
            check(stats.turboscript_expression_error_count == 0);
        }

        it("assigns bare fact references as fact ids through RHS value binding") {
            auto session = build_session(R"(
                declare Person
                    name: String
                    age: int
                end
                declare Ref
                    factId: int
                end
                rule "Capture Fact Id"
                when
                    $p : Person(age >= 18)
                then
                    insert Ref { factId = $p }
                end
            )");

            auto person = std::make_shared<Fact>();
            person->type = "Person";
            person->fields["name"] = "John";
            person->fields["age"] = (int64_t)25;

            session->add_fact(person);
            rulesforge::rhs_prof::reset_stats();
            int fired = session->fire_all_rules();
            auto stats = rulesforge::rhs_prof::get_stats();

            check(fired == 1);
            check(stats.turboscript_command_exec_count == 1);
            check(stats.turboscript_command_error_count == 0);
            Fact* ref = nullptr;
            for (int64_t id = 1; id <= session->get_next_fact_id(); ++id) {
                Fact* fact = session->get_fact_by_id(id);
                if (fact && fact->type == "Ref") {
                    ref = fact;
                    break;
                }
            }
            check(ref != nullptr);
            check(std::holds_alternative<int64_t>(ref->fields["factId"]));
            check(std::get<int64_t>(ref->fields["factId"]) == person->id);
        }

        it("executes if branches with multiple commands through TurboScript command script") {
            auto session = build_session(R"(
                declare Person
                    name: String
                    age: int
                    tier: String
                    processed: int
                end
                declare Audit
                    label: String
                end
                rule "Classify With Audit"
                when
                    $p : Person(age >= 18, processed == 0)
                then
                    if $p.age >= 65 {
                        update $p { tier = "senior", processed = 1 }
                        insert Audit { label = "senior" }
                    } else {
                        update $p { tier = "adult", processed = 1 }
                        insert Audit { label = "adult" }
                    }
                end
            )");

            auto person = std::make_shared<Fact>();
            person->type = "Person";
            person->fields["name"] = "Jane";
            person->fields["age"] = int64_t(70);
            person->fields["tier"] = std::string("");
            person->fields["processed"] = int64_t(0);

            session->add_fact(person);
            rulesforge::rhs_prof::reset_stats();
            int fired = session->fire_all_rules();
            auto stats = rulesforge::rhs_prof::get_stats();

            check(fired == 1);
            check(session->get_fact_count() == 2);
            check(std::get<std::string>(person->fields["tier"]) == "senior");
            check(std::get<int64_t>(person->fields["processed"]) == 1);
            check(stats.turboscript_command_exec_count == 1);
            check(stats.turboscript_command_error_count == 0);
            check(stats.turboscript_expression_exec_count >= 1);
        }

        it("executes else-if branches through TurboScript command script") {
            auto session = build_session(R"(
                declare Item
                    price: int
                    tier: String
                end
                rule "Classify"
                when
                    $item : Item(tier == "")
                then
                    if $item.price > 100 {
                        update $item { tier = "premium" }
                    } else if $item.price > 50 {
                        update $item { tier = "standard" }
                    } else {
                        update $item { tier = "budget" }
                    }
                end
            )");

            auto premium = std::make_shared<Fact>();
            premium->type = "Item";
            premium->fields["price"] = int64_t(200);
            premium->fields["tier"] = std::string("");

            auto standard = std::make_shared<Fact>();
            standard->type = "Item";
            standard->fields["price"] = int64_t(75);
            standard->fields["tier"] = std::string("");

            auto budget = std::make_shared<Fact>();
            budget->type = "Item";
            budget->fields["price"] = int64_t(20);
            budget->fields["tier"] = std::string("");

            session->add_fact(premium);
            session->add_fact(standard);
            session->add_fact(budget);

            rulesforge::rhs_prof::reset_stats();
            int fired = session->fire_all_rules();
            auto stats = rulesforge::rhs_prof::get_stats();

            check(fired == 3);
            check(std::get<std::string>(premium->fields["tier"]) == "premium");
            check(std::get<std::string>(standard->fields["tier"]) == "standard");
            check(std::get<std::string>(budget->fields["tier"]) == "budget");
            check(stats.turboscript_command_exec_count == 3);
            check(stats.turboscript_command_error_count == 0);
            check(stats.turboscript_expression_exec_count >= 3);
        }

        it("executes single switch RHS through TurboScript command script") {
            auto session = build_session(R"(
                declare Order
                    level: int
                    discount: int
                end
                rule "Switch Discount"
                when
                    $o : Order(discount == 0)
                then
                    switch $o.level {
                        case 1 {
                            update $o { discount = 10 }
                        }
                        case 2 {
                            update $o { discount = 20 }
                        }
                        default {
                            update $o { discount = 5 }
                        }
                    }
                end
            )");

            auto order = std::make_shared<Fact>();
            order->type = "Order";
            order->fields["level"] = int64_t(1);
            order->fields["discount"] = int64_t(0);

            auto second = std::make_shared<Fact>();
            second->type = "Order";
            second->fields["level"] = int64_t(2);
            second->fields["discount"] = int64_t(0);

            auto defaulted = std::make_shared<Fact>();
            defaulted->type = "Order";
            defaulted->fields["level"] = int64_t(3);
            defaulted->fields["discount"] = int64_t(0);

            session->add_fact(order);
            session->add_fact(second);
            session->add_fact(defaulted);
            rulesforge::rhs_prof::reset_stats();
            int fired = session->fire_all_rules();
            auto stats = rulesforge::rhs_prof::get_stats();

            check(fired == 3);
            check(std::get<int64_t>(order->fields["discount"]) == 10);
            check(std::get<int64_t>(second->fields["discount"]) == 20);
            check(std::get<int64_t>(defaulted->fields["discount"]) == 5);
            check(stats.turboscript_command_exec_count == 3);
            check(stats.turboscript_command_error_count == 0);
        }

        it("executes single while RHS through TurboScript command script at runtime") {
            auto session = build_session(R"(
                declare Counter
                    count: int
                    limit: int
                end
                rule "While Counter"
                when
                    $c : Counter(count == 0)
                then
                    while $c.count < $c.limit {
                        update $c { count = $c.count + 1 }
                    }
                end
            )");

            auto counter = std::make_shared<Fact>();
            counter->type = "Counter";
            counter->fields["count"] = int64_t(0);
            counter->fields["limit"] = int64_t(2);

            session->add_fact(counter);
            rulesforge::rhs_prof::reset_stats();
            int fired = session->fire_all_rules();
            auto stats = rulesforge::rhs_prof::get_stats();

            check(fired == 1);
            check(std::get<int64_t>(counter->fields["count"]) == 2);
            check(stats.turboscript_command_exec_count >= 1);
            check(stats.turboscript_command_error_count == 0);
        }

        it("executes simple single for RHS through TurboScript command script at runtime") {
            auto session = build_session(R"(
                declare Item
                    value: int
                end
                declare Container
                    items: List<Item>
                end
                declare Result
                    value: int
                end
                rule "For Results"
                when
                    $c : Container()
                then
                    for $item in $c.items {
                        insert Result { value = $item.value }
                    }
                end
            )");

            auto first = std::make_shared<Fact>();
            first->type = "Item";
            first->fields["value"] = int64_t(10);

            auto second = std::make_shared<Fact>();
            second->type = "Item";
            second->fields["value"] = int64_t(20);

            auto container = std::make_shared<Fact>();
            container->type = "Container";
            container->fields["items"] = FactList{{first.get(), second.get()}};

            session->add_fact(first);
            session->add_fact(second);
            session->add_fact(container);
            rulesforge::rhs_prof::reset_stats();
            int fired = session->fire_all_rules();
            auto stats = rulesforge::rhs_prof::get_stats();

            check(fired == 1);
            check(session->get_fact_count() == 5);
            std::vector<int64_t> result_values;
            for (int64_t id = 1; id <= session->get_next_fact_id(); ++id) {
                Fact* fact = session->get_fact_by_id(id);
                if (fact && fact->type == "Result") {
                    auto it = fact->fields.find("value");
                    if (it != fact->fields.end() && std::holds_alternative<int64_t>(it->second)) {
                        result_values.push_back(std::get<int64_t>(it->second));
                    }
                }
            }
            std::sort(result_values.begin(), result_values.end());
            check(result_values.size() == 2);
            check(result_values[0] == 10);
            check(result_values[1] == 20);
            check(stats.turboscript_command_exec_count >= 1);
            check(stats.turboscript_command_error_count == 0);
        }

        it("assigns scalar int double and string values through TurboScript expression path") {
            auto session = build_session(R"(
                declare Order
                    quantity: int
                    unitPrice: double
                    total: double
                    label: String
                    count: int
                end
                rule "Calculate Order"
                when
                    $o : Order(count == 0)
                then
                    update $o {
                        total = $o.quantity * $o.unitPrice,
                        label = "calculated",
                        count = $o.quantity + 1
                    }
                end
            )");

            auto order = std::make_shared<Fact>();
            order->type = "Order";
            order->fields["quantity"] = int64_t(3);
            order->fields["unitPrice"] = 12.5;
            order->fields["total"] = 0.0;
            order->fields["label"] = std::string("");
            order->fields["count"] = int64_t(0);

            session->add_fact(order);
            rulesforge::rhs_prof::reset_stats();
            int fired = session->fire_all_rules();
            auto stats = rulesforge::rhs_prof::get_stats();

            check(fired == 1);
            check(std::holds_alternative<double>(order->fields["total"]));
            check(std::abs(std::get<double>(order->fields["total"]) - 37.5) < 0.0001);
            check(std::get<std::string>(order->fields["label"]) == "calculated");
            check(std::get<int64_t>(order->fields["count"]) == 4);
            check(stats.turboscript_expression_exec_count >= 2);
            check(stats.turboscript_expression_non_scalar_error_count == 0);
        }

        it("assigns non-scalar globals directly without using the expression backend") {
            auto session = build_session(R"(
                global List results

                declare Trigger
                    flag: int
                end
                declare Snapshot
                    items: List<int>
                    itemCount: int
                end
                rule "Use Global List"
                when
                    $t : Trigger(flag == 1)
                then
                    insert Snapshot { items = $results, itemCount = $results.size }
                end
            )");

            session->set_global("results", make_typed_list({int64_t(10), int64_t(20)}));

            auto trigger = std::make_shared<Fact>();
            trigger->type = "Trigger";
            trigger->fields["flag"] = int64_t(1);
            session->add_fact(trigger);

            rulesforge::rhs_prof::reset_stats();
            int fired = session->fire_all_rules();
            auto stats = rulesforge::rhs_prof::get_stats();

            Fact* snapshot = nullptr;
            for (int64_t id = 1; id <= session->get_next_fact_id(); ++id) {
                Fact* fact = session->get_fact_by_id(id);
                if (fact && fact->type == "Snapshot") {
                    snapshot = fact;
                    break;
                }
            }

            check(fired == 1);
            check(snapshot != nullptr);
            auto list_field = snapshot->get_field("items");
            check(list_field.has_value());
            auto list_ptr = std::get_if<std::shared_ptr<TypedList>>(&*list_field);
            check(list_ptr != nullptr);
            check(*list_ptr != nullptr);
            check((*list_ptr)->values.size() == 2);
            auto item_count = snapshot->get_field("itemCount");
            check(item_count.has_value());
            check(std::get<int64_t>(*item_count) == 2);
            check(stats.turboscript_expression_non_scalar_error_count == 0);
        }

        it("filters alpha int fields with non-equality comparison") {
            auto session = build_session(R"(
                declare Person
                    name: String
                    age: int
                end
                rule "Find Adults"
                when
                    Person(age > 18)
                then
                end
            )");

            auto minor = std::make_shared<Fact>();
            minor->type = "Person";
            minor->fields["name"] = "Alice";
            minor->fields["age"] = (int64_t)18;

            auto adult = std::make_shared<Fact>();
            adult->type = "Person";
            adult->fields["name"] = "Bob";
            adult->fields["age"] = (int64_t)19;

            session->add_fact(minor);
            session->add_fact(adult);

            check(session->fire_all_rules() == 1);
        }

        it("fails alpha numeric comparison on runtime type mismatch") {
            auto session = build_session(R"(
                declare Person
                    name: String
                    age: int
                end
                rule "Find Adults"
                when
                    Person(age > 18)
                then
                end
            )");

            auto mismatched = std::make_shared<Fact>();
            mismatched->type = "Person";
            mismatched->fields["name"] = "TypeMismatch";
            mismatched->fields["age"] = std::string("old");

            auto adult = std::make_shared<Fact>();
            adult->type = "Person";
            adult->fields["name"] = "Bob";
            adult->fields["age"] = int64_t(19);

            bool threw = false;
            try {
                session->add_fact(mismatched);
                session->add_fact(adult);
                (void)session->fire_all_rules();
            } catch (std::exception const&) {
                threw = true;
            }
            check(threw);
        }

        it("matches alpha numeric comparison after field is modified to matching numeric value") {
            auto session = build_session(R"(
                declare Person
                    name: String
                    age: int
                end
                rule "Find Adults"
                when
                    Person(age > 18)
                then
                end
            )");

            auto person = std::make_shared<Fact>();
            person->type = "Person";
            person->fields["name"] = "Pat";
            person->fields["age"] = int64_t(17);

            session->add_fact(person);
            check(session->fire_all_rules() == 0);

            session->update_fact(person.get(), [](Fact& fact) {
                fact.fields["age"] = int64_t(19);
            });

            check(session->fire_all_rules() == 1);
        }

        it("does not fire alpha numeric comparison after matching fact is retracted") {
            auto session = build_session(R"(
                declare Person
                    name: String
                    age: int
                end
                rule "Find Adults"
                when
                    Person(age > 18)
                then
                end
            )");

            auto person = std::make_shared<Fact>();
            person->type = "Person";
            person->fields["name"] = "Retracted";
            person->fields["age"] = int64_t(21);

            session->add_fact(person);
            session->retract_fact(person);

            check(session->fire_all_rules() == 0);
        }

        it("filters alpha numeric comparison during batch assert") {
            auto session = build_session(R"(
                declare Person
                    name: String
                    age: int
                end
                rule "Find Adults"
                when
                    Person(age > 18)
                then
                end
            )");

            auto minor = std::make_shared<Fact>();
            minor->type = "Person";
            minor->fields["name"] = "Minor";
            minor->fields["age"] = int64_t(18);

            auto adult = std::make_shared<Fact>();
            adult->type = "Person";
            adult->fields["name"] = "Adult";
            adult->fields["age"] = int64_t(19);

            auto senior = std::make_shared<Fact>();
            senior->type = "Person";
            senior->fields["name"] = "Senior";
            senior->fields["age"] = int64_t(70);

            session->add_facts(std::vector<std::shared_ptr<Fact>>{minor, adult, senior});

            check(session->fire_all_rules() == 2);
        }

        it("filters alpha numeric comparison through PHREAK deferred flush") {
            ParsingResult result;
            auto kb = build_knowledge_base(R"(
                declare Person
                    name: String
                    age: int
                end
                rule "Find Adults"
                when
                    Person(age > 18)
                then
                end
            )", result);
            if (!result.success) throw_parse_failure(result);
            check(kb != nullptr);
            kb->set_phreak_experimental(true);

            auto session = kb->create_session();
            check(session != nullptr);

            auto minor = std::make_shared<Fact>();
            minor->type = "Person";
            minor->fields["name"] = "Minor";
            minor->fields["age"] = int64_t(18);

            auto adult = std::make_shared<Fact>();
            adult->type = "Person";
            adult->fields["name"] = "Adult";
            adult->fields["age"] = int64_t(19);

            session->add_facts(std::vector<std::shared_ptr<Fact>>{minor, adult});

            auto before_fire = session->runtime_counters();
            check(before_fire.phreak_dirty_segment_marks > 0);
            check(before_fire.phreak_dirty_path_marks > 0);

            check(session->fire_all_rules() == 1);

            auto after_fire = session->runtime_counters();
            check(after_fire.phreak_flush_iterations > before_fire.phreak_flush_iterations);
        }

        it("filters alpha mixed int and double numeric comparison") {
            auto session = build_session(R"(
                declare Score
                    value: int
                end
                rule "Find High Scores"
                when
                    Score(value > 10.5)
                then
                end
            )");

            auto low = std::make_shared<Fact>();
            low->type = "Score";
            low->fields["value"] = int64_t(10);

            auto high = std::make_shared<Fact>();
            high->type = "Score";
            high->fields["value"] = int64_t(11);

            session->add_fact(low);
            session->add_fact(high);

            check(session->fire_all_rules() == 1);
        }

        it("filters alpha double fields with non-equality comparison") {
            auto session = build_session(R"(
                declare Order
                    amount: double
                end
                rule "Find High Value Orders"
                when
                    Order(amount > 500.0)
                then
                end
            )");

            auto small = std::make_shared<Fact>();
            small->type = "Order";
            small->fields["amount"] = 500.0;

            auto large = std::make_shared<Fact>();
            large->type = "Order";
            large->fields["amount"] = 500.5;

            session->add_fact(small);
            session->add_fact(large);

            check(session->fire_all_rules() == 1);
        }

        it("filters numeric expression comparison through MIR") {
            auto session = build_session(R"(
                declare Order
                    amount: double
                    base: double
                end
                rule "Find Orders Above Multiplied Base"
                when
                    Order(amount > ($base * 1.2))
                then
                end
            )");

            auto small = std::make_shared<Fact>();
            small->type = "Order";
            small->fields["amount"] = 110.0;
            small->fields["base"] = 100.0;

            auto large = std::make_shared<Fact>();
            large->type = "Order";
            large->fields["amount"] = 125.0;
            large->fields["base"] = 100.0;

            session->add_fact(small);
            session->add_fact(large);

            check(session->fire_all_rules() == 1);
        }

        it("filters alpha string fields with MIR equality comparison") {
            auto session = build_session(R"(
                declare Customer
                    status: String
                end
                rule "Find Gold Customers"
                when
                    Customer(status == "Gold")
                then
                end
            )");

            auto silver = std::make_shared<Fact>();
            silver->type = "Customer";
            silver->fields["status"] = "Silver";

            auto gold = std::make_shared<Fact>();
            gold->type = "Customer";
            gold->fields["status"] = "Gold";

            session->add_fact(silver);
            session->add_fact(gold);

            check(session->fire_all_rules() == 1);
        }

        it("filters alpha string fields with MIR inequality comparison") {
            auto session = build_session(R"(
                declare Customer
                    status: String
                end
                rule "Find Non Gold Customers"
                when
                    Customer(status != "Gold")
                then
                end
            )");

            auto silver = std::make_shared<Fact>();
            silver->type = "Customer";
            silver->fields["status"] = "Silver";

            auto gold = std::make_shared<Fact>();
            gold->type = "Customer";
            gold->fields["status"] = "Gold";

            session->add_fact(silver);
            session->add_fact(gold);

            check(session->fire_all_rules() == 1);
        }

        it("filters alpha string fields with MIR ordered comparison") {
            auto session = build_session(R"(
                declare Customer
                    status: String
                end
                rule "Find High Status Customers"
                when
                    Customer(status > "Gold")
                then
                end
            )");

            auto bronze = std::make_shared<Fact>();
            bronze->type = "Customer";
            bronze->fields["status"] = "Bronze";

            auto silver = std::make_shared<Fact>();
            silver->type = "Customer";
            silver->fields["status"] = "Silver";

            session->add_fact(bronze);
            session->add_fact(silver);

            check(session->fire_all_rules() == 1);
        }

        it("filters alpha string fields with MIR contains comparison") {
            auto session = build_session(R"(
                declare Customer
                    status: String
                end
                rule "Find Gold Customers"
                when
                    Customer(status contains "Gold")
                then
                end
            )");

            auto silver = std::make_shared<Fact>();
            silver->type = "Customer";
            silver->fields["status"] = "Silver";

            auto gold = std::make_shared<Fact>();
            gold->type = "Customer";
            gold->fields["status"] = "Gold Premium";

            session->add_fact(silver);
            session->add_fact(gold);

            check(session->fire_all_rules() == 1);
        }

        it("filters alpha string fields with MIR not contains comparison") {
            auto session = build_session(R"(
                declare Customer
                    status: String
                end
                rule "Find Non Gold Customers"
                when
                    Customer(status not contains "Gold")
                then
                end
            )");

            auto silver = std::make_shared<Fact>();
            silver->type = "Customer";
            silver->fields["status"] = "Silver";

            auto gold = std::make_shared<Fact>();
            gold->type = "Customer";
            gold->fields["status"] = "Gold Premium";

            session->add_fact(silver);
            session->add_fact(gold);

            check(session->fire_all_rules() == 1);
        }

        it("filters alpha string fields with MIR regex matches comparison") {
            auto session = build_session(R"(
                declare Customer
                    code: String
                end
                rule "Find Gold Codes"
                when
                    Customer(code matches "Gold-[0-9]+")
                then
                end
            )");

            auto silver = std::make_shared<Fact>();
            silver->type = "Customer";
            silver->fields["code"] = "Silver-42";

            auto gold = std::make_shared<Fact>();
            gold->type = "Customer";
            gold->fields["code"] = "Gold-42";

            session->add_fact(silver);
            session->add_fact(gold);

            check(session->fire_all_rules() == 1);
        }

        it("filters alpha string fields with MIR regex not matches comparison") {
            auto session = build_session(R"(
                declare Customer
                    code: String
                end
                rule "Find Non Gold Codes"
                when
                    Customer(code not matches "Gold-[0-9]+")
                then
                end
            )");

            auto silver = std::make_shared<Fact>();
            silver->type = "Customer";
            silver->fields["code"] = "Silver-42";

            auto gold = std::make_shared<Fact>();
            gold->type = "Customer";
            gold->fields["code"] = "Gold-42";

            session->add_fact(silver);
            session->add_fact(gold);

            check(session->fire_all_rules() == 1);
        }

        it("fails build for alpha invalid regex matches operators") {
            check_build_fails_with(R"(
                declare Customer
                    code: String
                end
                rule "Find Invalid Regex Matches"
                when
                    Customer(code matches "[")
                then
                end
            )", "regex_literal_invalid");

            check_build_fails_with(R"(
                declare Customer
                    code: String
                end
                rule "Find Invalid Regex Not Matches"
                when
                    Customer(code not matches "[")
                then
                end
            )", "regex_literal_invalid");
        }

        it("filters alpha string fields with MIR startsWith comparison") {
            auto session = build_session(R"(
                declare Customer
                    code: String
                end
                rule "Find Gold Prefix Codes"
                when
                    Customer(code startsWith "Gold")
                then
                end
            )");

            auto silver = std::make_shared<Fact>();
            silver->type = "Customer";
            silver->fields["code"] = "Silver-42";

            auto gold = std::make_shared<Fact>();
            gold->type = "Customer";
            gold->fields["code"] = "Gold-42";

            session->add_fact(silver);
            session->add_fact(gold);

            check(session->fire_all_rules() == 1);
        }

        it("hard-fails runtime for MIR string helper type mismatch") {
            auto session = build_session(R"(
                declare Customer
                    code: String
                end
                rule "Find Gold Prefix Codes"
                when
                    Customer(code startsWith "Gold")
                then
                end
            )");

            check(session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());

            auto malformed = std::make_shared<Fact>();
            malformed->type = "Customer";
            malformed->fields["code"] = int64_t{7};

            bool threw = false;
            try {
                session->add_fact(malformed);
                (void)session->fire_all_rules();
            } catch (std::runtime_error const& e) {
                threw = true;
                check(std::string(e.what()).find("alpha binary predicate MIR string affix predicate failed at runtime")
                      != std::string::npos);
            }
            check(threw);
        }

        it("fails build for string helper constraints with unsupported literal shape") {
            check_build_fails_with(R"(
                declare Customer
                    code: String
                end
                rule "Find Numeric Prefix"
                when
                    Customer(code startsWith 7)
                then
                end
            )", "string_helper_unlowered");
        }

        it("fails build for string helper constraints with unsupported dynamic field shape") {
            check_build_fails_with(R"(
                declare Pattern
                    prefix: int
                end
                declare Document
                    text: String
                end
                rule "Find Numeric Prefix"
                when
                    $p : Pattern($prefix : prefix)
                    Document(text startsWith $prefix)
                then
                end
            )", "string_helper_unlowered");

            check_build_fails_with(R"(
                declare Pattern
                    expectedLength: String
                end
                declare Document
                    text: String
                end
                rule "Find String Length"
                when
                    $p : Pattern($expectedLength : expectedLength)
                    Document(text lengthIs $expectedLength)
                then
                end
            )", "string_helper_unlowered");
        }

        it("filters alpha string fields with MIR endsWith comparison") {
            auto session = build_session(R"(
                declare Customer
                    code: String
                end
                rule "Find Suffix Codes"
                when
                    Customer(code endsWith "-42")
                then
                end
            )");

            auto other = std::make_shared<Fact>();
            other->type = "Customer";
            other->fields["code"] = "Gold-43";

            auto matching = std::make_shared<Fact>();
            matching->type = "Customer";
            matching->fields["code"] = "Gold-42";

            session->add_fact(other);
            session->add_fact(matching);

            check(session->fire_all_rules() == 1);
        }

        it("filters alpha string fields with MIR lengthIs comparison") {
            auto session = build_session(R"(
                declare Customer
                    code: String
                end
                rule "Find Seven Character Codes"
                when
                    Customer(code lengthIs 7.9)
                then
                end
            )");

            auto short_code = std::make_shared<Fact>();
            short_code->type = "Customer";
            short_code->fields["code"] = "Gold";

            auto matching = std::make_shared<Fact>();
            matching->type = "Customer";
            matching->fields["code"] = "Gold-42";

            session->add_fact(short_code);
            session->add_fact(matching);

            check(session->fire_all_rules() == 1);
        }

        it("filters alpha string fields with MIR-backed in list comparison") {
            auto session = build_session(R"(
                declare Customer
                    status: String
                end
                rule "Find Target Status Customers"
                when
                    Customer(status in ("Gold", "Platinum"))
                then
                end
            )");

            auto silver = std::make_shared<Fact>();
            silver->type = "Customer";
            silver->fields["status"] = "Silver";

            auto gold = std::make_shared<Fact>();
            gold->type = "Customer";
            gold->fields["status"] = "Gold";

            session->add_fact(silver);
            session->add_fact(gold);

            check(session->fire_all_rules() == 1);
        }

        it("filters alpha numeric fields with MIR-backed not in list comparison") {
            auto session = build_session(R"(
                declare Score
                    value: double
                end
                rule "Find Unblocked Scores"
                when
                    Score(value not in (10, 20.5))
                then
                end
            )");

            auto blocked_int = std::make_shared<Fact>();
            blocked_int->type = "Score";
            blocked_int->fields["value"] = 10.0;

            auto blocked_double = std::make_shared<Fact>();
            blocked_double->type = "Score";
            blocked_double->fields["value"] = 20.5;

            auto allowed = std::make_shared<Fact>();
            allowed->type = "Score";
            allowed->fields["value"] = 21.0;

            session->add_fact(blocked_int);
            session->add_fact(blocked_double);
            session->add_fact(allowed);

            check(session->fire_all_rules() == 1);
        }

        it("filters map fields with MIR-backed containsKey comparison") {
            auto session = build_session(R"(
                declare Profile
                    attrs: Map<String, int>
                end
                rule "Find Tiered Profiles"
                when
                    Profile(attrs containsKey "tier")
                then
                end
            )");
            check(session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());

            auto missing = std::make_shared<Fact>();
            missing->type = "Profile";
            auto missing_attrs = std::make_shared<ValueMap>();
            missing_attrs->entries[std::string("score")] = int64_t(7);
            missing->fields["attrs"] = missing_attrs;

            auto matching = std::make_shared<Fact>();
            matching->type = "Profile";
            auto matching_attrs = std::make_shared<ValueMap>();
            matching_attrs->entries[std::string("tier")] = int64_t(1);
            matching->fields["attrs"] = matching_attrs;

            session->add_fact(missing);
            session->add_fact(matching);

            check(session->fire_all_rules() == 1);
        }

        it("filters map fields with MIR-backed not containsKey comparison") {
            auto session = build_session(R"(
                declare Profile
                    attrs: Map<int, String>
                end
                rule "Find Profiles Without Block"
                when
                    Profile(attrs not containsKey 10)
                then
                end
            )");
            check(session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());

            auto blocked = std::make_shared<Fact>();
            blocked->type = "Profile";
            auto blocked_attrs = std::make_shared<ValueMap>();
            blocked_attrs->entries[int64_t(10)] = std::string("blocked");
            blocked->fields["attrs"] = blocked_attrs;

            auto allowed = std::make_shared<Fact>();
            allowed->type = "Profile";
            auto allowed_attrs = std::make_shared<ValueMap>();
            allowed_attrs->entries[int64_t(20)] = std::string("ok");
            allowed->fields["attrs"] = allowed_attrs;

            session->add_fact(blocked);
            session->add_fact(allowed);

            check(session->fire_all_rules() == 1);
        }

        it("filters map fields with MIR-backed nil containsKey comparison") {
            auto session = build_session(R"(
                declare Profile
                    attrs: Map<String, int>
                end
                rule "Find Profiles With Nil Key"
                when
                    Profile(attrs containsKey nil)
                then
                end
            )");
            check(session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());

            auto matching = std::make_shared<Fact>();
            matching->type = "Profile";
            auto matching_attrs = std::make_shared<ValueMap>();
            matching_attrs->entries[NilValue{}] = int64_t(1);
            matching->fields["attrs"] = matching_attrs;

            auto missing = std::make_shared<Fact>();
            missing->type = "Profile";
            auto missing_attrs = std::make_shared<ValueMap>();
            missing_attrs->entries[std::string("tier")] = int64_t(1);
            missing->fields["attrs"] = missing_attrs;

            session->add_fact(matching);
            session->add_fact(missing);

            check(session->fire_all_rules() == 1);
        }

        it("filters map fields with MIR-backed dynamic containsKey comparison") {
            auto session = build_session(R"(
                declare KeyHolder
                    name: String
                end
                declare Profile
                    attrs: Map<String, int>
                end
                rule "Find Profiles With Dynamic Key"
                when
                    $k : KeyHolder(name == "tier")
                    Profile(attrs containsKey $k.name)
                then
                end
            )");
            check(session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());

            auto key = std::make_shared<Fact>();
            key->type = "KeyHolder";
            key->fields["name"] = std::string("tier");

            auto missing = std::make_shared<Fact>();
            missing->type = "Profile";
            auto missing_attrs = std::make_shared<ValueMap>();
            missing_attrs->entries[std::string("score")] = int64_t(7);
            missing->fields["attrs"] = missing_attrs;

            auto matching = std::make_shared<Fact>();
            matching->type = "Profile";
            auto matching_attrs = std::make_shared<ValueMap>();
            matching_attrs->entries[std::string("tier")] = int64_t(1);
            matching->fields["attrs"] = matching_attrs;

            session->add_fact(key);
            session->add_fact(missing);
            session->add_fact(matching);

            check(session->fire_all_rules() == 1);
        }

        it("filters map fields with MIR-backed dynamic not containsKey comparison") {
            auto session = build_session(R"(
                declare KeyHolder
                    code: int
                end
                declare Profile
                    attrs: Map<int, String>
                end
                rule "Find Profiles Without Dynamic Key"
                when
                    $k : KeyHolder(code == 10)
                    Profile(attrs not containsKey $k.code)
                then
                end
            )");
            check(session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());

            auto key = std::make_shared<Fact>();
            key->type = "KeyHolder";
            key->fields["code"] = int64_t(10);

            auto blocked = std::make_shared<Fact>();
            blocked->type = "Profile";
            auto blocked_attrs = std::make_shared<ValueMap>();
            blocked_attrs->entries[int64_t(10)] = std::string("blocked");
            blocked->fields["attrs"] = blocked_attrs;

            auto allowed = std::make_shared<Fact>();
            allowed->type = "Profile";
            auto allowed_attrs = std::make_shared<ValueMap>();
            allowed_attrs->entries[int64_t(20)] = std::string("ok");
            allowed->fields["attrs"] = allowed_attrs;

            session->add_fact(key);
            session->add_fact(blocked);
            session->add_fact(allowed);

            check(session->fire_all_rules() == 1);
        }

        it("filters map fields with MIR-backed complex dynamic containsKey comparison") {
            auto session = build_session(R"(
                declare KeyHolder
                    values: List<int>
                end
                declare Profile
                    attrs: Map<List<int>, String>
                end
                rule "Find Profiles With Complex Dynamic Key"
                when
                    $k : KeyHolder()
                    Profile(attrs containsKey $k.values)
                then
                end
            )");
            check(session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());

            auto key = std::make_shared<Fact>();
            key->type = "KeyHolder";
            key->fields["values"] = make_typed_list({int64_t(10), int64_t(20)});

            auto matching = std::make_shared<Fact>();
            matching->type = "Profile";
            auto matching_attrs = std::make_shared<ValueMap>();
            matching_attrs->entries[make_typed_list({int64_t(10), int64_t(20)})] = std::string("ok");
            matching->fields["attrs"] = matching_attrs;

            auto missing = std::make_shared<Fact>();
            missing->type = "Profile";
            auto missing_attrs = std::make_shared<ValueMap>();
            missing_attrs->entries[make_typed_list({int64_t(20)})] = std::string("other");
            missing->fields["attrs"] = missing_attrs;

            session->add_fact(key);
            session->add_fact(matching);
            session->add_fact(missing);

            check(session->fire_all_rules() == 1);
        }

        it("filters map fields with MIR-backed FactList dynamic containsKey comparison") {
            auto session = build_session(R"(
                declare KeyHolder
                    values: FactList
                end
                declare Profile
                    attrs: Map<FactList, String>
                end
                rule "Find Profiles With FactList Key"
                when
                    $k : KeyHolder()
                    Profile(attrs containsKey $k.values)
                then
                end
            )");
            check(session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());

            auto item = std::make_shared<Fact>();
            item->type = "Item";

            auto other = std::make_shared<Fact>();
            other->type = "Other";

            auto key = std::make_shared<Fact>();
            key->type = "KeyHolder";
            key->fields["values"] = FactList{{item.get()}};

            auto matching = std::make_shared<Fact>();
            matching->type = "Profile";
            auto matching_attrs = std::make_shared<ValueMap>();
            matching_attrs->entries[FactList{{item.get()}}] = std::string("match");
            matching->fields["attrs"] = matching_attrs;

            auto missing = std::make_shared<Fact>();
            missing->type = "Profile";
            auto missing_attrs = std::make_shared<ValueMap>();
            missing_attrs->entries[FactList{{other.get()}}] = std::string("other");
            missing->fields["attrs"] = missing_attrs;

            session->add_fact(key);
            session->add_fact(matching);
            session->add_fact(missing);

            check(session->fire_all_rules() == 1);
        }

        it("filters typed list fields with MIR-backed contains comparison") {
            auto session = build_session(R"(
                declare Box
                    values: List<int>
                end
                rule "Find Ten"
                when
                    Box(values contains 10)
                then
                end
            )");
            check(session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());

            auto matching = std::make_shared<Fact>();
            matching->type = "Box";
            matching->fields["values"] = make_typed_list({int64_t(10), int64_t(20)});

            auto missing = std::make_shared<Fact>();
            missing->type = "Box";
            missing->fields["values"] = make_typed_list({int64_t(20)});

            session->add_fact(matching);
            session->add_fact(missing);

            check(session->fire_all_rules() == 1);
        }

        it("filters typed list fields with MIR-backed not contains comparison") {
            auto session = build_session(R"(
                declare Box
                    values: List<double>
                end
                rule "Find Boxes Without Ten"
                when
                    Box(values not contains 10.0)
                then
                end
            )");
            check(session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());

            auto blocked = std::make_shared<Fact>();
            blocked->type = "Box";
            blocked->fields["values"] = make_typed_list({10.0, 20.0});

            auto allowed = std::make_shared<Fact>();
            allowed->type = "Box";
            allowed->fields["values"] = make_typed_list({20.0});

            session->add_fact(blocked);
            session->add_fact(allowed);

            check(session->fire_all_rules() == 1);
        }

        it("filters value set fields with MIR-backed contains comparison") {
            auto session = build_session(R"(
                declare Box
                    values: Set<int>
                end
                rule "Find Ten"
                when
                    Box(values contains 10)
                then
                end
            )");
            check(session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());

            auto matching_values = std::make_shared<ValueSet>();
            matching_values->values.insert(int64_t(10));
            matching_values->values.insert(int64_t(20));
            auto matching = std::make_shared<Fact>();
            matching->type = "Box";
            matching->fields["values"] = matching_values;

            auto missing_values = std::make_shared<ValueSet>();
            missing_values->values.insert(int64_t(20));
            auto missing = std::make_shared<Fact>();
            missing->type = "Box";
            missing->fields["values"] = missing_values;

            session->add_fact(matching);
            session->add_fact(missing);

            check(session->fire_all_rules() == 1);
        }

        it("filters typed list fields with MIR-backed memberOf comparison") {
            auto session = build_session(R"(
                declare Box
                    values: List<int>
                end
                declare Probe
                    value: int
                end
                rule "Find Member"
                when
                    $b: Box($values: values)
                    Probe(value memberOf $values)
                then
                end
            )");
            check(session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());

            auto box = std::make_shared<Fact>();
            box->type = "Box";
            box->fields["values"] = make_typed_list({int64_t(10), int64_t(20)});

            auto matching = std::make_shared<Fact>();
            matching->type = "Probe";
            matching->fields["value"] = int64_t(10);

            auto missing = std::make_shared<Fact>();
            missing->type = "Probe";
            missing->fields["value"] = int64_t(30);

            session->add_fact(box);
            session->add_fact(matching);
            session->add_fact(missing);

            check(session->fire_all_rules() == 1);
        }

        it("filters value set fields with MIR-backed not memberOf comparison") {
            auto session = build_session(R"(
                declare Box
                    values: Set<int>
                end
                declare Probe
                    value: int
                end
                rule "Find Non Member"
                when
                    $b: Box($values: values)
                    Probe(value not memberOf $values)
                then
                end
            )");
            check(session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());

            auto values = std::make_shared<ValueSet>();
            values->values.insert(int64_t(10));
            values->values.insert(int64_t(20));
            auto box = std::make_shared<Fact>();
            box->type = "Box";
            box->fields["values"] = values;

            auto blocked = std::make_shared<Fact>();
            blocked->type = "Probe";
            blocked->fields["value"] = int64_t(10);

            auto allowed = std::make_shared<Fact>();
            allowed->type = "Probe";
            allowed->fields["value"] = int64_t(30);

            session->add_fact(box);
            session->add_fact(blocked);
            session->add_fact(allowed);

            check(session->fire_all_rules() == 1);
        }

        it("filters fact list fields with MIR-backed contains type comparison") {
            auto session = build_session(R"(
                declare Container
                    items: FactList
                end
                rule "Find Item Type"
                when
                    Container(items contains "Item")
                then
                end
            )");
            check(session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());

            auto item = std::make_shared<Fact>();
            item->type = "Item";

            auto other = std::make_shared<Fact>();
            other->type = "Other";

            auto matching = std::make_shared<Fact>();
            matching->type = "Container";
            matching->fields["items"] = FactList{{item.get(), other.get()}};

            auto missing = std::make_shared<Fact>();
            missing->type = "Container";
            missing->fields["items"] = FactList{{other.get()}};

            session->add_fact(item);
            session->add_fact(other);
            session->add_fact(matching);
            session->add_fact(missing);

            check(session->fire_all_rules() == 1);
        }

        it("filters fact list fields with MIR-backed not contains type comparison") {
            auto session = build_session(R"(
                declare Container
                    items: FactList
                end
                rule "Find Missing Type"
                when
                    Container(items not contains "Item")
                then
                end
            )");
            check(session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());

            auto item = std::make_shared<Fact>();
            item->type = "Item";

            auto other = std::make_shared<Fact>();
            other->type = "Other";

            auto blocked = std::make_shared<Fact>();
            blocked->type = "Container";
            blocked->fields["items"] = FactList{{item.get()}};

            auto matching = std::make_shared<Fact>();
            matching->type = "Container";
            matching->fields["items"] = FactList{{other.get()}};

            session->add_fact(item);
            session->add_fact(other);
            session->add_fact(blocked);
            session->add_fact(matching);

            check(session->fire_all_rules() == 1);
        }

        it("filters fact list fields with MIR-backed memberOf type comparison") {
            auto session = build_session(R"(
                declare Container
                    items: FactList
                end
                declare Probe
                    label: String
                end
                rule "Find Item Type"
                when
                    $c: Container($items: items)
                    Probe(label memberOf $items)
                then
                end
            )");
            check(session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());

            auto item = std::make_shared<Fact>();
            item->type = "Item";

            auto container = std::make_shared<Fact>();
            container->type = "Container";
            container->fields["items"] = FactList{{item.get()}};

            auto matching = std::make_shared<Fact>();
            matching->type = "Probe";
            matching->fields["label"] = std::string("Item");

            auto missing = std::make_shared<Fact>();
            missing->type = "Probe";
            missing->fields["label"] = std::string("Other");

            session->add_fact(item);
            session->add_fact(container);
            session->add_fact(matching);
            session->add_fact(missing);

            check(session->fire_all_rules() == 1);
        }

        it("filters fact list fields with MIR-backed not memberOf type comparison") {
            auto session = build_session(R"(
                declare Container
                    items: FactList
                end
                declare Probe
                    label: String
                end
                rule "Find Missing Type"
                when
                    $c: Container($items: items)
                    Probe(label not memberOf $items)
                then
                end
            )");
            check(session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());

            auto item = std::make_shared<Fact>();
            item->type = "Item";

            auto container = std::make_shared<Fact>();
            container->type = "Container";
            container->fields["items"] = FactList{{item.get()}};

            auto blocked = std::make_shared<Fact>();
            blocked->type = "Probe";
            blocked->fields["label"] = std::string("Item");

            auto matching = std::make_shared<Fact>();
            matching->type = "Probe";
            matching->fields["label"] = std::string("Other");

            session->add_fact(item);
            session->add_fact(container);
            session->add_fact(blocked);
            session->add_fact(matching);

            check(session->fire_all_rules() == 1);
        }

        it("filters projected fact list fields with MIR-backed contains comparison") {
            auto session = build_session(R"(
                declare Container
                    items: FactList
                end
                declare Item
                    value: int
                end
                rule "Find Projected Item Value"
                when
                    Container(items.value contains 10)
                then
                end
            )");
            check(session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());

            auto first = std::make_shared<Fact>();
            first->type = "Item";
            first->fields["value"] = int64_t(10);

            auto second = std::make_shared<Fact>();
            second->type = "Item";
            second->fields["value"] = int64_t(20);

            auto third = std::make_shared<Fact>();
            third->type = "Item";
            third->fields["value"] = int64_t(30);

            auto matching = std::make_shared<Fact>();
            matching->type = "Container";
            matching->fields["items"] = FactList{{first.get(), second.get()}};

            auto missing = std::make_shared<Fact>();
            missing->type = "Container";
            missing->fields["items"] = FactList{{second.get(), third.get()}};

            session->add_fact(matching);
            session->add_fact(missing);

            check(session->fire_all_rules() == 1);
        }

        it("filters projected fact list fields with MIR-backed memberOf comparison") {
            auto session = build_session(R"(
                declare Container
                    items: FactList
                end
                declare Item
                    value: int
                end
                declare Probe
                    value: int
                end
                rule "Find Projected Item Member"
                when
                    $c : Container()
                    Probe(value memberOf $c.items.value)
                then
                end
            )");
            check(session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());

            auto first = std::make_shared<Fact>();
            first->type = "Item";
            first->fields["value"] = int64_t(10);

            auto second = std::make_shared<Fact>();
            second->type = "Item";
            second->fields["value"] = int64_t(20);

            auto container = std::make_shared<Fact>();
            container->type = "Container";
            container->fields["items"] = FactList{{first.get(), second.get()}};

            auto matching = std::make_shared<Fact>();
            matching->type = "Probe";
            matching->fields["value"] = int64_t(20);

            auto missing = std::make_shared<Fact>();
            missing->type = "Probe";
            missing->fields["value"] = int64_t(30);

            session->add_fact(container);
            session->add_fact(matching);
            session->add_fact(missing);

            check(session->fire_all_rules() == 1);
        }

    }

    group("Join Condition") {
        it("joins facts correctly") {
            auto session = build_session(R"(
                declare Customer
                    id: int
                    name: String
                end
                declare Order
                    customerId: int
                    item: String
                end
                rule "Find Customer Orders"
                when
                    $c : Customer($id : id)
                    $o : Order(customerId == $id)
                then
                end
            )");

            auto customer = std::make_shared<Fact>();
            customer->type = "Customer";
            customer->fields["id"] = (int64_t)123;
            customer->fields["name"] = "Acme Inc.";

            auto order = std::make_shared<Fact>();
            order->type = "Order";
            order->fields["customerId"] = (int64_t)123;
            order->fields["item"] = "Anvil";

            session->add_fact(customer);
            session->add_fact(order);

            check(session->fire_all_rules() == 1);
        }

        it("joins int fields with non-equality comparison") {
            auto session = build_session(R"(
                declare Customer
                    id: int
                end
                declare Order
                    customerId: int
                    item: String
                end
                rule "Find Later Customer Orders"
                when
                    $c : Customer($id : id)
                    $o : Order(customerId > $id)
                then
                end
            )");

            auto customer = std::make_shared<Fact>();
            customer->type = "Customer";
            customer->fields["id"] = (int64_t)123;

            auto later_order = std::make_shared<Fact>();
            later_order->type = "Order";
            later_order->fields["customerId"] = (int64_t)124;
            later_order->fields["item"] = "Anvil";

            auto same_order = std::make_shared<Fact>();
            same_order->type = "Order";
            same_order->fields["customerId"] = (int64_t)123;
            same_order->fields["item"] = "Hammer";

            session->add_fact(customer);
            session->add_fact(later_order);
            session->add_fact(same_order);

            check(session->fire_all_rules() == 1);
        }

        it("fails join numeric comparison on runtime type mismatch") {
            auto session = build_session(R"(
                declare Customer
                    id: int
                end
                declare Order
                    customerId: int
                end
                rule "Find Later Customer Orders"
                when
                    $c : Customer($id : id)
                    $o : Order(customerId > $id)
                then
                end
            )");

            auto customer = std::make_shared<Fact>();
            customer->type = "Customer";
            customer->fields["id"] = int64_t(123);

            auto mismatched_order = std::make_shared<Fact>();
            mismatched_order->type = "Order";
            mismatched_order->fields["customerId"] = std::string("later");

            auto later_order = std::make_shared<Fact>();
            later_order->type = "Order";
            later_order->fields["customerId"] = int64_t(124);

            bool threw = false;
            try {
                session->add_fact(customer);
                session->add_fact(mismatched_order);
                session->add_fact(later_order);
                (void)session->fire_all_rules();
            } catch (std::exception const&) {
                threw = true;
            }
            check(threw);
        }

        it("joins mixed int and double numeric comparison") {
            auto session = build_session(R"(
                declare Customer
                    limit: int
                end
                declare Order
                    amount: double
                end
                rule "Find Orders Above Customer Limit"
                when
                    $c : Customer($limit : limit)
                    $o : Order(amount > $limit)
                then
                end
            )");

            auto customer = std::make_shared<Fact>();
            customer->type = "Customer";
            customer->fields["limit"] = int64_t(100);

            auto low_order = std::make_shared<Fact>();
            low_order->type = "Order";
            low_order->fields["amount"] = 100.0;

            auto high_order = std::make_shared<Fact>();
            high_order->type = "Order";
            high_order->fields["amount"] = 100.5;

            session->add_fact(customer);
            session->add_fact(low_order);
            session->add_fact(high_order);

            check(session->fire_all_rules() == 1);
        }

        it("joins double fields with non-equality comparison") {
            auto session = build_session(R"(
                declare Customer
                    limit: double
                end
                declare Order
                    amount: double
                end
                rule "Find Orders Above Customer Limit"
                when
                    $c : Customer($limit : limit)
                    $o : Order(amount > $limit)
                then
                end
            )");

            auto customer = std::make_shared<Fact>();
            customer->type = "Customer";
            customer->fields["limit"] = 500.0;

            auto high_order = std::make_shared<Fact>();
            high_order->type = "Order";
            high_order->fields["amount"] = 500.5;

            auto low_order = std::make_shared<Fact>();
            low_order->type = "Order";
            low_order->fields["amount"] = 500.0;

            session->add_fact(customer);
            session->add_fact(high_order);
            session->add_fact(low_order);

            check(session->fire_all_rules() == 1);
        }

        it("joins string fields with MIR equality comparison") {
            auto session = build_session(R"(
                declare Customer
                    status: String
                end
                declare Order
                    requiredStatus: String
                end
                rule "Find Matching Status Orders"
                when
                    $c : Customer($status : status)
                    $o : Order(requiredStatus == $status)
                then
                end
            )");

            auto customer = std::make_shared<Fact>();
            customer->type = "Customer";
            customer->fields["status"] = "Gold";

            auto matching_order = std::make_shared<Fact>();
            matching_order->type = "Order";
            matching_order->fields["requiredStatus"] = "Gold";

            auto other_order = std::make_shared<Fact>();
            other_order->type = "Order";
            other_order->fields["requiredStatus"] = "Silver";

            session->add_fact(customer);
            session->add_fact(matching_order);
            session->add_fact(other_order);

            check(session->fire_all_rules() == 1);
        }

        it("joins string fields with MIR inequality comparison") {
            auto session = build_session(R"(
                declare Customer
                    status: String
                end
                declare Order
                    requiredStatus: String
                end
                rule "Find Different Status Orders"
                when
                    $c : Customer($status : status)
                    $o : Order(requiredStatus != $status)
                then
                end
            )");

            auto customer = std::make_shared<Fact>();
            customer->type = "Customer";
            customer->fields["status"] = "Gold";

            auto matching_order = std::make_shared<Fact>();
            matching_order->type = "Order";
            matching_order->fields["requiredStatus"] = "Gold";

            auto other_order = std::make_shared<Fact>();
            other_order->type = "Order";
            other_order->fields["requiredStatus"] = "Silver";

            session->add_fact(customer);
            session->add_fact(matching_order);
            session->add_fact(other_order);

            check(session->fire_all_rules() == 1);
        }

        it("joins string fields with MIR ordered comparison") {
            auto session = build_session(R"(
                declare Customer
                    status: String
                end
                declare Order
                    requiredStatus: String
                end
                rule "Find Higher Status Orders"
                when
                    $c : Customer($status : status)
                    $o : Order(requiredStatus > $status)
                then
                end
            )");

            auto customer = std::make_shared<Fact>();
            customer->type = "Customer";
            customer->fields["status"] = "Gold";

            auto higher_order = std::make_shared<Fact>();
            higher_order->type = "Order";
            higher_order->fields["requiredStatus"] = "Silver";

            auto lower_order = std::make_shared<Fact>();
            lower_order->type = "Order";
            lower_order->fields["requiredStatus"] = "Bronze";

            session->add_fact(customer);
            session->add_fact(higher_order);
            session->add_fact(lower_order);

            check(session->fire_all_rules() == 1);
        }

        it("joins string fields with MIR runtime string operators") {
            auto session = build_session(R"(
                declare Pattern
                    needle: String
                    prefix: String
                    expectedLength: int
                end
                declare Document
                    text: String
                end
                rule "Find Containing Documents"
                when
                    $p : Pattern($needle : needle)
                    $d : Document(text contains $needle)
                then
                end
                rule "Find Prefixed Documents"
                when
                    $p : Pattern($prefix : prefix)
                    $d : Document(text startsWith $prefix)
                then
                end
                rule "Find Length Documents"
                when
                    $p : Pattern($expectedLength : expectedLength)
                    $d : Document(text lengthIs $expectedLength)
                then
                end
            )");

            auto pattern = std::make_shared<Fact>();
            pattern->type = "Pattern";
            pattern->fields["needle"] = "Gold";
            pattern->fields["prefix"] = "Gold";
            pattern->fields["expectedLength"] = int64_t(7);

            auto matching = std::make_shared<Fact>();
            matching->type = "Document";
            matching->fields["text"] = "Gold-42";

            auto other = std::make_shared<Fact>();
            other->type = "Document";
            other->fields["text"] = "Silver-43";

            session->add_fact(pattern);
            session->add_fact(matching);
            session->add_fact(other);

            check(session->fire_all_rules() == 3);
        }

        it("joins string fields with negated and boundary MIR runtime string operators") {
            auto session = build_session(R"(
                declare Pattern
                    needle: String
                    suffix: String
                    expectedLength: double
                end
                declare Document
                    text: String
                end
                rule "Find Non Containing Documents"
                when
                    $p : Pattern($needle : needle)
                    $d : Document(text not contains $needle)
                then
                end
                rule "Find Suffixed Documents"
                when
                    $p : Pattern($suffix : suffix)
                    $d : Document(text endsWith $suffix)
                then
                end
                rule "Find Double Length Documents"
                when
                    $p : Pattern($expectedLength : expectedLength)
                    $d : Document(text lengthIs $expectedLength)
                then
                end
            )");

            auto pattern = std::make_shared<Fact>();
            pattern->type = "Pattern";
            pattern->fields["needle"] = "Gold";
            pattern->fields["suffix"] = "-43";
            pattern->fields["expectedLength"] = 9.9;

            auto gold = std::make_shared<Fact>();
            gold->type = "Document";
            gold->fields["text"] = "Gold-42";

            auto silver = std::make_shared<Fact>();
            silver->type = "Document";
            silver->fields["text"] = "Silver-43";

            session->add_fact(pattern);
            session->add_fact(gold);
            session->add_fact(silver);

            check(session->fire_all_rules() == 3);
        }

        it("joins string fields with dynamic MIR regex operators") {
            auto session = build_session(R"(
                declare Pattern
                    regex: String
                end
                declare Document
                    text: String
                end
                rule "Find Dynamic Regex Join Matches"
                when
                    $p : Pattern($regex : regex)
                    $d : Document(text matches $regex)
                then
                end
                rule "Find Dynamic Regex Join Not Matches"
                when
                    $p : Pattern($regex : regex)
                    $d : Document(text not matches $regex)
                then
                end
            )");

            auto pattern = std::make_shared<Fact>();
            pattern->type = "Pattern";
            pattern->fields["regex"] = "Gold-[0-9]+";

            auto gold = std::make_shared<Fact>();
            gold->type = "Document";
            gold->fields["text"] = "Gold-42";

            auto silver = std::make_shared<Fact>();
            silver->type = "Document";
            silver->fields["text"] = "Silver";

            session->add_fact(pattern);
            session->add_fact(gold);
            session->add_fact(silver);

            check(session->fire_all_rules() == 2);
        }

        it("hard-fails runtime for invalid dynamic MIR regex") {
            auto session = build_session(R"(
                declare Pattern
                    regex: String
                end
                declare Document
                    text: String
                end
                rule "Find Invalid Dynamic Regex"
                when
                    $p : Pattern($regex : regex)
                    $d : Document(text matches $regex)
                then
                end
            )");

            check(session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());

            auto pattern = std::make_shared<Fact>();
            pattern->type = "Pattern";
            pattern->fields["regex"] = "[";

            auto document = std::make_shared<Fact>();
            document->type = "Document";
            document->fields["text"] = "Gold-42";

            bool threw = false;
            try {
                session->add_fact(pattern);
                session->add_fact(document);
                (void)session->fire_all_rules();
            } catch (std::runtime_error const& e) {
                threw = true;
                check(std::string(e.what()).find("join predicate MIR string regex predicate failed at runtime")
                      != std::string::npos);
            }
            check(threw);
        }

        it("treats missing join fields as non-matches instead of synthetic nil") {
            auto session = build_session(R"(
                declare Pattern
                    expected: String
                end
                declare Document
                    text: String
                end
                rule "Find Expected Text"
                when
                    $p : Pattern($expected : expected)
                    $d : Document(text == $expected)
                then
                end
            )");

            auto pattern = std::make_shared<Fact>();
            pattern->type = "Pattern";
            pattern->fields["expected"] = "Gold";

            auto missing_expected = std::make_shared<Fact>();
            missing_expected->type = "Pattern";

            auto matching = std::make_shared<Fact>();
            matching->type = "Document";
            matching->fields["text"] = "Gold";

            session->add_fact(pattern);
            session->add_fact(missing_expected);
            session->add_fact(matching);

            check(session->fire_all_rules() == 1);
        }

        it("filters temporal join constraints through MIR") {
            auto session = build_session(R"(
                declare Event
                    timestamp: long
                    kind: String
                end
                rule "Find Nearby Later Events"
                when
                    $e1 : Event(kind == "target")
                    $e2 : Event(timestamp after $e1.timestamp, within 10s of $e1)
                then
                end
            )");

            auto anchor = std::make_shared<Fact>();
            anchor->type = "Event";
            anchor->fields["timestamp"] = int64_t(1000);
            anchor->fields["kind"] = "target";

            auto nearby = std::make_shared<Fact>();
            nearby->type = "Event";
            nearby->fields["timestamp"] = int64_t(5000);
            nearby->fields["kind"] = "other";

            auto distant = std::make_shared<Fact>();
            distant->type = "Event";
            distant->fields["timestamp"] = int64_t(20000);
            distant->fields["kind"] = "other";

            session->add_fact(anchor);
            session->add_fact(nearby);
            session->add_fact(distant);

            check(session->fire_all_rules() == 1);
        }

        it("filters join numeric expression comparison through MIR") {
            auto session = build_session(R"(
                declare Customer
                    limit: double
                end
                declare Order
                    amount: double
                    multiplier: double
                end
                rule "Find Orders Above Adjusted Customer Limit"
                when
                    $c : Customer($limit : limit)
                    $o : Order(amount > ($c.limit * $multiplier))
                then
                end
            )");

            auto customer = std::make_shared<Fact>();
            customer->type = "Customer";
            customer->fields["limit"] = 100.0;

            auto small = std::make_shared<Fact>();
            small->type = "Order";
            small->fields["amount"] = 150.0;
            small->fields["multiplier"] = 2.0;

            auto large = std::make_shared<Fact>();
            large->type = "Order";
            large->fields["amount"] = 250.0;
            large->fields["multiplier"] = 2.0;

            session->add_fact(customer);
            session->add_fact(small);
            session->add_fact(large);

            check(session->fire_all_rules() == 1);
        }

        it("fails join numeric expression comparison on runtime type mismatch") {
            auto session = build_session(R"(
                declare Customer
                    limit: double
                end
                declare Order
                    amount: double
                    multiplier: double
                end
                rule "Find Orders Above Adjusted Customer Limit"
                when
                    $c : Customer($limit : limit)
                    $o : Order(amount > ($c.limit * $multiplier))
                then
                end
            )");

            auto customer = std::make_shared<Fact>();
            customer->type = "Customer";
            customer->fields["limit"] = 100.0;

            auto mismatched = std::make_shared<Fact>();
            mismatched->type = "Order";
            mismatched->fields["amount"] = 250.0;
            mismatched->fields["multiplier"] = std::string("2.0");

            bool threw = false;
            try {
                session->add_fact(customer);
                session->add_fact(mismatched);
                (void)session->fire_all_rules();
            } catch (std::exception const&) {
                threw = true;
            }
            check(threw);
        }

        it("treats missing join expression fields as non-matches") {
            auto session = build_session(R"(
                declare Customer
                    limit: double
                end
                declare Order
                    amount: double
                    multiplier: double
                end
                rule "Find Orders Above Adjusted Customer Limit"
                when
                    $c : Customer($limit : limit)
                    $o : Order(amount > ($c.limit * $multiplier))
                then
                end
            )");

            auto customer = std::make_shared<Fact>();
            customer->type = "Customer";
            customer->fields["limit"] = 100.0;

            auto missing_multiplier = std::make_shared<Fact>();
            missing_multiplier->type = "Order";
            missing_multiplier->fields["amount"] = 250.0;

            auto matching = std::make_shared<Fact>();
            matching->type = "Order";
            matching->fields["amount"] = 250.0;
            matching->fields["multiplier"] = 2.0;

            session->add_fact(customer);
            session->add_fact(missing_multiplier);
            session->add_fact(matching);

            check(session->fire_all_rules() == 1);
        }
    }

    group("Conditional Join") {
        it("runs not pattern orchestration with MIR numeric join comparison") {
            auto session = build_session(R"(
                declare Customer
                    id: int
                end
                declare Order
                    customerId: int
                end
                rule "Find Customers Without Later Orders"
                when
                    $c : Customer($id : id)
                    not (Order(customerId > $id))
                then
                end
            )");
            check(session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());

            auto customer_one = std::make_shared<Fact>();
            customer_one->type = "Customer";
            customer_one->fields["id"] = int64_t(10);

            auto customer_two = std::make_shared<Fact>();
            customer_two->type = "Customer";
            customer_two->fields["id"] = int64_t(20);

            auto later_order = std::make_shared<Fact>();
            later_order->type = "Order";
            later_order->fields["customerId"] = int64_t(30);

            session->add_fact(customer_one);
            session->add_fact(customer_two);
            session->add_fact(later_order);

            check(session->fire_all_rules() == 0);
        }

        it("runs exists pattern orchestration with MIR numeric join comparison") {
            auto session = build_session(R"(
                declare Customer
                    id: int
                end
                declare Order
                    customerId: int
                end
                rule "Find Customers With Later Orders"
                when
                    $c : Customer($id : id)
                    exists (Order(customerId > $id))
                then
                end
            )");
            check(session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());

            auto customer_one = std::make_shared<Fact>();
            customer_one->type = "Customer";
            customer_one->fields["id"] = int64_t(10);

            auto customer_two = std::make_shared<Fact>();
            customer_two->type = "Customer";
            customer_two->fields["id"] = int64_t(20);

            auto later_order = std::make_shared<Fact>();
            later_order->type = "Order";
            later_order->fields["customerId"] = int64_t(30);

            session->add_fact(customer_one);
            session->add_fact(customer_two);
            session->add_fact(later_order);

            check(session->fire_all_rules() == 2);
        }

        it("runs rule-side query call orchestration with MIR-backed query body") {
            auto session = build_session(R"(
                declare Event
                    kind: String
                end
                query "findEvents"
                    $e : Event(kind == "target")
                end
                rule "Query Call Rule"
                when
                    "findEvents"()
                then
                end
            )");
            check(session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());
            check(session->fire_all_rules() == 0);

            auto ignored = std::make_shared<Fact>();
            ignored->type = "Event";
            ignored->fields["kind"] = "other";
            session->add_fact(ignored);
            check(session->fire_all_rules() == 0);

            auto target = std::make_shared<Fact>();
            target->type = "Event";
            target->fields["kind"] = "target";
            session->add_fact(target);
            check(session->fire_all_rules() == 1);

            session->retract_fact(target.get());
            check(session->fire_all_rules() == 0);

            auto target_again = std::make_shared<Fact>();
            target_again->type = "Event";
            target_again->fields["kind"] = "target";
            session->add_fact(target_again);
            check(session->fire_all_rules() == 1);
        }

        it("runs parameterized rule-side query call orchestration") {
            auto session = build_session(R"(
                declare Customer
                    id: int
                end
                declare Order
                    customerId: int
                end
                query "findOrders"(Customer $c)
                    $o : Order(customerId == $c.id)
                end
                rule "Parameterized Query Call Rule"
                when
                    $c : Customer()
                    "findOrders"($c)
                then
                end
            )");
            auto const& rule_coverages = session->get_knowledge_base()->mir_rule_coverages();
            check(rule_coverages.size() == 1);
            check(rule_coverages[0].lowering_errors.empty());
            auto const& query_coverages = session->get_knowledge_base()->mir_query_coverages();
            check(query_coverages.size() == 1);
            check(query_coverages[0].query_name == "findOrders");
            check(query_coverages[0].constraint_count == 1);
            check(query_coverages[0].compare_predicate_count == 1);
            check(query_coverages[0].lowering_errors.empty());
            auto const& query_graphs = session->get_knowledge_base()->mir_query_graphs();
            check(query_graphs.size() == 1);
            check(query_graphs[0].query_name == "findOrders");
            check(query_graphs[0].nodes.size() == 2);
            check(query_graphs[0].nodes[1].binding == "$o");
            check(query_graphs[0].nodes[1].predicates.size() == 1);
            check(query_graphs[0].nodes[1].predicates[0].kind == rulesforge::MirRuntimePredicateKind::Compare);

            auto customer = std::make_shared<Fact>();
            customer->type = "Customer";
            customer->fields["id"] = int64_t(7);
            session->add_fact(customer);
            check(session->fire_all_rules() == 0);

            auto unrelated_order = std::make_shared<Fact>();
            unrelated_order->type = "Order";
            unrelated_order->fields["customerId"] = int64_t(9);
            session->add_fact(unrelated_order);
            check(session->fire_all_rules() == 0);

            auto order = std::make_shared<Fact>();
            order->type = "Order";
            order->fields["customerId"] = int64_t(7);
            session->add_fact(order);
            check(session->fire_all_rules() == 1);
        }

        it("projects rule-side query result bindings into downstream MIR predicates") {
            auto session = build_session(R"(
                declare Customer
                    id: int
                end
                declare Order
                    customerId: int
                end
                query "findOrders"(Customer $c)
                    $o : Order(customerId == $c.id)
                end
                rule "Projected Query Call Rule"
                when
                    $c : Customer()
                    "findOrders"($c)
                    eval($o.customerId == $c.id)
                then
                end
            )");
            auto const& rule_coverages = session->get_knowledge_base()->mir_rule_coverages();
            check(rule_coverages.size() == 1);
            check(rule_coverages[0].lowering_errors.empty());
            auto const& query_coverages = session->get_knowledge_base()->mir_query_coverages();
            check(query_coverages.size() == 1);
            check(query_coverages[0].lowering_errors.empty());
            auto const& query_graphs = session->get_knowledge_base()->mir_query_graphs();
            check(query_graphs.size() == 1);
            check(query_graphs[0].nodes.size() == 2);
            check(query_graphs[0].nodes[1].binding == "$o");

            auto customer = std::make_shared<Fact>();
            customer->type = "Customer";
            customer->fields["id"] = int64_t(7);

            auto matching_order_one = std::make_shared<Fact>();
            matching_order_one->type = "Order";
            matching_order_one->fields["customerId"] = int64_t(7);

            auto matching_order_two = std::make_shared<Fact>();
            matching_order_two->type = "Order";
            matching_order_two->fields["customerId"] = int64_t(7);

            auto unrelated_order = std::make_shared<Fact>();
            unrelated_order->type = "Order";
            unrelated_order->fields["customerId"] = int64_t(9);

            session->add_fact(customer);
            session->add_fact(matching_order_one);
            session->add_fact(matching_order_two);
            session->add_fact(unrelated_order);

            check(session->fire_all_rules() == 2);
        }

        it("projects rule-side query result bindings into TurboScript RHS") {
            auto session = build_session(R"(
                declare Customer
                    id: int
                end
                declare Order
                    customerId: int
                end
                declare Result
                    customerId: int
                end
                query "findOrders"(Customer $c)
                    $o : Order(customerId == $c.id)
                end
                query "findResults"
                    $r : Result()
                end
                rule "Projected Query Call RHS Rule"
                when
                    $c : Customer()
                    "findOrders"($c)
                then
                    insert Result { customerId = $o.customerId }
                end
            )");
            check(session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());
            check(session->get_knowledge_base()->rhs_backend_summary().turboscript_mir_rule_count == 1);
            check(session->get_knowledge_base()->rhs_backend_summary().command_count == 1);

            auto customer = std::make_shared<Fact>();
            customer->type = "Customer";
            customer->fields["id"] = int64_t(7);

            auto matching_order_one = std::make_shared<Fact>();
            matching_order_one->type = "Order";
            matching_order_one->fields["customerId"] = int64_t(7);

            auto matching_order_two = std::make_shared<Fact>();
            matching_order_two->type = "Order";
            matching_order_two->fields["customerId"] = int64_t(7);

            auto unrelated_order = std::make_shared<Fact>();
            unrelated_order->type = "Order";
            unrelated_order->fields["customerId"] = int64_t(9);

            session->add_fact(customer);
            session->add_fact(matching_order_one);
            session->add_fact(matching_order_two);
            session->add_fact(unrelated_order);

            rulesforge::rhs_prof::reset_stats();
            check(session->fire_all_rules() == 2);
            auto stats = rulesforge::rhs_prof::get_stats();
            check(stats.turboscript_command_exec_count == 2);
            check(stats.turboscript_command_error_count == 0);

            auto results = session->execute_query("findResults");
            check(results.success());
            check(results.size() == 2);
            for (auto row : results) {
                auto result_fact = row.get("$r");
                check(result_fact.has_value());
                check(*result_fact != nullptr);
                auto customer_id = (*result_fact)->get_field("customerId");
                check(customer_id.has_value());
                check(std::holds_alternative<int64_t>(*customer_id));
                check(std::get<int64_t>(*customer_id) == 7);
            }
        }
    }

    group("Alpha modify propagation") {
        it("does not let unrelated field updates bypass alpha constraints") {
            auto session = build_session(R"(
                declare Order
                    quantity: int
                    unitPrice: double
                    finalPrice: double
                end

                rule "No Discount"
                    no-loop
                when
                    $order : Order(quantity >= 1, quantity <= 2, finalPrice <= 0.01)
                then
                    update $order { finalPrice = $order.quantity * $order.unitPrice }
                end

                rule "Round Down Payment"
                    salience -10
                    no-loop
                when
                    $order : Order(quantity > 2, finalPrice > 0.01)
                then
                    update $order { finalPrice = floor($order.finalPrice) }
                end
            )");

            auto order = std::make_shared<Fact>();
            order->type = "Order";
            order->fields["quantity"] = static_cast<int64_t>(1);
            order->fields["unitPrice"] = 25.99;
            order->fields["finalPrice"] = 0.01;

            session->add_fact(order);

            check(session->fire_all_rules() == 1);
            check(std::holds_alternative<double>(order->fields["finalPrice"]));
            check(std::abs(std::get<double>(order->fields["finalPrice"]) - 25.99) < 0.0001);
        }

        it("reevaluates numeric expression constraints on modify paths") {
            auto session = build_session(R"(
                declare Order
                    quantity: int
                    unitPrice: double
                    finalPrice: double
                    note: String
                end

                rule "Touch Matching Order"
                when
                    $order : Order(finalPrice <= ($quantity * $unitPrice))
                then
                end
            )");

            auto order = std::make_shared<Fact>();
            order->type = "Order";
            order->fields["quantity"] = int64_t(2);
            order->fields["unitPrice"] = 10.0;
            order->fields["finalPrice"] = 25.0;
            order->fields["note"] = "initial";

            session->add_fact(order);
            check(session->fire_all_rules() == 0);

            session->update_fact(order.get(), [](Fact& fact) {
                fact.fields["finalPrice"] = 20.0;
            });

            check(session->fire_all_rules() == 1);
        }

    }

    group("'not' Pattern") {
        it("runs existence orchestration while predicates stay MIR-backed") {
            auto session = build_session(R"(
                declare Person name:String end
                declare Holiday name:String end
                rule "Work Day"
                when
                    Person()
                    not (Holiday())
                then
                end
            )");
            check(session->get_knowledge_base()->mir_rule_coverages()[0].lowering_errors.empty());

            auto person = std::make_shared<Fact>();
            person->type = "Person";
            person->fields["name"] = "Ada";
            session->add_fact(person);
            check(session->fire_all_rules() == 1);

            auto holiday = std::make_shared<Fact>();
            holiday->type = "Holiday";
            holiday->fields["name"] = "Founders Day";
            session->add_fact(holiday);
            check(session->fire_all_rules() == 0);
        }
    }

    group("Salience") {
        it("respects rule salience order") {
            auto session = build_session(R"(
                declare Trigger end
                declare Result name:String end
                rule "High Salience" salience 10 when Trigger() then
                    insert Result { name = "High" }
                end
                rule "Low Salience" salience 5 when Trigger() then
                    insert Result { name = "Low" }
                end
            )");

            auto fact = std::make_shared<Fact>();
            fact->type = "Trigger";
            session->add_fact(fact);
            int fired = session->fire_all_rules();

            check(fired == 2);
            check(session->get_fact_count() == 3);
        }
    }

    group("Logical Insertions (TMS)") {
        it("retracts logical facts when support is removed") {
            auto session = build_session(R"(
                declare Alarm reason:String end
                declare Fire active:bool end
                rule "Sound Alarm on Fire"
                when
                    $f : Fire(active == true)
                then
                    insertLogical Alarm { reason = "fire" }
                end
            )");

            auto fire_fact = std::make_shared<Fact>(Fact{0, "Fire", {{"active", (int64_t)1}}});
            session->add_fact(fire_fact);
            session->fire_all_rules();

            check(session->get_fact_count() == 2);

            session->retract_fact(fire_fact);
            check(session->get_fact_count() == 0);
        }
    }

    group("'or' Condition") {
        it("fires due to Gold status") {
            auto session = build_session(R"(
                declare Customer
                    id : int
                    status : String
                end
                declare Order
                    customerId : int
                    amount : double
                end
                declare PremiumCustomer
                    id : int
                end

                rule "Identify Premium Customers"
                when
                    $c1 : Customer( status == "Gold" )
                    or
                    $c2 : Customer( $id : id )
                    Order( customerId == $id, amount > 500.0 )
                then
                    insert PremiumCustomer { }
                end
            )");

            auto customer = std::make_shared<Fact>();
            customer->type = "Customer";
            customer->fields["id"] = (int64_t)1;
            customer->fields["status"] = "Gold";

            session->add_fact(customer);
            check(session->fire_all_rules() == 1);
            check(session->get_fact_count() == 2);
        }

        it("fires due to high-value order") {
            auto session = build_session(R"(
                declare Customer
                    id : int
                    status : String
                end
                declare Order
                    customerId : int
                    amount : double
                end
                declare PremiumCustomer
                    id : int
                end

                rule "Identify Premium Customers"
                when
                    $c1 : Customer( status == "Gold" )
                    or
                    $c2 : Customer( $id : id )
                    Order( customerId == $id, amount > 500.0 )
                then
                    insert PremiumCustomer { }
                end
            )");

            auto customer = std::make_shared<Fact>();
            customer->type = "Customer";
            customer->fields["id"] = (int64_t)2;
            customer->fields["status"] = "Silver";

            auto order = std::make_shared<Fact>();
            order->type = "Order";
            order->fields["customerId"] = (int64_t)2;
            order->fields["amount"] = 600.0;

            session->add_fact(customer);
            session->add_fact(order);
            check(session->fire_all_rules() == 1);
            check(session->get_fact_count() == 3);
        }

        it("does not fire for non-premium") {
            auto session = build_session(R"(
                declare Customer
                    id : int
                    status : String
                end
                declare Order
                    customerId : int
                    amount : double
                end
                declare PremiumCustomer
                    id : int
                end

                rule "Identify Premium Customers"
                when
                    $c1 : Customer( status == "Gold" )
                    or
                    $c2 : Customer( $id : id )
                    Order( customerId == $id, amount > 500.0 )
                then
                    insert PremiumCustomer { }
                end
            )");

            auto customer = std::make_shared<Fact>();
            customer->type = "Customer";
            customer->fields["id"] = (int64_t)3;
            customer->fields["status"] = "Bronze";

            auto order = std::make_shared<Fact>();
            order->type = "Order";
            order->fields["customerId"] = (int64_t)3;
            order->fields["amount"] = 100.0;

            session->add_fact(customer);
            session->add_fact(order);
            check(session->fire_all_rules() == 0);
            check(session->get_fact_count() == 2);
        }
    }
}
