#ifndef RETE_NODE_TRANSFORM_HPP
#define RETE_NODE_TRANSFORM_HPP

#include "rete/rete_node_base.hpp"

class AccumulateNode : public ReteNode
{
public:
  static constexpr NodeKind Kind = NodeKind::Accumulate;
  AccumulateNode() : ReteNode(NodeKind::Accumulate) {}
  AccumulateNode(IAccumulator const* prototype,
                 ParsedAccumulate&&,
                 std::string res_fact_type,
                 std::map<std::string, int> bindings,
                 std::vector<ParsedConstraint> joins);
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
  bool flush_pending(StatefulSession&) override;
  void print_node(std::ostream& os) const override;
  friend class KnowledgeBase;
  friend class ReteSerializer;

private:
  void update_and_propagate_result(
      StatefulSession& session,
      NetworkMemory::AccumulateMem::LeftMemoryItem& item);

  // Immutable config
  IAccumulator const* accumulator_prototype = nullptr;
  ParsedAccumulate info;
  std::string result_fact_type;
  std::map<std::string, int> binding_to_token_idx;
  std::vector<ParsedConstraint> join_constraints;
};

class UnnestNode : public ReteNode
{
public:
  static constexpr NodeKind Kind = NodeKind::Unnest;
  UnnestNode() : ReteNode(NodeKind::Unnest) {}
  UnnestNode(ParsedUnnest const&, std::map<std::string, int> const&);
  void left_activate(StatefulSession&, Token const&) override;

  void right_activate(StatefulSession&,
                      Fact*,
                      PropagationType) override
  {
  }

  void print_node(std::ostream& os) const override;
  friend class ReteSerializer;

private:
  // Immutable config
  ParsedUnnest info;
  std::map<std::string, int> binding_to_token_idx;
};


class EvalNode : public ReteNode
{
public:
  static constexpr NodeKind Kind = NodeKind::Eval;
  EvalNode() : ReteNode(NodeKind::Eval) {}
  EvalNode(std::string expression,
           std::map<std::string, int> bindings,
           std::map<std::string, std::string> scalar_binding_fields);
  void left_activate(StatefulSession& session,
                     Token const& token) override;

  void right_activate(StatefulSession&,
                      Fact*,
                      PropagationType) override
  {
  }

  void print_node(std::ostream& os) const override;
  friend class ReteSerializer;

private:
  struct EvalResolvedVar {
    int token_depth = -1;
    std::string field_name;
    bool use_fact_id = false;
  };

  // Immutable config
  std::string expression;
  std::map<std::string, int> binding_to_token_idx;
  std::map<std::string, std::string> scalar_binding_to_field;
  std::shared_ptr<rulesforge::ExpressionEvaluator> compiled_expression;
  std::unordered_map<std::string, EvalResolvedVar> resolved_vars_;
};

class WindowNode : public ReteNode
{
public:
  static constexpr NodeKind Kind = NodeKind::Window;
  WindowNode() : ReteNode(NodeKind::Window) {}
  WindowNode(ParsedWindow const& window_info);

  void left_activate(StatefulSession&, Token const&) override {}
  
  void right_activate(StatefulSession& session,
                      Fact* fact,
                      PropagationType p_type) override;

  void print_node(std::ostream& os) const override;
  friend class ReteSerializer;

private:
  void evaluate_expiration(StatefulSession& session);

  // Immutable config
  ParsedWindow info;
};

#endif  // RETE_NODE_TRANSFORM_HPP
