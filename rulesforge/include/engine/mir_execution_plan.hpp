#ifndef MIR_EXECUTION_PLAN_HPP
#define MIR_EXECUTION_PLAN_HPP

#include "core/constraint_types.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct ParsedQuery;
struct ParsedRule;
struct ParsedDeclaration;

namespace rulesforge {

struct MirRuleCoverage {
    std::size_t rule_index = 0;
    std::string rule_name;
    std::size_t constraint_count = 0;
    std::size_t compare_predicate_count = 0;
    std::size_t literal_predicate_count = 0;
    std::size_t lowering_error_constraint_count = 0;
    std::vector<std::string> lowering_errors;
};

struct MirQueryCoverage {
    std::size_t query_index = 0;
    std::string query_name;
    std::size_t constraint_count = 0;
    std::size_t compare_predicate_count = 0;
    std::size_t literal_predicate_count = 0;
    std::size_t lowering_error_constraint_count = 0;
    std::vector<std::string> lowering_errors;
};

struct MirExecutionPlanSummary {
    bool available = false;
    std::size_t rule_count = 0;
    std::size_t query_count = 0;
    std::size_t fixed_i64_compare_count = 0;
    std::size_t fixed_double_compare_count = 0;
    std::size_t fixed_string_compare_count = 0;
    std::size_t compare_predicate_count = 0;
    std::size_t numeric_literal_predicate_count = 0;
    std::size_t numeric_expression_predicate_count = 0;
    std::size_t numeric_value_expression_count = 0;
    std::size_t value_list_predicate_count = 0;
    std::size_t eval_expression_predicate_count = 0;
    bool has_generic_i64_compare = false;
    bool has_generic_double_compare = false;
    bool supports_numeric_compare_predicates = false;
    bool supports_string_compare_predicates = false;
    bool supports_string_compare = false;
    bool supports_string_contains = false;
    bool supports_string_matches = false;
    bool supports_string_affix = false;
    bool supports_string_length_is = false;
    bool supports_numeric_literal_predicates = false;
    std::size_t rule_coverage_count = 0;
    std::size_t query_coverage_count = 0;
    std::size_t rule_graph_count = 0;
    std::size_t rule_graph_predicate_count = 0;
    std::size_t query_graph_count = 0;
    std::size_t query_graph_predicate_count = 0;
    std::vector<std::string> lowering_errors;
};

enum class MirRuntimePredicateKind : uint8_t {
    Compare,
    Temporal,
    StringContains,
    StringMatches,
    StringAffix,
    StringLengthIs,
    MapContainsKey,
    CollectionContains,
    NumericLiteral,
    NumericExpression,
    ValueList,
    EvalExpression,
    External,
};

enum class RuntimePredicateBackend : uint8_t {
    MirJit,
    Native,
};

struct RuntimePredicateRef {
    RuntimePredicateBackend backend = RuntimePredicateBackend::MirJit;
    MirRuntimePredicateKind kind = MirRuntimePredicateKind::Compare;
    std::size_t predicate_id = 0;
    CompareOp compare_op = CompareOp::None;
    TemporalOp temporal_op = TemporalOp::None;
    std::int64_t window_ms = 0;
    std::string external_name;

    RuntimePredicateRef() = default;
    RuntimePredicateRef(MirRuntimePredicateKind kind_,
                        std::size_t predicate_id_ = 0,
                        CompareOp compare_op_ = CompareOp::None,
                        TemporalOp temporal_op_ = TemporalOp::None,
                        std::int64_t window_ms_ = 0,
                        std::string external_name_ = {})
        : backend(RuntimePredicateBackend::MirJit),
          kind(kind_),
          predicate_id(predicate_id_),
          compare_op(compare_op_),
          temporal_op(temporal_op_),
          window_ms(window_ms_),
          external_name(std::move(external_name_)) {}

