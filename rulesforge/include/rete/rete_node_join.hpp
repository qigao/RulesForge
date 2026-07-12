#ifndef RETE_NODE_JOIN_HPP
#define RETE_NODE_JOIN_HPP

#include "rete/rete_node_base.hpp"

// --- NODE SUBCLASSES ---

class AlphaNode : public ReteNode
{
public:
  static constexpr NodeKind Kind = NodeKind::Alpha;
  AlphaNode() : ReteNode(NodeKind::Alpha) {}
  explicit AlphaNode(ParsedConstraint const& constraint);
  AlphaNode(ParsedConstraint const& constraint,
            std::optional<rulesforge::RuntimePredicateRef> runtime_predicate_ref);
  void left_activate(StatefulSession&, Token const&) override;
  void right_activate(StatefulSession&,
                      Fact*,
                      PropagationType) override;
  void right_activate_batch(StatefulSession&,
                            std::vector<Fact*>&,
                            PropagationType) override;
  void right_activate_deferred(StatefulSession&,
                               Fact*,
                               PropagationType) override;
  void right_activate_batch_deferred(StatefulSession&,
                                     std::vector<Fact*>&,
                                     PropagationType) override;
  void print_node(std::ostream& os) const override;
  friend class BetaNetworkBuilder;
  friend struct CompiledNetwork;

private:
  ParsedConstraint constraint;
  std::optional<rulesforge::RuntimePredicateRef> runtime_predicate_ref_;
  bool check_constraint(StatefulSession const& session, Fact const& fact) const;
};

class EntryPointNode : public ReteNode
{
public:
  static constexpr NodeKind Kind = NodeKind::EntryPoint;
  EntryPointNode() : ReteNode(NodeKind::EntryPoint) {}
  void left_activate(StatefulSession&, Token const&) override;
  void right_activate(StatefulSession&,
                      Fact*,
                      PropagationType) override;
  void right_activate_batch(StatefulSession&,
                            std::vector<Fact*>&,
                            PropagationType) override;
  void right_activate_deferred(StatefulSession&,
                               Fact*,
                               PropagationType) override;
  void right_activate_batch_deferred(StatefulSession&,
                                     std::vector<Fact*>&,
                                     PropagationType) override;
  void print_node(std::ostream& os) const override;

  friend class ReteSerializer;
};

class HashedJoinNode : public BaseJoinNode
{
public:
  static constexpr NodeKind Kind = NodeKind::HashedJoin;
  using HashedTokenMemory =
      std::unordered_map<ConstraintValue,
                         std::vector<TokenWME const*>,
                         ConstraintValueHasher,
                         ConstraintValueEquals>;
  using HashedFactMemory =
      std::unordered_map<ConstraintValue,
                         std::vector<Fact*>,
                         ConstraintValueHasher,
                         ConstraintValueEquals>;

  HashedJoinNode(std::vector<ParsedConstraint> joins,
                 std::map<std::string, int> bindings,
                 std::map<std::string, std::string> scalar_binding_fields,
                 std::vector<std::optional<rulesforge::RuntimePredicateRef>> runtime_predicates,
                 std::pair<std::string, int> left_hash_key,
                 std::string right_hash_key);
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
  void print_node(std::ostream& os) const override;

  friend class ReteSerializer;

private:
  std::optional<ConstraintValue> get_key(
      Token const& token) const;
  std::optional<ConstraintValue> get_key(
      Fact const* fact) const;
  // Immutable config
  std::pair<std::string, int> left_hash_key_;
  std::string right_hash_key_;
};

class CrossProductJoinNode : public BaseJoinNode
{
public:
  static constexpr NodeKind Kind = NodeKind::CrossProductJoin;
  using TokenMemory =
      std::unordered_map<TokenWME const*, TokenWME const*>;
  using FactMemory = std::unordered_map<int64_t, Fact*>;

  CrossProductJoinNode(std::vector<ParsedConstraint> joins,
                       std::map<std::string, int> bindings,
                       std::map<std::string, std::string> scalar_binding_fields = {},
                       std::vector<std::optional<rulesforge::RuntimePredicateRef>> runtime_predicates = {});
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
  void print_node(std::ostream& os) const override;
  friend class ReteSerializer;
};

class NotNode : public BetaConditionNode
{
public:
  static constexpr NodeKind Kind = NodeKind::Not;
  NotNode() : BetaConditionNode(NodeKind::Not, {}, {}) {}
  NotNode(std::vector<ParsedConstraint> const& joins,
          std::map<std::string, int> const& bindings,
          std::map<std::string, std::string> scalar_binding_fields = {},
          std::vector<std::optional<rulesforge::RuntimePredicateRef>> runtime_predicates = {});
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
  static constexpr NodeKind Kind = NodeKind::Exists;
  ExistsNode() : BetaConditionNode(NodeKind::Exists, {}, {}) {}
  ExistsNode(std::vector<ParsedConstraint> const& joins,
             std::map<std::string, int> const& bindings,
             std::map<std::string, std::string> scalar_binding_fields = {},
             std::vector<std::optional<rulesforge::RuntimePredicateRef>> runtime_predicates = {});
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

#endif  // RETE_NODE_JOIN_HPP
