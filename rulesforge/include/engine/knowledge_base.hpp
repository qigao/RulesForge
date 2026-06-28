#ifndef KNOWLEDGE_BASE_HPP
#define KNOWLEDGE_BASE_HPP

#include "core/rfl_parser_state.hpp"
#include "data/fact_type_registry.hpp"
#include "engine/mir_execution_plan.hpp"
#include "engine/rhs_backend_plan.hpp"

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations
class StatefulSession;
class ReteNode;
class QueryTerminalNode;
class QueryInputNode;
class AccumulatorRegistry;
struct IAccumulator;
struct ParsedRule;
struct ParsedQuery;
struct ParsedPattern;
struct ConstraintNode;
struct CompiledNetwork;

// Native function callback type (shared with JSScriptingManager)
using NativeFunctionCallback = int (*)(void* ctx, int argc, const char** argv, char** out_result);

struct NativeFunction {
    NativeFunctionCallback callback;
    void* user_data;
};

class KnowledgeBase : public std::enable_shared_from_this<KnowledgeBase> {
public:
    struct private_key {
        explicit private_key() = default;
    };

    explicit KnowledgeBase(private_key);

    ~KnowledgeBase();

    KnowledgeBase(KnowledgeBase const&) = delete;
    KnowledgeBase& operator=(KnowledgeBase const&) = delete;

    // --- FACTORIES ---
    static std::shared_ptr<KnowledgeBase> create(parser_state&& state);
    static std::shared_ptr<KnowledgeBase> create_empty();
    using MirExecutionPlanCompiler =
        std::unique_ptr<rulesforge::MirExecutionPlan> (*)(std::vector<ParsedRule> const&,
                                                          std::vector<ParsedQuery> const&,
                                                          std::vector<ParsedDeclaration> const&,
                                                          std::string*);
    using MirExecutionPlanSelfCheck = bool (*)(rulesforge::MirExecutionPlan const&,
                                               std::vector<ParsedRule> const&,
                                               std::string*);
    // Testing hooks for MIR failure boundaries; passing nullptr restores the default path.
    static MirExecutionPlanCompiler set_mir_execution_plan_compiler_for_testing(
        MirExecutionPlanCompiler compiler);
    static MirExecutionPlanSelfCheck set_mir_execution_plan_self_check_for_testing(
        MirExecutionPlanSelfCheck self_check);

    std::unique_ptr<StatefulSession> create_session();
    ParsedRule const* find_rule_by_name(std::string const& name) const;
    void set_phreak_experimental(bool enabled) { phreak_experimental_ = enabled; }
    bool is_phreak_experimental() const { return phreak_experimental_; }

    // --- API ---

    // --- Accessors for immutable data ---
    AccumulatorRegistry const& get_accumulator_registry() const;
    FactTypeRegistry& get_fact_type_registry();
    FactTypeRegistry const& get_fact_type_registry() const;

    /**
     * @brief Register a custom accumulate function.
     *
     * Custom accumulators can be used in RFL rules like built-in functions:
     * ```rfl
     * $result : Number() from accumulate(
     *     Order($amount : amount),
     *     myCustomFunc($amount)
     * )
     * ```
     *
     * @param name The function name to use in RFL (e.g., "myCustomFunc")
     * @param prototype A prototype instance that will be cloned for each accumulate node
     *
     * Example:
     * ```cpp
     * class MyAccumulator : public IAccumulator {
     *     void accumulate(ConstraintValue const& value) override { ... }
     *     void reverse(ConstraintValue const& value) override { ... }
     *     ConstraintValue get_result() const override { return result_; }
     *     void clear() override { result_ = 0; }
     *     std::unique_ptr<IAccumulator> clone() const override {
     *         return std::make_unique<MyAccumulator>(*this);
     *     }
     * };
     *
     * kb->register_accumulator("myFunc", std::make_unique<MyAccumulator>());
     * ```
     */
    void register_accumulator(std::string const& name, std::unique_ptr<IAccumulator> prototype);

    // --- Native Function Registration ---
    void register_native_function(std::string const& name, NativeFunctionCallback callback, void* user_data);
    std::map<std::string, NativeFunction> const& get_native_functions() const { return native_functions_; }
    std::size_t register_native_predicate(std::string const& name,
                                          NativeFunctionCallback callback,
                                          void* user_data);
    std::optional<std::size_t> native_predicate_id(std::string const& name) const;

    std::vector<ParsedRule> const& get_rules() const { return processed_rules_; }

