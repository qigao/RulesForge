#ifndef KNOWLEDGE_BASE_HPP
#define KNOWLEDGE_BASE_HPP

#include "rfl_parser_state.hpp"
#include "fact_type_registry.hpp"

#include <chrono>
#include <map>
#include <memory>
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

    std::unique_ptr<StatefulSession> create_session();
    ParsedRule const* find_rule_by_name(std::string const& name) const;

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

    std::vector<ParsedRule> const& get_rules() const { return processed_rules_; }

    parser_state const& get_parser_state() const { return parser_state_; }

    std::unique_ptr<ConstraintNode>
    partition_and_get_alpha_root(ParsedPattern const& pattern,
                                 std::vector<ParsedConstraint>& out_join_constraints) const;

    CompiledNetwork const& network() const;

private:
    friend class BetaNetworkBuilder;
    friend class StatefulSession;

    void build(parser_state&& state);
    void compile_network();

    parser_state parser_state_;
    std::vector<ParsedRule> processed_rules_;
    std::unordered_map<std::string, size_t> rule_name_index_;  // name -> index in processed_rules_
    std::shared_ptr<AccumulatorRegistry> accumulator_registry_;
    ruleforge::map<std::string, std::chrono::milliseconds> type_expiration_policies_;
    FactTypeRegistry fact_type_registry_;
    std::map<std::string, NativeFunction> native_functions_;
    std::unique_ptr<CompiledNetwork> compiled_network_;
};

#endif   // KNOWLEDGE_BASE_HPP


