#ifndef RETE_NODE_HPP
#define RETE_NODE_HPP

#include <iosfwd>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "rfl_accumulators.hpp"
#include "rfl_rete_defs.hpp"
#include "rete/network_memory.hpp"
#include "phmap.h"

// Forward declarations
class StatefulSession;
struct Token;
struct Fact;
enum class PropagationType;
struct TokenWME;
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

// Node kind tag for O(1) type checks — replaces dynamic_cast in hot paths
enum class NodeKind : uint8_t {
  Alpha,
  EntryPoint,
  HashedJoin,
  CrossProductJoin,
  Not,
  Exists,
  Accumulate,
  Unnest,
  Eval,
  Terminal,
  QueryTerminal,
  QueryInput,
};

// --- BASE CLASS ---
class ReteNode : public std::enable_shared_from_this<ReteNode>
{
public:
  NodeKind const kind;
  virtual ~ReteNode() = default;

  bool is_beta_node() const {
    switch (kind) {
      case NodeKind::HashedJoin:
      case NodeKind::CrossProductJoin:
      case NodeKind::Not:
      case NodeKind::Exists:
      case NodeKind::Accumulate:
      case NodeKind::Eval:
      case NodeKind::Unnest:
      case NodeKind::QueryInput:
        return true;
      default:
        return false;
    }
  }

  virtual void left_activate(StatefulSession& session,
                             Token const& token) = 0;
  virtual void right_activate(StatefulSession& session,
                              std::shared_ptr<Fact> fact,
                              PropagationType p_type) = 0;

  // Batch right-activation: propagate a vector of facts in one pass.
  // Default falls back to per-fact right_activate(). Alpha-chain nodes
  // override this to filter the entire vector before forwarding survivors.
  virtual void right_activate_batch(StatefulSession& session,
                                    std::vector<std::shared_ptr<Fact>>& facts,
                                    PropagationType p_type) {
    for (auto& fact : facts) {
      right_activate(session, fact, p_type);
    }
  }

  // Deferred evaluation: queue facts for later processing.
  // Default is no-op. Beta nodes override to store into pending queue.
  virtual void right_activate_deferred(StatefulSession& session,
                                       std::shared_ptr<Fact> fact,
                                       PropagationType p_type) {
    right_activate(session, fact, p_type);
  }

  // Batch deferred: queue a vector of facts for later processing.
  virtual void right_activate_batch_deferred(StatefulSession& session,
                                             std::vector<std::shared_ptr<Fact>>& facts,
                                             PropagationType p_type) {
    right_activate_batch(session, facts, p_type);
  }

  // Flush pending facts queued by deferred activation.
  // Returns true if any work was done.
  virtual bool flush_pending(StatefulSession& session) { return false; }
  void add_child(std::shared_ptr<ReteNode> const& child);
  void add_parent(std::shared_ptr<ReteNode> const& parent);
  virtual void print_node(std::ostream& os) const = 0;

  std::vector<std::weak_ptr<ReteNode>> const& get_children() const
  {
    return children;
  }

  std::vector<std::weak_ptr<ReteNode>> const& get_parents() const
  {
    return parents;
  }

  int id = -1;
  int mem_slot = -1;

  uintptr_t get_id() const { return reinterpret_cast<uintptr_t>(this); }

protected:
  explicit ReteNode(NodeKind k) : kind(k) {}
  std::vector<std::weak_ptr<ReteNode>> children;
  std::vector<std::weak_ptr<ReteNode>> parents;
};

class BetaConditionNode : public ReteNode
{
public:
  BetaConditionNode(NodeKind k) : ReteNode(k) {}
  BetaConditionNode(NodeKind k, std::vector<ParsedConstraint> const& joins,
                    ruleforge::map<std::string, int> const& bindings);
  void left_activate(StatefulSession& session,
                     Token const& token) override;
  void right_activate(StatefulSession& session,
                      std::shared_ptr<Fact> fact,
                      PropagationType p_type) override;
  void right_activate_batch(StatefulSession& session,
                            std::vector<std::shared_ptr<Fact>>& facts,
                            PropagationType p_type) override;
  void right_activate_deferred(StatefulSession& session,
                               std::shared_ptr<Fact> fact,
                               PropagationType p_type) override;
  void right_activate_batch_deferred(StatefulSession& session,
                                     std::vector<std::shared_ptr<Fact>>& facts,
                                     PropagationType p_type) override;
  bool flush_pending(StatefulSession& session) override;

protected:
  virtual bool condition_passes(size_t match_count) const = 0;
  virtual bool was_passing(size_t old_match_count) const = 0;

