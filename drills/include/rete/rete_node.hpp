#ifndef RETE_NODE_HPP
#define RETE_NODE_HPP

#include "drools_accumulators.hpp"
#include "drools_rete_defs.hpp"

#include <iosfwd>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations
class StatefulSession;
struct Token;
struct Fact;
enum class PropagationType;
class TokenWME;
class QueryInputNode;
// Forward declare all node types for friend declarations
class AlphaNode;
class EntryPointNode;
class HashedJoinNode;
class CrossProductJoinNode;
class NotNode;
class ExistsNode;
class AccumulateNode;
class UnnestNode;
class EvalNode;
class TerminalNode;
class QueryTerminalNode;

// --- BASE CLASS ---
/**
 * @class ReteNode
 * @brief Abstract base class representing a node in a Rete network.
 *
 * ReteNode serves as the foundational interface for all nodes in the Rete algorithm's network.
 * It provides mechanisms for activation from the left and right, management of parent and child nodes,
 * and node identification. Derived classes must implement the activation and printing logic.
 *
 * @note Inherits from std::enable_shared_from_this to allow safe shared_ptr usage.
 */
class ReteNode : public std::enable_shared_from_this<ReteNode> {
public:
    virtual ~ReteNode() = default;
    /**
     * @brief Activates the node with a token from the left input.
     *
     * This pure virtual function is called when a token is propagated from the left input
     * of the node. Implementations should define how the node processes the token within
     * the given session context.
     *
     * @param session Reference to the current StatefulSession, providing context for activation.
     * @param token Shared pointer to the Token being activated on the left input.
     */
    virtual void left_activate(StatefulSession& session, std::shared_ptr<Token> token) = 0;
    /**
     * @brief Activates the right input of the node with the given fact.
     *
     * This method is called when a fact is asserted or modified on the right input of the node.
     * It processes the provided fact within the context of the specified session and propagates
     * the activation according to the given propagation type.
     *
     * @param session Reference to the current stateful session.
     * @param fact Shared pointer to the fact being activated.
     * @param p_type The type of propagation (e.g., assert, retract, modify).
     */
    virtual void right_activate(StatefulSession& session, std::shared_ptr<Fact> fact, PropagationType p_type) = 0;
    void add_child(std::shared_ptr<ReteNode> child);
    void add_parent(std::shared_ptr<ReteNode> parent);
    virtual void print_node(std::ostream& os) const = 0;

    std::vector<std::weak_ptr<ReteNode>> const& get_children() const { return children; }

    std::vector<std::weak_ptr<ReteNode>> const& get_parents() const { return parents; }

    int id = -1;

    uintptr_t get_id() const { return reinterpret_cast<uintptr_t>(this); }

protected:
    std::vector<std::weak_ptr<ReteNode>> children;
    std::vector<std::weak_ptr<ReteNode>> parents;
};

class BetaConditionNode : public ReteNode {
public:
    BetaConditionNode() = default;
    BetaConditionNode(std::vector<ParsedConstraint> const& joins, std::map<std::string, int> const& bindings);
    void left_activate(StatefulSession& session, std::shared_ptr<Token> token) override;
    void right_activate(StatefulSession& session, std::shared_ptr<Fact> fact, PropagationType p_type) override;

protected:
    virtual bool condition_passes(size_t match_count) const = 0;
    virtual bool was_passing(size_t old_match_count) const = 0;

    struct LeftMemoryItem {
        std::shared_ptr<TokenWME const> wme;
        size_t match_count = 0;
    };

    std::unordered_map<TokenWME const*, LeftMemoryItem> left_memory;
    std::unordered_map<int64_t, std::shared_ptr<Fact>> right_memory;
    std::vector<ParsedConstraint> join_constraints;
    std::map<std::string, int> binding_to_token_idx;
};

// --- NODE SUBCLASSES ---

class AlphaNode : public ReteNode {
public:
    AlphaNode() = default;
    explicit AlphaNode(ParsedConstraint const& constraint);
    void left_activate(StatefulSession&, std::shared_ptr<Token>) override;
    void right_activate(StatefulSession&, std::shared_ptr<Fact>, PropagationType) override;
    void print_node(std::ostream& os) const override;


private:
    ParsedConstraint constraint;
    bool check_constraint(Fact const& fact) const;
};

class EntryPointNode : public ReteNode {
public:
    void left_activate(StatefulSession&, std::shared_ptr<Token>) override;
    void right_activate(StatefulSession&, std::shared_ptr<Fact>, PropagationType) override;
    void print_node(std::ostream& os) const override;

