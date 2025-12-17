#ifndef STATEFUL_SESSION_HPP
#define STATEFUL_SESSION_HPP
#include <queue>

#include <quickjs.h>

#include "drools_rete_defs.hpp"
#include "i_engine_listener.hpp"
#include "i_network_callback.hpp"
#include "knowledge_base.hpp"
#include "metrics_exporter.hpp"
#include "phmap.h"
#include "query_result.hpp"
#include "rule_execution_tracer.hpp"
#include "session_arena.hpp"

// Forward declarations
class JSScriptingManager;
class TruthMaintenanceSystem;
class ReteNode;
class QueryTerminalNode;
class QueryInputNode;
class BetaNetworkBuilder;

/**
 * @brief Simple free-list pool for TokenWME objects
 *
 * Provides O(1) allocation/deallocation with memory reuse.
 * Used with shared_ptr custom deleter for reference-counted pool objects.
 */
class TokenWMEPool {
public:
    static constexpr size_t CHUNK_SIZE = 256;

    TokenWMEPool() = default;
    ~TokenWMEPool() {
        // Free all allocated chunks
        for (auto* chunk : chunks_) {
            delete[] chunk;
        }
    }

    TokenWMEPool(TokenWMEPool const&) = delete;
    TokenWMEPool& operator=(TokenWMEPool const&) = delete;

    TokenWME* allocate() {
        if (!free_list_) {
            allocate_chunk();
        }
        Node* node = free_list_;
        free_list_ = node->next;
        return reinterpret_cast<TokenWME*>(&node->storage);
    }

    void deallocate(TokenWME* ptr) {
        if (!ptr) return;
        ptr->~TokenWME();
        Node* node = reinterpret_cast<Node*>(ptr);
        node->next = free_list_;
        free_list_ = node;
    }

private:
    struct Node {
        union {
            alignas(TokenWME) unsigned char storage[sizeof(TokenWME)];
            Node* next;
        };
    };

    void allocate_chunk() {
        Node* chunk = new Node[CHUNK_SIZE];
        chunks_.push_back(chunk);
        for (size_t i = 0; i < CHUNK_SIZE - 1; ++i) {
            chunk[i].next = &chunk[i + 1];
        }
        chunk[CHUNK_SIZE - 1].next = free_list_;
        free_list_ = chunk;
    }