    RuntimePredicateRef(RuntimePredicateBackend backend_,
                        MirRuntimePredicateKind kind_,
                        std::size_t predicate_id_ = 0,
                        CompareOp compare_op_ = CompareOp::None,
                        TemporalOp temporal_op_ = TemporalOp::None,
                        std::int64_t window_ms_ = 0,
                        std::string external_name_ = {})
        : backend(backend_),
          kind(kind_),
          predicate_id(predicate_id_),
          compare_op(compare_op_),
          temporal_op(temporal_op_),
          window_ms(window_ms_),
          external_name(std::move(external_name_)) {}
};

using MirRuntimePredicateRef = RuntimePredicateRef;

enum class MirRuleGraphNodeKind : uint8_t {
    Standard,
    Not,
    Exists,
    Forall,
    Eval,
    QueryCall,
    Accumulate,
    Unnest,
    Window,
};

enum class MirRulePredicateRole : uint8_t {
    Alpha,
    Join,
    Eval,
};

struct MirRuleGraphPredicate {
    MirRulePredicateRole role = MirRulePredicateRole::Alpha;
    RuntimePredicateRef ref;
};

struct MirPredicateInputAbi {
    bool reads_current_fact = true;
    bool reads_token_facts = true;
    bool uses_field_accessor = true;
    bool missing_field_is_non_match = true;
    bool explicit_nil_is_value = true;
};

struct MirRuleGraphNode {
    std::size_t condition_group_index = 0;
    std::size_t pattern_index = 0;
    std::size_t depth = 0;
    std::optional<std::size_t> parent_node_index;
    MirRuleGraphNodeKind kind = MirRuleGraphNodeKind::Standard;
    std::string source_kind;
    std::string binding;
    std::string fact_type;
    std::size_t constraint_count = 0;
    MirPredicateInputAbi input_abi;
    std::vector<MirRuleGraphPredicate> predicates;
};

struct MirRuleGraph {
    std::size_t rule_index = 0;
    std::string rule_name;
    std::size_t condition_group_index = 0;
    std::vector<MirRuleGraphNode> nodes;
};

struct MirQueryGraphNode {
    std::size_t pattern_index = 0;
    std::string binding;
    std::string fact_type;
    std::size_t constraint_count = 0;
    std::vector<RuntimePredicateRef> predicates;
};

struct MirQueryGraph {
    std::size_t query_index = 0;
    std::string query_name;
    std::vector<MirQueryGraphNode> nodes;
};

class MirExecutionPlan {
public:
    MirExecutionPlan(MirExecutionPlan const&) = delete;
    MirExecutionPlan& operator=(MirExecutionPlan const&) = delete;
    MirExecutionPlan(MirExecutionPlan&&) = delete;
    MirExecutionPlan& operator=(MirExecutionPlan&&) = delete;
    ~MirExecutionPlan();

    static std::unique_ptr<MirExecutionPlan> compile(std::vector<ParsedRule> const& rules,
                                                     std::vector<ParsedQuery> const& queries,
                                                     std::vector<ParsedDeclaration> const& declarations,
                                                     std::string* error_out = nullptr);

    std::int64_t run_rule_count() const;
    std::int64_t run_rule_salience(std::size_t rule_index) const;
    bool run_rule_enabled(std::size_t rule_index) const;
    std::optional<std::size_t> compare_predicate_id(CompareOp op) const;
    std::optional<std::size_t> numeric_compare_predicate_id(CompareOp op) const;
    std::optional<std::size_t> numeric_literal_predicate_id(CompareOp op,
                                                            ConstraintValue const& literal) const;
    std::optional<std::size_t> numeric_expression_predicate_id(CompareOp op,
                                                               std::string const& expression) const;
    std::vector<std::string> const* numeric_expression_predicate_variables(std::size_t predicate_id) const;
    std::optional<std::size_t> numeric_value_expression_id(std::string const& expression) const;
    std::vector<std::string> const* numeric_value_expression_variables(std::size_t expression_id) const;
    std::optional<double> run_runtime_numeric_value_expression(std::size_t expression_id,
                                                               std::vector<ConstraintValue> const& args) const;
    std::optional<std::size_t> value_list_predicate_id(CompareOp op,
                                                       std::vector<ConstraintValue> const& values) const;
    std::optional<std::size_t> eval_expression_predicate_id(std::string const& expression) const;
    std::vector<std::string> const* eval_expression_predicate_variables(std::size_t predicate_id) const;
    std::optional<bool> run_runtime_predicate(RuntimePredicateRef predicate,
                                              std::vector<ConstraintValue> const& args) const;
    std::optional<std::int64_t> run_runtime_count_aggregate(std::int64_t current,
                                                            std::int64_t direction) const;
    std::optional<double> run_runtime_sum_aggregate(double current,
                                                    ConstraintValue const& value,
                                                    std::int64_t direction) const;
    std::optional<double> run_runtime_min_aggregate(double current,
                                                    ConstraintValue const& value) const;
    std::optional<double> run_runtime_max_aggregate(double current,
                                                    ConstraintValue const& value) const;
    std::optional<bool> run_runtime_collect_list_update(std::vector<Fact*>& facts,
                                                        Fact* fact,
                                                        std::int64_t direction) const;
    std::optional<bool> run_runtime_collect_set_update(std::unordered_set<Fact*>& facts,
                                                       Fact* fact,
                                                       std::int64_t direction) const;
    std::optional<FactList> run_runtime_collect_list_result(std::vector<Fact*> const& facts) const;
    std::optional<FactList> run_runtime_collect_set_result(std::unordered_set<Fact*> const& facts) const;
    std::vector<MirRuleCoverage> const& rule_coverages() const { return rule_coverages_; }
    std::vector<MirQueryCoverage> const& query_coverages() const { return query_coverages_; }
    std::vector<MirRuleGraph> const& rule_graphs() const { return rule_graphs_; }
    std::vector<MirQueryGraph> const& query_graphs() const { return query_graphs_; }
    MirExecutionPlanSummary summary() const;
    std::string format_summary() const;
    std::string format_debug_dump() const;
    std::size_t rule_count() const { return rule_count_; }
    std::size_t query_count() const { return query_count_; }

private:
    struct Impl;

