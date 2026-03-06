#ifndef RETE_NODE_QUERY_HPP
#define RETE_NODE_QUERY_HPP

#include "rete/rete_node_base.hpp"

class TerminalNode : public ReteNode
{
public:
  static constexpr NodeKind Kind = NodeKind::Terminal;
  TerminalNode() : ReteNode(NodeKind::Terminal) {}
  TerminalNode(ParsedRule const& rule, std::map<std::string, int> bindings);
  void left_activate(StatefulSession&, Token const&) override;
  void left_activate_batch(StatefulSession&, std::vector<Token>&) override;

  void right_activate(StatefulSession&,
                      Fact*,
                      PropagationType) override
  {
  }

  void print_node(std::ostream& os) const override;
  friend class ReteSerializer;

private:
  // Immutable config
  std::string rule_name;
  ParsedRule const* cached_rule_ = nullptr;  // Cached pointer — avoids find_rule_by_name per activation
  std::map<std::string, int> binding_to_token_idx;
};

class QueryTerminalNode : public ReteNode
{
public:
  static constexpr NodeKind Kind = NodeKind::QueryTerminal;
  QueryTerminalNode() : ReteNode(NodeKind::QueryTerminal) {}
  explicit QueryTerminalNode(std::map<std::string, int> bindings);
  void left_activate(StatefulSession&, Token const&) override;

  void right_activate(StatefulSession&,
                      Fact*,
                      PropagationType) override
  {
  }

  std::map<TokenWME const*, Token> const& get_results(StatefulSession& session);
  void clear_results(StatefulSession& session);
  void set_bindings(std::map<std::string, int> const& bindings);

  std::map<std::string, int> const& get_bindings() const
  {
    return binding_to_token_idx;
  }

  void print_node(std::ostream& os) const override;
  friend class ReteSerializer;

private:
  // Immutable config
  std::map<std::string, int> binding_to_token_idx;
};

class QueryInputNode : public ReteNode
{
public:
  static constexpr NodeKind Kind = NodeKind::QueryInput;
  QueryInputNode() : ReteNode(NodeKind::QueryInput) {}
  explicit QueryInputNode(std::shared_ptr<QueryTerminalNode> terminal_node);

  void left_activate(StatefulSession&, Token const&) override {}

  void right_activate(StatefulSession&,
                      Fact*,
                      PropagationType) override
  {
  }

  void execute(StatefulSession& session,
               std::vector<Fact*> const& args);
  void print_node(std::ostream& os) const override;
  friend class KnowledgeBase;
  friend class ReteSerializer;

private:
  std::weak_ptr<QueryTerminalNode> terminal_node;
};

namespace rulesforge::rete_prof {
struct Stats {
  uint64_t alpha_checks = 0;
  uint64_t join_checks = 0;
  uint64_t compare_calls = 0;
  uint64_t compiled_expr_evals = 0;
  uint64_t field_lookups = 0;
};

void reset_stats();
Stats get_stats();
}  // namespace rulesforge::rete_prof

#endif  // RETE_NODE_QUERY_HPP