    Node* free_list_ = nullptr;
    std::vector<Node*> chunks_;
};

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

  /**
   * @brief Insert a fact into a named entry point stream.
   * Facts inserted via this method will only match patterns that specify
   * `from entry-point "stream_name"` in their DRL definition.
   * @param stream_name The name of the entry point stream
   * @param fact The fact to insert
   */
  void insert_into(std::string const& stream_name, std::shared_ptr<Fact> fact);
  int fire_all_rules(int max_rules = -1);
  void retract_fact(std::shared_ptr<Fact> fact) override;
  void retract_facts(std::vector<std::shared_ptr<Fact>> const& facts);
  void update_fact(std::shared_ptr<Fact> fact,
                   std::function<void(Fact&)> modifier) override;

  /**
   * @brief Immediately stop rule execution.
   *
   * P1 FIX: Implement drools.halt() functionality.
   * When called, fire_all_rules() will stop after the current rule completes.
   * The halt flag is automatically reset at the start of fire_all_rules().
   */
  void halt() { halt_requested_ = true; }

  /**
   * @brief Check if halt has been requested.
   */
  bool is_halt_requested() const { return halt_requested_; }

  /**
   * @brief P1-001 FIX: Check if the session is in a consistent state.
   * A session is inconsistent if an RHS transaction failed without proper cleanup.
   */
  bool is_consistent() const { return is_consistent_; }

  /**
   * @brief P1-001 FIX: Begin tracking fact changes for potential rollback.
   */
  void begin_rhs_transaction() override;

  /**
   * @brief P1-001 FIX: End the current transaction.
   * @param commit If true, changes are kept. If false, inserted facts are retracted.
   */
  void end_rhs_transaction(bool commit) override;

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

  // Memory statistics
  SessionArena& get_arena() { return arena_; }
  std::string get_memory_stats() const { return arena_.format_stats(); }

  /**
   * @brief P1-002 FIX: Get current session metrics for monitoring
   * Collects all relevant metrics from the session state.
   */
  SessionMetrics get_metrics() const;

  /**
   * @brief P1-002 FIX: Export metrics using the provided exporter
   * Convenience method that combines get_metrics() and exporter.export_metrics()
   */
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
    // Lookup by hash first - avoids creating temporary WME for cache hit
    auto it = wme_cache_.find(hash);
    if (it != wme_cache_.end()) {
      return it->second;
    }
    // Allocate from pool and construct in place
    TokenWME* raw = wme_pool_.allocate();
    new (raw) TokenWME{parent_wme, fact, parent_wme->depth + 1, hash};
    // Wrap with custom deleter that returns to pool
    auto new_wme = std::shared_ptr<TokenWME const>(raw, [this](TokenWME const* p) {
        wme_pool_.deallocate(const_cast<TokenWME*>(p));
    });
    wme_cache_[hash] = new_wme;
    return new_wme;
  }

  inline std::shared_ptr<TokenWME const> get_dummy_wme() { return dummy_wme_; }

  // Invalidate a WME from the cache (needed for MODIFY to work correctly)
  inline void invalidate_wme_cache(size_t hash) {
    wme_cache_.erase(hash);
  }

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

  using TokenWMECache = map<size_t, std::shared_ptr<TokenWME const>>;  // hash -> WME

  // --- Immutable Reference ---
  std::shared_ptr<KnowledgeBase const> kb_;

  // --- TokenWME Pool (MUST be first mutable member) ---
  // CRITICAL: wme_pool_ must be declared before ALL members that might hold
  // TokenWME shared_ptrs (nodes, caches, etc). C++ destroys members in reverse
  // declaration order, so this ensures the pool is destroyed LAST.
  TokenWMEPool wme_pool_;

  // --- Mutable State ---
  std::unique_ptr<JSScriptingManager> scripting_manager_;
  std::unique_ptr<TruthMaintenanceSystem> tms_;
  RuleExecutionTracer tracer_;
  SessionArena arena_;  // Per-session memory arena

  // Rete network instance state
  mutable int64_t next_fact_id_ = 1;
  int next_node_id_ = 0;
  std::vector<std::shared_ptr<ReteNode>> all_nodes_;
  map<std::string, std::shared_ptr<ReteNode>> alpha_entry_points_;
  // Outer key: entry point name, Inner key: fact type
  map<std::string, map<std::string, std::shared_ptr<ReteNode>>> named_entry_points_;
  map<std::string, std::shared_ptr<QueryTerminalNode>> query_nodes_;
  map<std::string, std::shared_ptr<QueryInputNode>>
      parameterized_query_inputs_;

  // Working memory state
  std::shared_ptr<TokenWME const> dummy_wme_;
  TokenWMECache wme_cache_;
  map<int64_t, std::shared_ptr<Fact>> all_facts_;
  std::vector<std::string> agenda_group_focus_stack_;
  map<size_t, Activation> agenda_map_;
  std::priority_queue<std::pair<int, size_t>> agenda_queue_;
  std::vector<std::shared_ptr<IEngineListener>> listeners_;
  unordered_set<size_t> no_loop_blocked_;  // Blocked activations for no-loop rules
  bool halt_requested_ = false;  // P1 FIX: drools.halt() support

  // P1 FIX: activation-group support - maps group name to activation hashes
  map<std::string, unordered_set<size_t>> activation_group_map_;

  // P1 FIX: lock-on-active support - rules locked during current firing cycle
  unordered_set<ParsedRule const*> lock_on_active_blocked_;
  struct DelayedActivation {
    std::chrono::steady_clock::time_point fire_time;
    Activation activation;
    bool operator>(DelayedActivation const& other) const {
      return fire_time > other.fire_time;  // Min-heap: earliest first
    }
  };
  std::priority_queue<DelayedActivation, std::vector<DelayedActivation>, std::greater<DelayedActivation>> delayed_activations_;

  // P1-001 FIX: Transaction tracking for rollback on JS exception
  bool in_rhs_transaction_ = false;
  bool is_consistent_ = true;
  std::vector<int64_t> transaction_inserted_facts_;  // Facts to retract on rollback

  // P1-002 FIX: Metrics counters for monitoring
  mutable int64_t rules_fired_total_ = 0;
  mutable int64_t facts_inserted_total_ = 0;
  mutable int64_t facts_retracted_total_ = 0;
  map<int, std::shared_ptr<ReteNode>> get_nodes() const;
  friend class ReteSerializer;
};

#endif  // STATEFUL_SESSION_HPP


