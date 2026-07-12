#ifndef STATEFUL_SESSION_HPP
#define STATEFUL_SESSION_HPP
#include <cstdint>
#include <queue>
#include <unordered_map>

#include "core/parsed_rule.hpp"
#include "core/token.hpp"
#include "engine/i_engine_listener.hpp"
#include "engine/i_network_callback.hpp"
#include "engine/knowledge_base.hpp"
#include "engine/metrics_exporter.hpp"
#include "engine/agenda_selector.hpp"
#include "engine/data_source.hpp"

#include "engine/query_result.hpp"
#include "rete/network_memory.hpp"
#include "engine/rule_execution_tracer.hpp"
#include "engine/schema_validator.hpp"
#include "engine/working_memory.hpp"
#include "core/value_types.hpp"

#include "data/session_arena.hpp"
#include "data/token_pool.hpp" // Object pool for long-running engines
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
  void add_fact(std::shared_ptr<Fact> const& fact);
  void add_facts(std::vector<Fact*> const& facts);
  void add_facts(std::vector<std::shared_ptr<Fact>> const& facts);

  /**
   * @brief Add already constructed fact data.
   */
  void add_data(rulesforge::DataSource const& source);
  void add_data(Fact* fact) { add_fact(fact); }

  /**
   * @brief Insert a fact into a named entry point stream.
   */
  void insert_into(std::string const& stream_name, Fact* fact);
  void insert_into(std::string const& stream_name, std::shared_ptr<Fact> const& fact);
  void insert_event_into(std::string const& stream_name,
                         std::shared_ptr<Fact> const& fact,
                         std::int64_t event_time_ms);
  void retract_from(std::string const& stream_name, Fact* fact);
  void validate_entry_point_route(std::string const& stream_name,
                                  std::string const& fact_type) const;
  int fire_all_rules(int max_rules = -1);
  int fire_all_rules_fail_fast(int max_rules = -1);
  std::size_t advance_event_time(std::int64_t watermark_ms);
  void enable_event_time_mode() { event_time_mode_ = true; }
  bool is_event_time_mode() const { return event_time_mode_; }
  std::optional<std::int64_t> event_time_watermark() const { return event_time_watermark_ms_; }
  std::optional<std::int64_t> fact_event_time(Fact const* fact) const;
  std::size_t pending_activation_count() const { return agenda_.size(); }
  std::int64_t next_fact_id() const { return working_memory_.next_id(); }
  std::vector<Fact*> facts_snapshot() const { return working_memory_.snapshot(); }

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
  void track_rhs_update_snapshot(Fact const& fact) override;

  // ... (halt, is_consistent methods remain same) ...
  void halt() { halt_requested_ = true; }
  bool is_halt_requested() const { return halt_requested_; }
  bool is_consistent() const { return is_consistent_; }
  void begin_rhs_transaction() override;
  void end_rhs_transaction(bool commit) override;

  /**
   * @brief 型安全な fact 追加 API。
   *
   * 登録済みの C++ 型 T を RulesForge の Fact に変換して session に追加する。
   * T は事前に `KnowledgeBase::get_fact_type_registry().register_type<T>(...)` で
   * 登録されている必要がある。未登録の場合は std::runtime_error で即座に失敗する。
   *
   * @tparam T 非ポインタの class/struct 型（Concept: std::is_class_v<T>）
   * @param typed_fact 追加する C++ オブジェクト（値参照）
   * @throws std::runtime_error T が FactTypeRegistry に未登録の場合
   *
   * 所有権: Fact は session の FactArena が管理する。呼び出し元は typed_fact の
   * 所有権を保持したまま（コピー渡し）。
   */
  template <typename T>
      requires std::is_class_v<T> && (!std::is_pointer_v<T>)
  void add_fact_typed(T const& typed_fact)
  {
    Fact* fact = kb_->get_fact_type_registry().convert(fact_arena_, typed_fact);
    if (!fact) {
      throw std::runtime_error(
          std::string("add_fact_typed: type '") + typeid(T).name() +
          "' has not been registered with FactTypeRegistry. "
          "Call kb->get_fact_type_registry().register_type<T>(...) before inserting facts.");
    }
    add_fact(fact);
  }

  void set_focus(std::string const& group_name) override;
  std::string get_focus() const;
  size_t get_fact_count() const;
  int64_t get_next_fact_id();
  void set_global(std::string const& name, ConstraintValue value);
  std::optional<ConstraintValue> get_global(std::string const& name) const;

  void set_validation_mode(ValidationMode mode) {
      ensure_consistent_for_mutation("setting validation mode");
      validation_mode_ = mode;
  }
  ValidationMode get_validation_mode() const { return validation_mode_; }

  bool has_type_declaration(std::string const& type_name) const;
  std::string canonicalize_fact_type_name(std::string const& type_name) const;

  // Rule execution tracing
  RuleExecutionTracer& get_tracer() { return tracer_; }
  void enable_tracing(bool enabled = true) {
      ensure_consistent_for_mutation("setting tracing mode");
      tracer_.enable_tracing(enabled);
  }
  std::string get_execution_trace(bool include_network = false) const {
      return tracer_.format_trace(include_network);
  }
  std::string get_rule_performance_summary() const {
      return tracer_.format_rule_summary();
  }
  std::string get_agenda_implementation() const {
      return agenda_.implementation_name();
  }
  std::string get_execution_mode() const {
      return rulesforge::execution_mode_name(
          rulesforge::agenda_implementation_to_execution_mode(agenda_.implementation()));
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

  /**
   * @brief Reset session state for reuse
   *
   * Clears all runtime state while preserving the knowledge base.
   * Used by SessionPool to recycle sessions.
   *
   * WARNING: Does not reset network memory or compiled network.
   * Only suitable for stateless rule processing.
   */
  void reset();

  // --- Internal & INetworkCallback API ---
  void _internal_add_fact(Fact* fact);
  void _internal_add_facts_batch(std::vector<Fact*> const& facts);
  void _internal_remove_fact(int64_t fact_id);
  Fact* get_fact_by_id(int64_t id) override;
  void logical_insert(Token& token, Fact* fact) override;
  Fact* logical_insert(Fact const& fact) override;

  Fact* create_fact(std::string const& type) override;
  std::optional<FieldType> get_declared_field_type(std::string const& fact_type,
                                                   std::string_view field_name) const override;

  inline TokenWME const* get_or_create_wme(
      TokenWME const* parent_wme, Fact const* fact)
  {
    if (!parent_wme) {
         parent_wme = token_pool_.get_root();
    }

    size_t parent_hash = parent_wme ? parent_wme->hash : 0;
    size_t fact_hash = std::hash<const void*>{}(fact);
    size_t hash = parent_hash ^ (fact_hash + 0x9e3779b9 + (parent_hash << 6) + (parent_hash >> 2));

    auto it = wme_cache_.find(hash);
    if (it != wme_cache_.end()) {
      for (auto const* cached_wme : it->second) {
        if (cached_wme != nullptr
            && cached_wme->parent == parent_wme
            && cached_wme->fact == fact) {
          return cached_wme;
        }
      }
    }

    TokenWME* new_wme = token_pool_.create_token(parent_wme, fact);

    wme_cache_[hash].push_back(new_wme);
    return new_wme;
  }

  // Fast path: skip cache lookup when we know the WME is new (batch ASSERT)
  inline TokenWME const* create_wme_uncached(
      TokenWME const* parent_wme, Fact const* fact)
  {
    if (!parent_wme) {
         parent_wme = token_pool_.get_root();
    }
    return token_pool_.create_token(parent_wme, fact);
  }

  inline TokenWME const* get_dummy_wme() { return token_pool_.get_root(); }

  inline void invalidate_wme_cache(size_t hash) {
    auto it = wme_cache_.find(hash);
    if (it != wme_cache_.end()) {
      wme_cache_.erase(it);
    }
  }

  void add_activation(Activation const& activation);
  void add_activations_batch(std::vector<Activation>& activations);  // PHREAK: batch agenda insert
  void remove_activation(size_t activation_hash);
  void logical_retract(TokenWME const* wme);

  NetworkMemory& net_mem() { return net_mem_; }
  NetworkMemory const& net_mem() const { return net_mem_; }
  void allocate_network_memory(MemSlotCounts const& counts);
  void reset_runtime_counters();
  RuntimeCounters runtime_counters() const;
  rulesforge::ModifiedFieldsHint const* current_modified_fields() const { return current_modified_fields_; }

  void flush_pending_nodes();
  bool is_deferred_mode() const { return deferred_mode_; }

private:
  void retain_shared_fact(std::shared_ptr<Fact> const& fact);
  void release_retained_fact(int64_t fact_id);
  void ensure_consistent_for_mutation(char const* operation) const;
  void validate_fact_for_insert(Fact const& fact) const;
  void prime_network_state();
  void refresh_query_call_nodes();
  int fire_all_rules_impl(int max_rules, bool fail_fast);
  void fire_activation(Activation& activation, bool fail_fast);
  std::shared_ptr<ReteNode> require_entry_point_route(std::string const& stream_name,
                                                      std::string const& fact_type) const;
  bool is_node_pending_dirty(ReteNode const& node) const;
  void mark_phreak_dirty_for_node(int node_id);
  void mark_phreak_dirty_for_type(std::string const& fact_type);
  void clear_phreak_dirty_state();

  // Cache using raw pointers
  using TokenWMECache = std::unordered_map<size_t, std::vector<TokenWME const*>>;

  std::shared_ptr<KnowledgeBase const> kb_;

  NetworkMemory net_mem_;

  std::unique_ptr<rulesforge::RhsExecutor> rhs_executor_;
  std::unique_ptr<QueryEngine> query_engine_;
  std::unique_ptr<TruthMaintenanceSystem> tms_;
  RuleExecutionTracer tracer_;
  SessionArena arena_;
  rulesforge::TokenPool token_pool_;
  rulesforge::FactArena fact_arena_;

  // dummy_wme_ is managed by TokenArena (root)
  TokenWME const* dummy_wme_ = nullptr;
  TokenWMECache wme_cache_;
  WorkingMemory working_memory_;
  std::unordered_map<int64_t, std::shared_ptr<Fact>> retained_shared_facts_;
  Activation const* current_activation_ = nullptr;
  rulesforge::RuntimeAgenda agenda_;
  std::vector<std::shared_ptr<IEngineListener>> listeners_;
  bool halt_requested_ = false;

  bool in_rhs_transaction_ = false;
  bool is_consistent_ = true;
  bool deferred_mode_ = false;
  bool phreak_experimental_ = false;
  std::map<std::string, ConstraintValue> globals_;
  std::vector<int64_t> transaction_inserted_facts_;
  std::vector<Fact*> transaction_retracted_facts_;
  std::vector<int64_t> transaction_updated_fact_order_;
  std::unordered_map<int64_t, rulesforge::InternedKeyMap<ConstraintValue>> transaction_updated_fact_snapshots_;

  ValidationMode validation_mode_ = ValidationMode::None;
  std::unique_ptr<SchemaValidator> schema_validator_;



  mutable int64_t rules_fired_total_ = 0;
  mutable int64_t facts_inserted_total_ = 0;
  mutable int64_t facts_retracted_total_ = 0;
  RuntimeCounters runtime_counters_{};
  rulesforge::ModifiedFieldsHint const* current_modified_fields_ = nullptr;
  uint64_t phreak_epoch_ = 0;
  std::optional<std::int64_t> event_time_watermark_ms_;
  bool event_time_mode_ = false;
  std::unordered_map<Fact const*, std::int64_t> fact_event_times_ms_;
  std::map<int, std::shared_ptr<ReteNode>> get_nodes() const;
  friend class ReteSerializer;
};

#endif  // STATEFUL_SESSION_HPP
