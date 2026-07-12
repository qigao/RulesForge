#ifndef NETWORK_MEMORY_HPP
#define NETWORK_MEMORY_HPP

#include <memory>
#include <set>
#include <vector>
#include "core/token.hpp"
#include "engine/rfl_accumulators.hpp"
#include <unordered_map>
#include <unordered_set>



struct MemSlotCounts {
    int alpha = 0;
    int segment = 0;
    int path = 0;
    int hashed_join = 0;
    int cross_product_join = 0;
    int beta_condition = 0;
    int accumulate = 0;
    int terminal = 0;
    int query_terminal = 0;
    int query_call = 0;
    int eval = 0;
    int unnest = 0;
    int window = 0;
};

struct NetworkMemory {
    // --- PHREAK Segment state (skeleton; not wired to execution yet) ---
    struct SegmentMem {
        bool linked = true;
        uint64_t dirty_mask = 0;
        uint64_t dirty_epoch = 0;
    };

    // --- PHREAK Path state (skeleton; not wired to execution yet) ---
    struct PathMem {
        bool dirty = false;
        uint64_t dirty_segments_mask = 0;
        uint64_t eval_epoch = 0;
    };

    // --- AlphaNode state ---
    struct AlphaMem {
        std::unordered_set<int64_t> passing_facts;
    };

    // --- HashedJoinNode state ---
    struct HashedJoinMem {
        std::unordered_map<ConstraintValue,
            std::vector<TokenWME const*>,
            ConstraintValueHasher,
            ConstraintValueEquals> left;
        std::unordered_map<ConstraintValue,
            std::vector<Fact*>,
            ConstraintValueHasher,
            ConstraintValueEquals> right;
        std::unordered_map<TokenWME const*,
            std::vector<TokenWME const*>> left_to_children;
        std::unordered_map<int64_t,
            std::vector<TokenWME const*>> right_to_children;
        bool right_index_dirty = false;  // PHREAK: lazy right_to_children rebuild
        // Deferred evaluation pending queue
        std::vector<Fact*> pending_facts;
        bool dirty = false;
    };

    // --- CrossProductJoinNode state ---
    struct CrossProductJoinMem {
        std::unordered_map<TokenWME const*, TokenWME const*> left;
        std::unordered_map<int64_t, Fact*> right;
        std::unordered_map<TokenWME const*,
            std::vector<TokenWME const*>> left_to_children;
        std::unordered_map<int64_t,
            std::vector<TokenWME const*>> right_to_children;
        bool right_index_dirty = false;  // PHREAK: lazy right_to_children rebuild
        // Deferred evaluation pending queue
        std::vector<Fact*> pending_facts;
        bool dirty = false;
    };

    // --- BetaConditionNode (Not/Exists) state ---
    struct BetaConditionMem {
        struct LeftMemoryItem {
            TokenWME const* wme;
            std::unordered_set<int64_t> matched_fact_ids;
        };
        std::unordered_map<TokenWME const*, LeftMemoryItem> left;
        std::unordered_map<int64_t, Fact*> right;
        std::unordered_map<ConstraintValue,
            std::vector<TokenWME const*>,
            ConstraintValueHasher,
            ConstraintValueEquals> left_index;
        std::unordered_map<ConstraintValue,
            std::vector<Fact*>,
            ConstraintValueHasher,
            ConstraintValueEquals> right_index;
        std::unordered_map<TokenWME const*, ConstraintValue> left_keys;
        std::unordered_map<int64_t, ConstraintValue> right_keys;
        std::vector<TokenWME const*> left_unindexed;
        std::vector<Fact*> right_unindexed;
        // Deferred evaluation pending queue
        std::vector<Fact*> pending_facts;
        bool dirty = false;
    };

    // --- AccumulateNode state ---
    struct AccumulateMem {
        struct LeftMemoryItem {
            TokenWME const* wme;
            Fact* result_fact;
            std::unique_ptr<IAccumulator> accumulator;
            double numeric_sum = 0.0;
            double numeric_extreme = 0.0;
            int64_t aggregate_count = 0;
            bool sum_is_double = false;
            std::multiset<double> numeric_values;
            std::vector<Fact*> contributing_facts_list;
            std::unordered_set<Fact*> contributing_facts_set;

            LeftMemoryItem() = default;
            LeftMemoryItem(LeftMemoryItem&&) noexcept = default;
            LeftMemoryItem& operator=(LeftMemoryItem&&) noexcept = default;
            LeftMemoryItem(LeftMemoryItem const&) = delete;
            LeftMemoryItem& operator=(LeftMemoryItem const&) = delete;
        };
        std::unordered_map<TokenWME const*, LeftMemoryItem> left;
        std::unordered_map<int64_t, Fact*> right;
        // Deferred evaluation pending queue
        std::vector<Fact*> pending_facts;
        bool dirty = false;

        AccumulateMem() = default;
        AccumulateMem(AccumulateMem&&) noexcept = default;
        AccumulateMem& operator=(AccumulateMem&&) noexcept = default;
        AccumulateMem(AccumulateMem const&) = delete;
        AccumulateMem& operator=(AccumulateMem const&) = delete;
    };

    // --- TerminalNode state ---
    struct TerminalMem {
        std::unordered_set<TokenWME const*> memory;
    };

    // --- QueryTerminalNode state ---
    struct QueryTerminalMem {
        std::map<TokenWME const*, Token> results;
    };

    // --- QueryCallNode state ---
    struct QueryCallMem {
        std::unordered_map<TokenWME const*, Token> left;
        std::unordered_map<TokenWME const*, std::vector<TokenWME const*>> parent_to_children;
    };

    // --- EvalNode state ---
    struct EvalMem {
        std::unordered_map<TokenWME const*, TokenWME const*> memory;
    };

    // --- UnnestNode state ---
    struct UnnestMem {
        std::unordered_map<TokenWME const*,
            std::pair<TokenWME const*,
                      std::vector<TokenWME const*>>> parent_to_children;
    };

    // --- WindowNode state ---
    struct WindowMem {
        std::vector<Fact*> facts; // Used as a simple deque/buffer for sliding windows
    };


    // Flat arrays — one entry per node of that kind, indexed by mem_slot
    std::vector<AlphaMem> alpha;
    std::vector<SegmentMem> segment;
    std::vector<PathMem> path;
    std::vector<HashedJoinMem> hashed_join;
    std::vector<CrossProductJoinMem> cross_product_join;
    std::vector<BetaConditionMem> beta_condition;
    std::vector<AccumulateMem> accumulate;
    std::vector<TerminalMem> terminal;
    std::vector<QueryTerminalMem> query_terminal;
    std::vector<QueryCallMem> query_call;
    std::vector<EvalMem> eval;
    std::vector<UnnestMem> unnest;
    std::vector<WindowMem> window;

    void allocate(MemSlotCounts const& counts) {
        alpha.resize(counts.alpha);
        segment.resize(counts.segment);
        path.resize(counts.path);
        hashed_join.resize(counts.hashed_join);
        cross_product_join.resize(counts.cross_product_join);
        beta_condition.resize(counts.beta_condition);
        accumulate.resize(counts.accumulate);
        terminal.resize(counts.terminal);
        query_terminal.resize(counts.query_terminal);
        query_call.resize(counts.query_call);
        eval.resize(counts.eval);
        unnest.resize(counts.unnest);
        window.resize(counts.window);
    }
};

#endif // NETWORK_MEMORY_HPP
