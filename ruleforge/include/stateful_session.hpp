#ifndef STATEFUL_SESSION_HPP
#define STATEFUL_SESSION_HPP
#include <queue>

#include <quickjs.h>

#include "drools_rete_defs.hpp"
#include "i_engine_listener.hpp"
#include "i_network_callback.hpp"
#include "knowledge_base.hpp"
#include "phmap.h"
#include "query_result.hpp"
#include "rule_execution_tracer.hpp"

// Forward declarations
class JSScriptingManager;
class TruthMaintenanceSystem;
class ReteNode;
class QueryTerminalNode;
class QueryInputNode;
class BetaNetworkBuilder;

class StatefulSession : public INetworkCallback
{
public:
  friend class BetaNetworkBuilder;
  friend class KnowledgeBase;
  friend class TerminalNode;

  struct private_key
  {
    explicit private_key() = default;
  };

  explicit StatefulSession(private_key,
                           std::shared_ptr<KnowledgeBase const> kb);
  // Not copyable or assignable
  StatefulSession(StatefulSession const&) = delete;
  StatefulSession& operator=(StatefulSession const&) = delete;

  ~StatefulSession();

  QueryResult execute_query(
      std::string const& query_name,
      std::vector<std::shared_ptr<Fact>> const& args = {});

  // --- User-facing Runtime API ---
  void add_fact(std::shared_ptr<Fact> fact) override;
  void add_facts(std::vector<std::shared_ptr<Fact>> const& facts);
  int fire_all_rules();
  void retract_fact(std::shared_ptr<Fact> fact) override;
  void retract_facts(std::vector<std::shared_ptr<Fact>> const& facts);
  void update_fact(std::shared_ptr<Fact> fact,
                   std::function<void(Fact&)> modifier) override;

  template<typename T>
  void add_fact_typed(T const& typed_fact)
  {
    std::shared_ptr<Fact> fact =
        kb_->get_fact_type_registry().convert(typed_fact);
    if (fact) {
      add_fact(fact);
    }
  }

  void set_focus(std::string const& group_name) override;
  std::string get_focus() const;
  void set_global(std::string const& name, JSValue obj);
  size_t get_fact_count() const;
  int64_t get_next_fact_id();
  JSContext* get_js_context();

  // Rule execution tracing
  RuleExecutionTracer& get_tracer() { return tracer_; }
  void enable_tracing(bool enabled = true) { tracer_.enable_tracing(enabled); }
  std::string get_execution_trace(bool include_network = false) const { 
      return tracer_.format_trace(include_network); 
  }
  std::string get_rule_performance_summary() const { 
      return tracer_.format_rule_summary(); 
  }

  std::shared_ptr<KnowledgeBase const> get_knowledge_base() const
  {
    return kb_;
  }

  void addListener(std::shared_ptr<IEngineListener> listener);
  void removeListener(std::shared_ptr<IEngineListener> const& listener);

  std::vector<std::shared_ptr<IEngineListener>>& get_listeners()
  {
    return listeners_;
  }

  // --- Internal & INetworkCallback API ---
  void _internal_add_fact(std::shared_ptr<Fact> fact);
  void _internal_add_facts_batch(std::vector<std::shared_ptr<Fact>> const& facts);
  void _internal_remove_fact(int64_t fact_id);
  std::optional<std::shared_ptr<Fact>> get_fact_by_id(int64_t id) override;
  void logical_insert(Token& token, std::shared_ptr<Fact> fact) override;
  map<std::string, JSValue> const& get_global_values() const override;

  inline std::shared_ptr<TokenWME const> get_or_create_wme(
      std::shared_ptr<TokenWME const> parent_wme, std::shared_ptr<Fact> fact)
  {
    if (!parent_wme) {
      parent_wme = dummy_wme_;
    }
    size_t fact_hash_key = (fact->id != 0)
        ? static_cast<size_t>(fact->id)
        : reinterpret_cast<size_t>(fact.get());
    size_t parent_hash = parent_wme ? parent_wme->hash : 0;
    size_t hash = parent_hash
        ^ (fact_hash_key + 0x9e3779b9 + (parent_hash << 6)
           + (parent_hash >> 2));
    auto new_wme = std::make_shared<TokenWME>(
        TokenWME {parent_wme, fact, parent_wme->depth + 1, hash});
    auto it = wme_cache_.find(new_wme);
    if (it != wme_cache_.end()) {
      return *it;
    }
    wme_cache_.insert(new_wme);
    return new_wme;
  }

  inline std::shared_ptr<TokenWME const> get_dummy_wme() { return dummy_wme_; }

  void add_activation(Activation const& activation);
  void remove_activation(size_t activation_hash);
  void logical_retract(TokenWME const* wme);

  bool execute_eval(std::string const& code,
                    Token const& token,
                    map<std::string, int> const& bindings);

private:
  void build_network();
  void prime_network_state();
  void assign_nested_fact_ids(Fact& fact);

  template<typename T, typename... Args>
  std::shared_ptr<T> create_node(Args&&... args)
  {
    auto node = std::make_shared<T>(std::forward<Args>(args)...);
    node->id = next_node_id_++;
    all_nodes_.push_back(node);
    return node;
  }

  std::vector<std::shared_ptr<ReteNode>> build_alpha_chain(
      ConstraintNode const* node,
      std::vector<std::shared_ptr<ReteNode>> parent_tails);

  using TokenWMESet = unordered_set<std::shared_ptr<TokenWME const>,
                                         TokenWMEPtrHasher,
                                         TokenWMEPtrEquals>;

  // --- Immutable Reference ---
  std::shared_ptr<KnowledgeBase const> kb_;

  // --- Mutable State ---
  std::unique_ptr<JSScriptingManager> scripting_manager_;
  std::unique_ptr<TruthMaintenanceSystem> tms_;
  RuleExecutionTracer tracer_;

  // Rete network instance state
  mutable int64_t next_fact_id_ = 1;
  int next_node_id_ = 0;
  std::vector<std::shared_ptr<ReteNode>> all_nodes_;
  map<std::string, std::shared_ptr<ReteNode>> alpha_entry_points_;
  map<std::string, std::shared_ptr<QueryTerminalNode>> query_nodes_;
  map<std::string, std::shared_ptr<QueryInputNode>>
      parameterized_query_inputs_;

  // Working memory state
  std::shared_ptr<TokenWME const> dummy_wme_;
  TokenWMESet wme_cache_;
  map<int64_t, std::shared_ptr<Fact>> all_facts_;
  std::vector<std::string> agenda_group_focus_stack_;
  map<size_t, Activation> agenda_map_;
  std::priority_queue<std::pair<int, size_t>> agenda_queue_;
  std::vector<std::shared_ptr<IEngineListener>> listeners_;
  map<int, std::shared_ptr<ReteNode>> get_nodes() const;
  friend class ReteSerializer;
};

#endif  // STATEFUL_SESSION_HPP