    friend class ReteSerializer;
};

// =========================================================================
// === JOIN NODE FAMILY ====================================================
// =========================================================================

class BaseJoinNode : public ReteNode {
public:
    BaseJoinNode(std::vector<ParsedConstraint> joins, std::map<std::string, int> bindings);
    void left_activate(StatefulSession& session, std::shared_ptr<Token> token) override = 0;
    void right_activate(StatefulSession& session, std::shared_ptr<Fact> fact, PropagationType p_type) override = 0;

protected:
    void propagate_assert(StatefulSession& session, std::shared_ptr<Token> token, std::shared_ptr<Fact> fact);
    void propagate_retract(StatefulSession& session, std::shared_ptr<TokenWME const> wme, std::shared_ptr<Fact> fact);

    std::vector<ParsedConstraint> join_constraints_;
    std::map<std::string, int> binding_to_token_idx_;
    std::unordered_map<TokenWME const*, std::vector<std::shared_ptr<TokenWME const>>> left_to_children_;
    std::unordered_map<int64_t, std::vector<std::shared_ptr<TokenWME const>>> right_to_children_;
};

class HashedJoinNode : public BaseJoinNode {
public:
    using HashedTokenMemory =
        std::unordered_map<ConstraintValue, std::vector<std::shared_ptr<TokenWME const>>, ConstraintValueHasher>;
    using HashedFactMemory =
        std::unordered_map<ConstraintValue, std::vector<std::shared_ptr<Fact>>, ConstraintValueHasher>;

    HashedJoinNode(std::vector<ParsedConstraint> joins, std::map<std::string, int> bindings,
                   std::pair<std::string, int> left_hash_key, std::string right_hash_key);
    void left_activate(StatefulSession& session, std::shared_ptr<Token> token) override;
    void right_activate(StatefulSession& session, std::shared_ptr<Fact> fact, PropagationType p_type) override;
    void print_node(std::ostream& os) const override;

    friend class ReteSerializer;

private:
    std::optional<ConstraintValue> get_key(std::shared_ptr<Token> const& token) const;
    std::optional<ConstraintValue> get_key(std::shared_ptr<Fact> const& fact) const;
    HashedTokenMemory left_memory_;
    HashedFactMemory right_memory_;
    std::pair<std::string, int> left_hash_key_;
    std::string right_hash_key_;
};

class CrossProductJoinNode : public BaseJoinNode {
public:
    using TokenMemory = std::unordered_map<TokenWME const*, std::shared_ptr<TokenWME const>>;
    using FactMemory = std::unordered_map<int64_t, std::shared_ptr<Fact>>;

    CrossProductJoinNode(std::vector<ParsedConstraint> joins, std::map<std::string, int> bindings);
    void left_activate(StatefulSession& session, std::shared_ptr<Token> token) override;
    void right_activate(StatefulSession& session, std::shared_ptr<Fact> fact, PropagationType p_type) override;
    void print_node(std::ostream& os) const override;
    friend class ReteSerializer;

private:
    TokenMemory left_memory_;
    FactMemory right_memory_;
};

class OrderedJoinNode : public BaseJoinNode {
public:
    OrderedJoinNode(std::vector<ParsedConstraint> joins, std::map<std::string, int> bindings);
    void left_activate(StatefulSession& session, std::shared_ptr<Token> token) override;
    void right_activate(StatefulSession& session, std::shared_ptr<Fact> fact, PropagationType p_type) override;
    void print_node(std::ostream& os) const override;
};

class NotNode : public BetaConditionNode {
public:
    NotNode() = default;
    NotNode(std::vector<ParsedConstraint> const& joins, std::map<std::string, int> const& bindings);
    void print_node(std::ostream& os) const override;

protected:
    bool condition_passes(size_t match_count) const override { return match_count == 0; }

    bool was_passing(size_t old_match_count) const override { return old_match_count == 0; }
};

class ExistsNode : public BetaConditionNode {
public:
    ExistsNode() = default;
    ExistsNode(std::vector<ParsedConstraint> const& joins, std::map<std::string, int> const& bindings);
    void print_node(std::ostream& os) const override;


protected:
    bool condition_passes(size_t match_count) const override { return match_count > 0; }

    bool was_passing(size_t old_match_count) const override { return old_match_count > 0; }
};