    explicit MirExecutionPlan(std::unique_ptr<Impl> impl);

    std::optional<bool> run_temporal_predicate(TemporalOp op,
                                               std::int64_t lhs,
                                               std::int64_t rhs,
                                               std::int64_t window_ms) const;
    std::optional<bool> run_string_contains(CompareOp op, std::string const& lhs, std::string const& rhs) const;
    std::optional<bool> run_string_matches(CompareOp op, std::string const& lhs, std::string const& rhs) const;
    std::optional<bool> run_string_affix(CompareOp op, std::string const& lhs, std::string const& rhs) const;
    std::optional<bool> run_string_length_is(std::string const& lhs, std::int64_t rhs) const;
    std::optional<bool> run_map_contains_key(CompareOp op,
                                             ConstraintValue const& lhs,
                                             ConstraintValue const& rhs) const;
    std::optional<bool> run_collection_contains(CompareOp op,
                                                ConstraintValue const& lhs,
                                                ConstraintValue const& rhs) const;
    std::optional<bool> run_compare_predicate(std::size_t predicate_id,
                                              ConstraintValue const& lhs,
                                              ConstraintValue const& rhs) const;
    std::optional<bool> run_numeric_literal_predicate(std::size_t predicate_id,
                                                      ConstraintValue const& lhs) const;
    std::optional<bool> run_numeric_expression_predicate(std::size_t predicate_id,
                                                         ConstraintValue const& lhs,
                                                         std::vector<ConstraintValue> const& args) const;
    std::optional<double> run_numeric_value_expression(std::size_t expression_id,
                                                       std::vector<ConstraintValue> const& args) const;
    std::optional<bool> run_value_list_predicate(std::size_t predicate_id,
                                                 ConstraintValue const& lhs) const;
    std::optional<bool> run_eval_expression_predicate(std::size_t predicate_id,
                                                      std::vector<ConstraintValue> const& args) const;
    std::optional<std::int64_t> run_count_aggregate(std::int64_t current,
                                                    std::int64_t direction) const;
    std::optional<double> run_sum_aggregate(double current,
                                            ConstraintValue const& value,
                                            std::int64_t direction) const;
    std::optional<double> run_min_aggregate(double current,
                                            ConstraintValue const& value) const;
    std::optional<double> run_max_aggregate(double current,
                                            ConstraintValue const& value) const;

    std::unique_ptr<Impl> impl_;
    std::size_t rule_count_ = 0;
    std::size_t query_count_ = 0;
    std::vector<MirRuleCoverage> rule_coverages_;
    std::vector<MirQueryCoverage> query_coverages_;
    std::vector<MirRuleGraph> rule_graphs_;
    std::vector<MirQueryGraph> query_graphs_;
};

} // namespace rulesforge

#endif // MIR_EXECUTION_PLAN_HPP
