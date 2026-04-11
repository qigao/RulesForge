#ifndef RETE_NODE_BASE_HPP
#define RETE_NODE_BASE_HPP

#include <cstdint>
#include <iosfwd>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/parsed_rule.hpp"
#include "core/token.hpp"
#include "engine/rfl_accumulators.hpp"
#include "rete/network_memory.hpp"

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
  Window,
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

  // PHREAK: Batch left-activation for set-oriented propagation.
  virtual void left_activate_batch(StatefulSession& session,
                                   std::vector<Token>& tokens) {
    for (auto& token : tokens) {
      left_activate(session, token);
    }
  }

  virtual void right_activate(StatefulSession& session,
                              Fact* fact,
                              PropagationType p_type) = 0;

  // Batch right-activation: propagate a vector of facts in one pass.
  virtual void right_activate_batch(StatefulSession& session,
                                    std::vector<Fact*>& facts,
                                    PropagationType p_type) {
    for (auto* fact : facts) {
      right_activate(session, fact, p_type);
    }
  }

  // Deferred evaluation: queue facts for later processing.
  virtual void right_activate_deferred(StatefulSession& session,
                                       Fact* fact,
                                       PropagationType p_type) {
    right_activate(session, fact, p_type);
  }

  // Batch deferred: queue a vector of facts for later processing.
  virtual void right_activate_batch_deferred(StatefulSession& session,
                                             std::vector<Fact*>& facts,
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
  std::vector<ReteNode*> children_raw;  // Cached raw pointers for hot-path traversal
  std::vector<std::weak_ptr<ReteNode>> parents;
};

class BetaConditionNode : public ReteNode
{
public:
  BetaConditionNode(NodeKind k) : ReteNode(k) {}
  BetaConditionNode(NodeKind k, std::vector<ParsedConstraint> const& joins,
                    std::map<std::string, int> const& bindings);
  void left_activate(StatefulSession& session,
                     Token const& token) override;
  void right_activate(StatefulSession& session,
                      Fact* fact,
                      PropagationType p_type) override;
  void right_activate_batch(StatefulSession& session,
                            std::vector<Fact*>& facts,
                            PropagationType p_type) override;
  void right_activate_deferred(StatefulSession& session,
                               Fact* fact,
                               PropagationType p_type) override;
  void right_activate_batch_deferred(StatefulSession& session,
                                     std::vector<Fact*>& facts,
                                     PropagationType p_type) override;
  bool flush_pending(StatefulSession& session) override;

protected:
  virtual bool condition_passes(size_t match_count) const = 0;
  virtual bool was_passing(size_t old_match_count) const = 0;

  // Immutable config (set at build time)
  std::vector<ParsedConstraint> join_constraints;
  std::map<std::string, int> binding_to_token_idx;
};

// =========================================================================
// === JOIN NODE FAMILY ====================================================
// =========================================================================

class BaseJoinNode : public ReteNode
{
public:
  BaseJoinNode(NodeKind k, std::vector<ParsedConstraint> joins,
               std::map<std::string, int> bindings);
  void left_activate(StatefulSession& session,
                     Token const& token) override = 0;
  void right_activate(StatefulSession& session,
                      Fact* fact,
                      PropagationType p_type) override = 0;

protected:
  using ChildMap = std::unordered_map<TokenWME const*,
      std::vector<TokenWME const*>>;
  using RightChildMap = std::unordered_map<int64_t,
      std::vector<TokenWME const*>>;

  void propagate_assert(StatefulSession& session,
                        Token const& token,
                        Fact* fact,
                        ChildMap& left_to_children,
                        RightChildMap& right_to_children);
  void propagate_retract(StatefulSession& session,
                         TokenWME const* wme,
                         Fact* fact,
                         ChildMap& left_to_children,
                         RightChildMap& right_to_children);

  // Immutable config
  std::vector<ParsedConstraint> join_constraints_;
  std::map<std::string, int> binding_to_token_idx_;
};

#endif  // RETE_NODE_BASE_HPP