class AccumulateNode : public ReteNode {
public:
    AccumulateNode() = default;
    AccumulateNode(IAccumulator const* prototype, ParsedAccumulate&&, std::string res_fact_type,
                   std::map<std::string, int> bindings, std::vector<ParsedConstraint> joins);
    void left_activate(StatefulSession&, std::shared_ptr<Token>) override;
    void right_activate(StatefulSession&, std::shared_ptr<Fact>, PropagationType) override;
    void print_node(std::ostream& os) const override;
    friend class KnowledgeBase;
    friend class ReteSerializer;

private:
    struct LeftMemoryItem {
        std::shared_ptr<TokenWME const> wme;
        std::shared_ptr<Fact> result_fact;
        std::unique_ptr<IAccumulator> accumulator;
        std::vector<std::shared_ptr<Fact>> contributing_facts_list;
        std::unordered_set<std::shared_ptr<Fact>> contributing_facts_set;
    };

    void update_and_propagate_result(StatefulSession& session, LeftMemoryItem& item);

    IAccumulator const* accumulator_prototype = nullptr;
    ParsedAccumulate info;
    std::string result_fact_type;
    std::map<std::string, int> binding_to_token_idx;
    std::vector<ParsedConstraint> join_constraints;
    std::unordered_map<TokenWME const*, LeftMemoryItem> left_memory;
    std::unordered_map<int64_t, std::shared_ptr<Fact>> right_memory;
};

class UnnestNode : public ReteNode {
public:
    UnnestNode() = default;
    UnnestNode(ParsedUnnest const&, std::map<std::string, int> const&);
    void left_activate(StatefulSession&, std::shared_ptr<Token>) override;

    void right_activate(StatefulSession&, std::shared_ptr<Fact>, PropagationType) override {}

    void print_node(std::ostream& os) const override;
    friend class ReteSerializer;

private:
    ParsedUnnest info;
    std::map<std::string, int> binding_to_token_idx;
    std::unordered_map<TokenWME const*,
                       std::pair<std::shared_ptr<TokenWME const>, std::vector<std::shared_ptr<TokenWME const>>>>
        parent_to_children_map;
};

class EvalNode : public ReteNode {
public:
    EvalNode() = default;
    EvalNode(std::string expression, std::map<std::string, int> bindings);
    void left_activate(StatefulSession& session, std::shared_ptr<Token> token) override;

    void right_activate(StatefulSession&, std::shared_ptr<Fact>, PropagationType) override {}

    void print_node(std::ostream& os) const override;
    friend class ReteSerializer;

private:
    std::string expression;
    std::map<std::string, int> binding_to_token_idx;
    std::unordered_map<TokenWME const*, std::shared_ptr<TokenWME const>> memory;
};

class TerminalNode : public ReteNode {
public:
    TerminalNode() = default;
    TerminalNode(ParsedRule const& rule, std::map<std::string, int> bindings);
    void left_activate(StatefulSession&, std::shared_ptr<Token>) override;

    void right_activate(StatefulSession&, std::shared_ptr<Fact>, PropagationType) override {}

    void print_node(std::ostream& os) const override;
    friend class ReteSerializer;

private:
    std::string rule_name;
    std::map<std::string, int> binding_to_token_idx;
    std::unordered_set<TokenWME const*> memory_;
};

class QueryTerminalNode : public ReteNode {
public:
    QueryTerminalNode() = default;
    explicit QueryTerminalNode(std::map<std::string, int> bindings);
    void left_activate(StatefulSession&, std::shared_ptr<Token>) override;

    void right_activate(StatefulSession&, std::shared_ptr<Fact>, PropagationType) override {}

    std::map<TokenWME const*, std::shared_ptr<Token>> const& get_results() { return results; }

    void clear_results();
    void set_bindings(std::map<std::string, int> const& bindings);

    std::map<std::string, int> const& get_bindings() const { return binding_to_token_idx; }

    void print_node(std::ostream& os) const override;
    friend class ReteSerializer;

private:
    std::map<TokenWME const*, std::shared_ptr<Token>> results;
    std::map<std::string, int> binding_to_token_idx;
};

class QueryInputNode : public ReteNode {
public:
    QueryInputNode() = default;
    explicit QueryInputNode(std::shared_ptr<QueryTerminalNode> terminal_node);

    void left_activate(StatefulSession&, std::shared_ptr<Token>) override {}

    void right_activate(StatefulSession&, std::shared_ptr<Fact>, PropagationType) override {}

    void execute(StatefulSession& session, std::vector<std::shared_ptr<Fact>> const& args);
    void print_node(std::ostream& os) const override;
    friend class KnowledgeBase;
    friend class ReteSerializer;

private:
    std::weak_ptr<QueryTerminalNode> terminal_node;
};

#endif   // RETE_NODE_HPP