  // Immutable config (set at build time)
  std::vector<ParsedConstraint> join_constraints;
  ruleforge::map<std::string, int> binding_to_token_idx;
};

// --- NODE SUBCLASSES ---

class AlphaNode : public ReteNode
{
public:
  AlphaNode() : ReteNode(NodeKind::Alpha) {}
  explicit AlphaNode(ParsedConstraint const& constraint);
  void left_activate(StatefulSession&, Token const&) override;
  void right_activate(StatefulSession&,
                      std::shared_ptr<Fact>,
                      PropagationType) override;
  void right_activate_batch(StatefulSession&,
                            std::vector<std::shared_ptr<Fact>>&,
                            PropagationType) override;
  void right_activate_deferred(StatefulSession&,
                               std::shared_ptr<Fact>,
                               PropagationType) override;
  void right_activate_batch_deferred(StatefulSession&,
                                     std::vector<std::shared_ptr<Fact>>&,
                                     PropagationType) override;
  void print_node(std::ostream& os) const override;
  friend class BetaNetworkBuilder;
  friend struct CompiledNetwork;

private:
  ParsedConstraint constraint;
  bool check_constraint(Fact const& fact) const;
};

class EntryPointNode : public ReteNode
{
public:
  EntryPointNode() : ReteNode(NodeKind::EntryPoint) {}
  void left_activate(StatefulSession&, Token const&) override;
  void right_activate(StatefulSession&,
                      std::shared_ptr<Fact>,
                      PropagationType) override;
  void right_activate_batch(StatefulSession&,
                            std::vector<std::shared_ptr<Fact>>&,
                            PropagationType) override;
  void right_activate_deferred(StatefulSession&,
                               std::shared_ptr<Fact>,
                               PropagationType) override;
  void right_activate_batch_deferred(StatefulSession&,
                                     std::vector<std::shared_ptr<Fact>>&,
                                     PropagationType) override;
  void print_node(std::ostream& os) const override;

  friend class ReteSerializer;
};

// =========================================================================
// === JOIN NODE FAMILY ====================================================
// =========================================================================

class BaseJoinNode : public ReteNode
{
public:
  BaseJoinNode(NodeKind k, std::vector<ParsedConstraint> joins,
               ruleforge::map<std::string, int> bindings);
  void left_activate(StatefulSession& session,
                     Token const& token) override = 0;
  void right_activate(StatefulSession& session,
                      std::shared_ptr<Fact> fact,
                      PropagationType p_type) override = 0;

protected:
  using ChildMap = ruleforge::unordered_map<TokenWME const*,
      std::vector<std::shared_ptr<TokenWME const>>>;
  using RightChildMap = ruleforge::unordered_map<int64_t,
      std::vector<std::shared_ptr<TokenWME const>>>;

  void propagate_assert(StatefulSession& session,
                        Token const& token,
                        std::shared_ptr<Fact> fact,
                        ChildMap& left_to_children,
                        RightChildMap& right_to_children);
  void propagate_retract(StatefulSession& session,
                         std::shared_ptr<TokenWME const> wme,
                         std::shared_ptr<Fact> fact,
                         ChildMap& left_to_children,
                         RightChildMap& right_to_children);

  // Immutable config
  std::vector<ParsedConstraint> join_constraints_;
  ruleforge::map<std::string, int> binding_to_token_idx_;
};

class HashedJoinNode : public BaseJoinNode
{
public:
  using HashedTokenMemory =
      ruleforge::unordered_map<ConstraintValue,
                         std::vector<std::shared_ptr<TokenWME const>>,
                         ConstraintValueHasher>;
  using HashedFactMemory =
      ruleforge::unordered_map<ConstraintValue,
                         std::vector<std::shared_ptr<Fact>>,
                         ConstraintValueHasher>;

