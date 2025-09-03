#ifndef KNOWLEDGE_BASE_HPP
#define KNOWLEDGE_BASE_HPP

#include "drools_parser_state.hpp"
#include "fact_type_registry.hpp"

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Forward declarations
class StatefulSession;
class ReteNode;
class QueryTerminalNode;
class QueryInputNode;
class AccumulatorRegistry;
struct ParsedRule;
struct ParsedPattern;
struct ConstraintNode;

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
    static std::shared_ptr<KnowledgeBase> create(parser_state& state);

    std::unique_ptr<StatefulSession> create_session();
    ParsedRule const* find_rule_by_name(std::string const& name) const;

    // --- API ---

    // --- Accessors for immutable data ---
    AccumulatorRegistry const& get_accumulator_registry() const;
    FactTypeRegistry& get_fact_type_registry();
    FactTypeRegistry const& get_fact_type_registry() const;

    std::vector<ParsedRule> const& get_rules() const { return processed_rules_; }

    parser_state const& get_parser_state() const { return parser_state_; }

    std::unique_ptr<ConstraintNode>
    partition_and_get_alpha_root(ParsedPattern const& pattern,
                                 std::vector<ParsedConstraint>& out_join_constraints) const;

private:
    friend class BetaNetworkBuilder;
    friend class StatefulSession;

    void build(parser_state& state);

    parser_state parser_state_;
    std::vector<ParsedRule> processed_rules_;
    std::shared_ptr<AccumulatorRegistry> accumulator_registry_;
    map<std::string, std::chrono::milliseconds> type_expiration_policies_;
    FactTypeRegistry fact_type_registry_;
};

#endif   // KNOWLEDGE_BASE_HPP
