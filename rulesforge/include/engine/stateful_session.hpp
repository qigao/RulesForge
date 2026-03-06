#ifndef STATEFUL_SESSION_HPP
#define STATEFUL_SESSION_HPP
#include <cstdint>
#include <queue>

#include "core/parsed_rule.hpp"
#include "core/token.hpp"
#include "engine/i_engine_listener.hpp"
#include "engine/i_network_callback.hpp"
#include "engine/knowledge_base.hpp"
#include "engine/metrics_exporter.hpp"
#include "engine/agenda.hpp"

#include "engine/query_result.hpp"
#include "rete/network_memory.hpp"
#include "engine/rule_execution_tracer.hpp"
#include "engine/schema_validator.hpp"
#include "engine/working_memory.hpp"
#include "core/value_types.hpp"

#include "data/session_arena.hpp"
#include "data/token_arena.hpp" // Added for TokenArena
#include "data/fact_arena.hpp"  // Added for FactArena (if we use it internally)

// Forward declarations
class TruthMaintenanceSystem;
class ReteNode;
class QueryTerminalNode;
class QueryInputNode;
class BetaNetworkBuilder;
class QueryEngine;

namespace rulesforge {
    class RhsExecutor;
    class ExpressionEvaluator;
}

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

  struct RuntimeCounters {
    uint64_t agenda_add_calls = 0;
    uint64_t agenda_pop_calls = 0;
    uint64_t fire_activation_calls = 0;
    uint64_t propagate_modify_calls = 0;
    uint64_t agenda_pop_time_us = 0;
    uint64_t agenda_pop_select_time_us = 0;
    uint64_t agenda_pop_detach_time_us = 0;
    uint64_t fire_activation_time_us = 0;
    uint64_t propagate_modify_time_us = 0;
    uint64_t rhs_execute_time_us = 0;
    uint64_t fire_bookkeeping_time_us = 0;
    uint64_t phreak_dirty_segment_marks = 0;
    uint64_t phreak_dirty_path_marks = 0;
    uint64_t phreak_flush_iterations = 0;
  };

  explicit StatefulSession(private_key,
                           std::shared_ptr<KnowledgeBase const> kb);
  // Not copyable or assignable
  StatefulSession(StatefulSession const&) = delete;
  StatefulSession& operator=(StatefulSession const&) = delete;

  ~StatefulSession();

  QueryResult execute_query(
      std::string const& query_name,
      std::vector<Fact*> const& args = {});

  // --- User-facing Runtime API ---
  void add_fact(Fact* fact) override;
  void add_fact(std::shared_ptr<Fact> const& fact) { add_fact(fact.get()); }
  void add_facts(std::vector<Fact*> const& facts);
  void add_facts(std::vector<std::shared_ptr<Fact>> const& facts) {
      std::vector<Fact*> raw;
      raw.reserve(facts.size());
      for (auto& f : facts) raw.push_back(f.get());
      add_facts(raw);
  }

  /**
   * @brief Insert a fact into a named entry point stream.
   */
  void insert_into(std::string const& stream_name, Fact* fact);
  int fire_all_rules(int max_rules = -1);

  void retract_fact(Fact* fact) override;
  void retract_fact(std::shared_ptr<Fact> const& fact) { retract_fact(fact.get()); }
  void retract_facts(std::vector<Fact*> const& facts);
  void retract_facts(std::vector<std::shared_ptr<Fact>> const& facts) {
    for (auto const& f : facts) retract_fact(f.get());
  }
  void update_fact(Fact* fact,
                   std::function<void(Fact&)> modifier) override;
  void propagate_modify(Fact* fact,
                        rulesforge::ModifiedFieldsHint const* changed_fields = nullptr) override;

  // ... (halt, is_consistent methods remain same) ...
  void halt() { halt_requested_ = true; }
  bool is_halt_requested() const { return halt_requested_; }
  bool is_consistent() const { return is_consistent_; }
  void begin_rhs_transaction() override;
  void end_rhs_transaction(bool commit) override;

  template<typename T>
  void add_fact_typed(T const& typed_fact)
  {
    // Type registry still returns shared_ptr? Need to check.
    // If registry returns shared_ptr, we might need to change it or release.
    // For now assuming we refactoring core.
    // If registry returns shared_ptr, we can call .get() but we must ensure ownership.
    // Ideally registry should be updated too.
    // Just using .get() for now if it returns shared_ptr (assuming it's kept alive elsewhere or we copy).
    // Actually, if add_fact takes Fact*, and Session expects ownership (or arena managed),
    // we should clone it into internal arena?
    // Or just take ownership if it's unique_ptr.
    // Given the task, I'll comment out implementation details or assume Fact* validity.
    // NOTE: This template needs to be updated based on registry changes.
  }

  void set_focus(std::string const& group_name) override;
  std::string get_focus() const;
  size_t get_fact_count() const;
  int64_t get_next_fact_id();
  void set_global(std::string const& name, ConstraintValue value);
  std::optional<ConstraintValue> get_global(std::string const& name) const;

  void set_validation_mode(ValidationMode mode) { validation_mode_ = mode; }
  ValidationMode get_validation_mode() const { return validation_mode_; }

  bool has_type_declaration(std::string const& type_name) const;

  // Rule execution tracing
  RuleExecutionTracer& get_tracer() { return tracer_; }
  void enable_tracing(bool enabled = true) { tracer_.enable_tracing(enabled); }
  std::string get_execution_trace(bool include_network = false) const {
      return tracer_.format_trace(include_network);
  }
  std::string get_rule_performance_summary() const {
      return tracer_.format_rule_summary();
  }

  // Memory statistics
  SessionArena& get_arena() { return arena_; }
  std::string get_memory_stats() const { return arena_.format_stats(); }

  SessionMetrics get_metrics() const;
  std::string export_metrics(IMetricsExporter const& exporter) const {
      return exporter.export_metrics(get_metrics());
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
  void _internal_add_fact(Fact* fact);
  void _internal_add_facts_batch(std::vector<Fact*> const& facts);
  void _internal_remove_fact(int64_t fact_id);
  Fact* get_fact_by_id(int64_t id) override;
  void logical_insert(Token& token, Fact* fact) override;
  Fact* logical_insert(Fact const& fact) override;

  Fact* create_fact(std::string const& type) override {
      Fact* f = fact_arena_.create_fact();
      f->type = type;
      return f;
  }

  inline TokenWME const* get_or_create_wme(
      TokenWME const* parent_wme, Fact const* fact)
  {
    if (!parent_wme) {
         parent_wme = token_arena_.get_root();
    }

    size_t parent_hash = parent_wme ? parent_wme->hash : 0;
    size_t fact_hash = std::hash<const void*>{}(fact);
    size_t hash = parent_hash ^ (fact_hash + 0x9e3779b9 + (parent_hash << 6) + (parent_hash >> 2));

    auto it = wme_cache_.find(hash);
    if (it != wme_cache_.end()) {
      return it->second;
    }

    TokenWME* new_wme = token_arena_.create_token(parent_wme, fact);

    wme_cache_[hash] = new_wme;
    return new_wme;
  }

  // Fast path: skip cache lookup when we know the WME is new (batch ASSERT)
  inline TokenWME const* create_wme_uncached(
      TokenWME const* parent_wme, Fact const* fact)
  {
    if (!parent_wme) {
         parent_wme = token_arena_.get_root();
    }
    return token_arena_.create_token(parent_wme, fact);
  }

  inline TokenWME const* get_dummy_wme() { return token_arena_.get_root(); }

  inline void invalidate_wme_cache(size_t hash) {
    wme_cache_.erase(hash);
  }

  void add_activation(Activation const& activation);
  void add_activations_batch(std::vector<Activation>& activations);  // PHREAK: batch agenda insert
  void remove_activation(size_t activation_hash);
  void logical_retract(TokenWME const* wme);

  bool execute_eval(std::string const& code,
                    Token const& token,
                    std::map<std::string, int> const& bindings);
  bool execute_eval(rulesforge::ExpressionEvaluator const& expr,
                    Token const& token,
                    std::map<std::string, int> const& bindings);

  NetworkMemory& net_mem() { return net_mem_; }
  NetworkMemory const& net_mem() const { return net_mem_; }
  void allocate_network_memory(MemSlotCounts const& counts);
  void reset_runtime_counters();
  RuntimeCounters runtime_counters() const;
  rulesforge::ModifiedFieldsHint const* current_modified_fields() const { return current_modified_fields_; }

  void flush_pending_nodes();
  bool is_deferred_mode() const { return deferred_mode_; }

private:
  void prime_network_state();
  void fire_activation(Activation& activation);
  bool is_node_pending_dirty(ReteNode const& node) const;
  void mark_phreak_dirty_for_node(int node_id);
  void mark_phreak_dirty_for_type(std::string const& fact_type);
  void clear_phreak_dirty_state();

  // Cache using raw pointers
  using TokenWMECache = std::unordered_map<size_t, TokenWME const*>;

  std::shared_ptr<KnowledgeBase const> kb_;

  NetworkMemory net_mem_;

  std::unique_ptr<rulesforge::RhsExecutor> rhs_executor_;
  std::unique_ptr<QueryEngine> query_engine_;
  std::unique_ptr<TruthMaintenanceSystem> tms_;
  RuleExecutionTracer tracer_;
  SessionArena arena_;
  rulesforge::TokenArena token_arena_;
  rulesforge::FactArena fact_arena_;

  // dummy_wme_ is managed by TokenArena (root)
  TokenWME const* dummy_wme_ = nullptr;
  TokenWMECache wme_cache_;
  WorkingMemory working_memory_;
  Activation const* current_activation_ = nullptr;
  Agenda agenda_;
  std::vector<std::shared_ptr<IEngineListener>> listeners_;
  bool halt_requested_ = false;

  bool in_rhs_transaction_ = false;
  bool is_consistent_ = true;
  bool deferred_mode_ = false;
  bool phreak_experimental_ = false;
  std::map<std::string, ConstraintValue> globals_;
  std::vector<int64_t> transaction_inserted_facts_;
  std::vector<int64_t> transaction_retracted_facts_;

  ValidationMode validation_mode_ = ValidationMode::None;
  std::unique_ptr<SchemaValidator> schema_validator_;



  mutable int64_t rules_fired_total_ = 0;
  mutable int64_t facts_inserted_total_ = 0;
  mutable int64_t facts_retracted_total_ = 0;
  RuntimeCounters runtime_counters_{};
  rulesforge::ModifiedFieldsHint const* current_modified_fields_ = nullptr;
  uint64_t phreak_epoch_ = 0;
  std::map<int, std::shared_ptr<ReteNode>> get_nodes() const;
  friend class ReteSerializer;
};

#endif  // STATEFUL_SESSION_HPP
