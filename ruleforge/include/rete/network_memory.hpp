#ifndef NETWORK_MEMORY_HPP
#define NETWORK_MEMORY_HPP

#include <memory>
#include <vector>
#include "rfl_rete_defs.hpp"
#include "rfl_accumulators.hpp"
#include "phmap.h"

struct MemSlotCounts {
    int hashed_join = 0;
    int cross_product_join = 0;
    int beta_condition = 0;
    int accumulate = 0;
    int terminal = 0;
    int query_terminal = 0;
    int eval = 0;
    int unnest = 0;
};

struct NetworkMemory {
    // --- HashedJoinNode state ---
    struct HashedJoinMem {
        ruleforge::unordered_map<ConstraintValue,
            std::vector<std::shared_ptr<TokenWME const>>,
            ConstraintValueHasher> left;
        ruleforge::unordered_map<ConstraintValue,
            std::vector<std::shared_ptr<Fact>>,
            ConstraintValueHasher> right;
        ruleforge::unordered_map<TokenWME const*,
            std::vector<std::shared_ptr<TokenWME const>>> left_to_children;
        ruleforge::unordered_map<int64_t,
            std::vector<std::shared_ptr<TokenWME const>>> right_to_children;
        // Deferred evaluation pending queue
        std::vector<std::shared_ptr<Fact>> pending_facts;
        bool dirty = false;
    };

    // --- CrossProductJoinNode state ---
    struct CrossProductJoinMem {
        ruleforge::unordered_map<TokenWME const*, std::shared_ptr<TokenWME const>> left;
        ruleforge::unordered_map<int64_t, std::shared_ptr<Fact>> right;
        ruleforge::unordered_map<TokenWME const*,
            std::vector<std::shared_ptr<TokenWME const>>> left_to_children;
        ruleforge::unordered_map<int64_t,
            std::vector<std::shared_ptr<TokenWME const>>> right_to_children;
        // Deferred evaluation pending queue
        std::vector<std::shared_ptr<Fact>> pending_facts;
        bool dirty = false;
    };

    // --- BetaConditionNode (Not/Exists) state ---
    struct BetaConditionMem {
        struct LeftMemoryItem {
            std::shared_ptr<TokenWME const> wme;
            size_t match_count = 0;
        };
        ruleforge::unordered_map<TokenWME const*, LeftMemoryItem> left;
        ruleforge::unordered_map<int64_t, std::shared_ptr<Fact>> right;
        // Deferred evaluation pending queue
        std::vector<std::shared_ptr<Fact>> pending_facts;
        bool dirty = false;
    };

    // --- AccumulateNode state ---
    struct AccumulateMem {
        struct LeftMemoryItem {
            std::shared_ptr<TokenWME const> wme;
            std::shared_ptr<Fact> result_fact;
            std::unique_ptr<IAccumulator> accumulator;
            std::vector<std::shared_ptr<Fact>> contributing_facts_list;
            ruleforge::unordered_set<std::shared_ptr<Fact>> contributing_facts_set;
        };
        ruleforge::unordered_map<TokenWME const*, LeftMemoryItem> left;
        ruleforge::unordered_map<int64_t, std::shared_ptr<Fact>> right;
        // Deferred evaluation pending queue
        std::vector<std::shared_ptr<Fact>> pending_facts;
        bool dirty = false;
    };

    // --- TerminalNode state ---
    struct TerminalMem {
        ruleforge::unordered_set<TokenWME const*> memory;
    };

    // --- QueryTerminalNode state ---
    struct QueryTerminalMem {
        ruleforge::map<TokenWME const*, Token> results;
    };

    // --- EvalNode state ---
    struct EvalMem {
        ruleforge::unordered_map<TokenWME const*, std::shared_ptr<TokenWME const>> memory;
    };

    // --- UnnestNode state ---
    struct UnnestMem {
        ruleforge::unordered_map<TokenWME const*,
            std::pair<std::shared_ptr<TokenWME const>,
                      std::vector<std::shared_ptr<TokenWME const>>>> parent_to_children;
    };

    // Flat arrays — one entry per node of that kind, indexed by mem_slot
    std::vector<HashedJoinMem> hashed_join;
    std::vector<CrossProductJoinMem> cross_product_join;
    std::vector<BetaConditionMem> beta_condition;
    std::vector<AccumulateMem> accumulate;
    std::vector<TerminalMem> terminal;
    std::vector<QueryTerminalMem> query_terminal;
    std::vector<EvalMem> eval;
    std::vector<UnnestMem> unnest;

    void allocate(MemSlotCounts const& counts) {
        hashed_join.resize(counts.hashed_join);
        cross_product_join.resize(counts.cross_product_join);
        beta_condition.resize(counts.beta_condition);
        accumulate.resize(counts.accumulate);
        terminal.resize(counts.terminal);
        query_terminal.resize(counts.query_terminal);
        eval.resize(counts.eval);
        unnest.resize(counts.unnest);
    }
};

#endif // NETWORK_MEMORY_HPP