  HashedJoinNode(std::vector<ParsedConstraint> joins,
                 ruleforge::map<std::string, int> bindings,
                 std::pair<std::string, int> left_hash_key,
                 std::string right_hash_key);
  void left_activate(StatefulSession& session,
                     Token const& token) override;
  void right_activate(StatefulSession& session,
                      std::shared_ptr<Fact> fact,
                      PropagationType p_type) override;
  void right_activate_batch(StatefulSession& session,
                            std::vector<std::shared_ptr<Fact>>& facts,
                            PropagationType p_type) override;
  void right_activate_deferred(StatefulSession& session,
                               std::shared_ptr<Fact> fact,
                               PropagationType p_type) override;
  void right_activate_batch_deferred(StatefulSession& session,
                                     std::vector<std::shared_ptr<Fact>>& facts,
                                     PropagationType p_type) override;
  bool flush_pending(StatefulSession& session) override;
  void print_node(std::ostream& os) const override;

  friend class ReteSerializer;

private:
  std::optional<ConstraintValue> get_key(
      Token const& token) const;
  std::optional<ConstraintValue> get_key(
      std::shared_ptr<Fact> const& fact) const;
  // Immutable config
  std::pair<std::string, int> left_hash_key_;
  std::string right_hash_key_;
};

class CrossProductJoinNode : public BaseJoinNode
{
public:
  using TokenMemory =
      ruleforge::unordered_map<TokenWME const*, std::shared_ptr<TokenWME const>>;
  using FactMemory = ruleforge::unordered_map<int64_t, std::shared_ptr<Fact>>;

  CrossProductJoinNode(std::vector<ParsedConstraint> joins,
                       ruleforge::map<std::string, int> bindings);
  void left_activate(StatefulSession& session,
                     Token const& token) override;
  void right_activate(StatefulSession& session,
                      std::shared_ptr<Fact> fact,
                      PropagationType p_type) override;
  void right_activate_batch(StatefulSession& session,
                            std::vector<std::shared_ptr<Fact>>& facts,
                            PropagationType p_type) override;
  void right_activate_deferred(StatefulSession& session,
                               std::shared_ptr<Fact> fact,
                               PropagationType p_type) override;
  void right_activate_batch_deferred(StatefulSession& session,
                                     std::vector<std::shared_ptr<Fact>>& facts,
                                     PropagationType p_type) override;
  bool flush_pending(StatefulSession& session) override;
  void print_node(std::ostream& os) const override;
  friend class ReteSerializer;
};

class NotNode : public BetaConditionNode
{
public:
  NotNode() : BetaConditionNode(NodeKind::Not, {}, {}) {}
  NotNode(std::vector<ParsedConstraint> const& joins,
          ruleforge::map<std::string, int> const& bindings);
  void print_node(std::ostream& os) const override;

protected:
  bool condition_passes(size_t match_count) const override
  {
    return match_count == 0;
  }

  bool was_passing(size_t old_match_count) const override
  {
    return old_match_count == 0;
  }
};

class ExistsNode : public BetaConditionNode
{
public:
  ExistsNode() : BetaConditionNode(NodeKind::Exists, {}, {}) {}
  ExistsNode(std::vector<ParsedConstraint> const& joins,
             ruleforge::map<std::string, int> const& bindings);
  void print_node(std::ostream& os) const override;

protected:
  bool condition_passes(size_t match_count) const override
  {
    return match_count > 0;
  }

  bool was_passing(size_t old_match_count) const override
  {
    return old_match_count > 0;
  }
};

class AccumulateNode : public ReteNode
{
public:
  AccumulateNode() : ReteNode(NodeKind::Accumulate) {}
  AccumulateNode(IAccumulator const* prototype,
                 ParsedAccumulate&&,
                 std::string res_fact_type,
                 ruleforge::map<std::string, int> bindings,
                 std::vector<ParsedConstraint> joins);
  void left_activate(StatefulSession&, Token const&) override;
  void right_activate(StatefulSession&,
                      std::shared_ptr<Fact>,
                      PropagationType) override;
  void right_activate_batch(StatefulSession&,
                            std::vector<std::shared_ptr<Fact>>&,
                            PropagationType) override;
  void right_activate_deferred(StatefulSession&,
                               std::shared_ptr<Fact>,
                               PropagationType) override;
  void right_activate_batch_deferred(StatefulSession&,
                                     std::vector<std::shared_ptr<Fact>>&,
                                     PropagationType) override;
  bool flush_pending(StatefulSession&) override;
  void print_node(std::ostream& os) const override;
  friend class KnowledgeBase;
  friend class ReteSerializer;

private:
  void update_and_propagate_result(StatefulSession& session,
                                   NetworkMemory::AccumulateMem::LeftMemoryItem& item);