    parser_state const& get_parser_state() const { return parser_state_; }
    bool has_mir_execution_plan() const { return static_cast<bool>(mir_execution_plan_); }
    rulesforge::MirExecutionPlanSummary mir_summary() const;
    std::string mir_summary_text() const;
    std::string mir_debug_dump() const;
    std::int64_t mir_rule_count() const;
    std::int64_t mir_rule_salience(std::size_t rule_index) const;
    bool mir_rule_enabled(std::size_t rule_index) const;
    std::vector<rulesforge::MirRuleCoverage> const& mir_rule_coverages() const;
    std::vector<rulesforge::MirQueryCoverage> const& mir_query_coverages() const;
    std::vector<rulesforge::MirRuleGraph> const& mir_rule_graphs() const;
    std::vector<rulesforge::MirQueryGraph> const& mir_query_graphs() const;
    rulesforge::RhsBackendPlanSummary rhs_backend_summary() const;
    std::string rhs_backend_summary_text() const;
    std::string rhs_backend_debug_dump() const;
    std::vector<rulesforge::RhsBackendCoverage> const& rhs_backend_coverages() const;
    std::optional<std::size_t> mir_compare_predicate_id(CompareOp op) const;
    std::optional<std::size_t> mir_numeric_compare_predicate_id(CompareOp op) const;
    std::optional<std::size_t> mir_numeric_literal_predicate_id(CompareOp op,
                                                                ConstraintValue const& literal) const;
    std::optional<std::size_t> mir_numeric_expression_predicate_id(CompareOp op,
                                                                   std::string const& expression) const;
    std::vector<std::string> const* mir_numeric_expression_predicate_variables(std::size_t predicate_id) const;
    std::optional<std::size_t> mir_numeric_value_expression_id(std::string const& expression) const;
    std::vector<std::string> const* mir_numeric_value_expression_variables(std::size_t expression_id) const;
    std::optional<double> mir_runtime_numeric_value_expression(std::size_t expression_id,
                                                               std::vector<ConstraintValue> const& args) const;
    std::optional<std::size_t> mir_value_list_predicate_id(CompareOp op,
                                                           std::vector<ConstraintValue> const& values) const;
    std::optional<std::size_t> mir_eval_expression_predicate_id(std::string const& expression) const;
    std::vector<std::string> const* mir_eval_expression_predicate_variables(std::size_t predicate_id) const;
    std::optional<bool> runtime_predicate(rulesforge::RuntimePredicateRef predicate,
                                          std::vector<ConstraintValue> const& args) const;
    std::optional<bool> mir_runtime_predicate(rulesforge::MirRuntimePredicateRef predicate,
                                              std::vector<ConstraintValue> const& args) const;
    std::optional<std::int64_t> mir_runtime_count_aggregate(std::int64_t current,
                                                            std::int64_t direction) const;
    std::optional<double> mir_runtime_sum_aggregate(double current,
                                                    ConstraintValue const& value,
                                                    std::int64_t direction) const;
    std::optional<double> mir_runtime_min_aggregate(double current,
                                                    ConstraintValue const& value) const;
    std::optional<double> mir_runtime_max_aggregate(double current,
                                                    ConstraintValue const& value) const;
    std::optional<bool> mir_runtime_collect_list_update(std::vector<Fact*>& facts,
                                                        Fact* fact,
                                                        std::int64_t direction) const;
    std::optional<bool> mir_runtime_collect_set_update(std::unordered_set<Fact*>& facts,
                                                       Fact* fact,
                                                       std::int64_t direction) const;
    std::optional<FactList> mir_runtime_collect_list_result(std::vector<Fact*> const& facts) const;
    std::optional<FactList> mir_runtime_collect_set_result(std::unordered_set<Fact*> const& facts) const;

    std::unique_ptr<ConstraintNode>
    partition_and_get_alpha_root(ParsedPattern const& pattern,
                                 std::vector<ParsedConstraint>& out_join_constraints) const;

    std::string network_dot() const;
    CompiledNetwork const& network() const;

private:
    friend class BetaNetworkBuilder;
    friend class StatefulSession;

    void build(parser_state&& state);
    void compile_network();
    std::optional<bool> run_native_predicate(std::size_t predicate_id,
                                             std::vector<ConstraintValue> const& args) const;

    parser_state parser_state_;
    std::vector<ParsedRule> processed_rules_;
    std::unordered_map<std::string, size_t> rule_name_index_;  // name -> index in processed_rules_
    std::shared_ptr<AccumulatorRegistry> accumulator_registry_;
    std::map<std::string, std::chrono::milliseconds> type_expiration_policies_;
    FactTypeRegistry fact_type_registry_;
    std::map<std::string, NativeFunction> native_functions_;
    std::map<std::string, std::size_t> native_predicate_name_to_id_;
    std::vector<NativeFunction> native_predicates_;
    std::unique_ptr<CompiledNetwork> compiled_network_;
    std::unique_ptr<rulesforge::MirExecutionPlan> mir_execution_plan_;
    std::unique_ptr<rulesforge::RhsBackendPlan> rhs_backend_plan_;
    bool phreak_experimental_ = false;
};

#endif   // KNOWLEDGE_BASE_HPP