  // Immutable config
  IAccumulator const* accumulator_prototype = nullptr;
  ParsedAccumulate info;
  std::string result_fact_type;
  ruleforge::map<std::string, int> binding_to_token_idx;
  std::vector<ParsedConstraint> join_constraints;
};

class UnnestNode : public ReteNode
{
public:
  UnnestNode() : ReteNode(NodeKind::Unnest) {}
  UnnestNode(ParsedUnnest const&, ruleforge::map<std::string, int> const&);
  void left_activate(StatefulSession&, Token const&) override;

  void right_activate(StatefulSession&,
                      std::shared_ptr<Fact>,
                      PropagationType) override
  {
  }

  void print_node(std::ostream& os) const override;
  friend class ReteSerializer;

private:
  // Immutable config
  ParsedUnnest info;
  ruleforge::map<std::string, int> binding_to_token_idx;
};

class EvalNode : public ReteNode
{
public:
  EvalNode() : ReteNode(NodeKind::Eval) {}
  EvalNode(std::string expression, ruleforge::map<std::string, int> bindings);
  void left_activate(StatefulSession& session,
                     Token const& token) override;

  void right_activate(StatefulSession&,
                      std::shared_ptr<Fact>,
                      PropagationType) override
  {
  }

  void print_node(std::ostream& os) const override;
  friend class ReteSerializer;

private:
  // Immutable config
  std::string expression;
  ruleforge::map<std::string, int> binding_to_token_idx;
};

class TerminalNode : public ReteNode
{
public:
  TerminalNode() : ReteNode(NodeKind::Terminal) {}
  TerminalNode(ParsedRule const& rule, ruleforge::map<std::string, int> bindings);
  void left_activate(StatefulSession&, Token const&) override;

  void right_activate(StatefulSession&,
                      std::shared_ptr<Fact>,
                      PropagationType) override
  {
  }

  void print_node(std::ostream& os) const override;
  friend class ReteSerializer;

private:
  // Immutable config
  std::string rule_name;
  ruleforge::map<std::string, int> binding_to_token_idx;
};

class QueryTerminalNode : public ReteNode
{
public:
  QueryTerminalNode() : ReteNode(NodeKind::QueryTerminal) {}
  explicit QueryTerminalNode(ruleforge::map<std::string, int> bindings);
  void left_activate(StatefulSession&, Token const&) override;

  void right_activate(StatefulSession&,
                      std::shared_ptr<Fact>,
                      PropagationType) override
  {
  }

  ruleforge::map<TokenWME const*, Token> const& get_results(StatefulSession& session);
  void clear_results(StatefulSession& session);
  void set_bindings(ruleforge::map<std::string, int> const& bindings);

  ruleforge::map<std::string, int> const& get_bindings() const
  {
    return binding_to_token_idx;
  }

  void print_node(std::ostream& os) const override;
  friend class ReteSerializer;

private:
  // Immutable config
  ruleforge::map<std::string, int> binding_to_token_idx;
};

class QueryInputNode : public ReteNode
{
public:
  QueryInputNode() : ReteNode(NodeKind::QueryInput) {}
  explicit QueryInputNode(std::shared_ptr<QueryTerminalNode> terminal_node);

  void left_activate(StatefulSession&, Token const&) override {}

  void right_activate(StatefulSession&,
                      std::shared_ptr<Fact>,
                      PropagationType) override
  {
  }

  void execute(StatefulSession& session,
               std::vector<std::shared_ptr<Fact>> const& args);
  void print_node(std::ostream& os) const override;
  friend class KnowledgeBase;
  friend class ReteSerializer;

private:
  std::weak_ptr<QueryTerminalNode> terminal_node;
};

#endif  // RETE_NODE_HPP
