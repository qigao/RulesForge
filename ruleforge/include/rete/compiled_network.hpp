#ifndef COMPILED_NETWORK_HPP
#define COMPILED_NETWORK_HPP

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <arena_buffer.h>
#include "rete/rete_node.hpp"
#include "phmap.h"

// C++ allocator backed by turbo_arena_t.
// allocate() bumps the arena pointer (fast, contiguous).
// deallocate() is a no-op — the arena frees everything at once on destruction.
template<typename T>
struct ArenaAllocator {
    using value_type = T;

    turbo_arena_t* arena;

    explicit ArenaAllocator(turbo_arena_t* a) noexcept : arena(a) {}

    template<typename U>
    ArenaAllocator(ArenaAllocator<U> const& o) noexcept : arena(o.arena) {}

    T* allocate(size_t n) {
        void* p = turbo_arena_alloc(arena, n * sizeof(T));
        if (!p) throw std::bad_alloc();
        return static_cast<T*>(p);
    }

    void deallocate(T*, size_t) noexcept { /* arena frees in bulk */ }

    template<typename U>
    bool operator==(ArenaAllocator<U> const& o) const noexcept { return arena == o.arena; }
};

namespace detail {

// Structural hash for ParsedConstraint — avoids ostringstream overhead.
// Combines the fields that define alpha-node identity (field, op, literal/list).
inline size_t constraint_hash(ParsedConstraint const& c) {
    size_t h = 0;
    auto combine = [&](size_t v) { h ^= v + 0x9e3779b9 + (h << 6) + (h >> 2); };
    combine(std::hash<std::string>{}(c.left_field));
    combine(static_cast<size_t>(c.op));
    if (c.right_literal) {
        combine(ConstraintValueHasher{}(*c.right_literal));
    }
    if (c.right_value_list) {
        for (auto const& v : *c.right_value_list) {
            combine(ConstraintValueHasher{}(v));
        }
    }
    if (c.left_binding) {
        combine(std::hash<std::string>{}(*c.left_binding));
    }
    return h;
}

struct AlphaCacheKey {
    int parent_id;
    size_t constraint_hash_val;
    ParsedConstraint const* constraint_ptr; // for equality check on collision

    bool operator==(AlphaCacheKey const& o) const {
        return parent_id == o.parent_id
            && constraint_hash_val == o.constraint_hash_val
            && *constraint_ptr == *o.constraint_ptr;
    }
};

struct AlphaCacheHasher {
    size_t operator()(AlphaCacheKey const& k) const {
        size_t h = std::hash<int>{}(k.parent_id);
        h ^= k.constraint_hash_val + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

} // namespace detail

struct CompiledNetwork {
    // Arena MUST be declared before all_nodes so it outlives the shared_ptrs.
    // Destruction order: alpha_cache_ → assign_mem_slot (trivial) → ... → all_nodes → arena_
    turbo_arena_t arena_;

    std::vector<std::shared_ptr<ReteNode>> all_nodes;
    ruleforge::map<std::string, std::shared_ptr<ReteNode>> alpha_entry_points;
    ruleforge::map<std::string, ruleforge::map<std::string, std::shared_ptr<ReteNode>>> named_entry_points;
    ruleforge::map<std::string, std::shared_ptr<QueryTerminalNode>> query_nodes;
    ruleforge::map<std::string, std::shared_ptr<QueryInputNode>> parameterized_query_inputs;
    MemSlotCounts mem_slot_counts;
    int next_node_id = 0;

    CompiledNetwork() {
        // 4MB initial arena — enough for ~10K nodes without realloc
        turbo_arena_init(&arena_, 4 * 1024 * 1024);
    }

    ~CompiledNetwork() {
        // Clear all shared_ptrs first (runs destructors while arena memory is still valid)
        alpha_cache_.clear();
        parameterized_query_inputs.clear();
        query_nodes.clear();
        named_entry_points.clear();
        alpha_entry_points.clear();
        all_nodes.clear();
        // Now safe to free the arena
        turbo_arena_free(&arena_);
    }

    CompiledNetwork(CompiledNetwork const&) = delete;
    CompiledNetwork& operator=(CompiledNetwork const&) = delete;
    CompiledNetwork(CompiledNetwork&&) = delete;
    CompiledNetwork& operator=(CompiledNetwork&&) = delete;

    template<typename T, typename... Args>
    std::shared_ptr<T> create_node(Args&&... args) {
        ArenaAllocator<T> alloc(&arena_);
        auto node = std::allocate_shared<T>(alloc, std::forward<Args>(args)...);
        node->id = next_node_id++;
        assign_mem_slot(*node);
        all_nodes.push_back(node);
        return node;
    }

    // Alpha node deduplication via structural hash — O(1) lookup, no string allocation.
    std::shared_ptr<AlphaNode> find_or_create_alpha(
            std::shared_ptr<ReteNode> const& parent,
            ParsedConstraint const& constraint) {
        size_t ch = detail::constraint_hash(constraint);
        detail::AlphaCacheKey key{parent->id, ch, &constraint};
        auto it = alpha_cache_.find(key);
        if (it != alpha_cache_.end()) {
            return it->second;
        }
        auto node = create_node<AlphaNode>(constraint);
        parent->add_child(node);
        // Store key with pointer to the node's own constraint (stable address)
        detail::AlphaCacheKey stored_key{parent->id, ch, &node->constraint};
        alpha_cache_[stored_key] = node;
        return node;
    }

private:
    // Cache for alpha node sharing: structural hash → AlphaNode
    ruleforge::unordered_map<detail::AlphaCacheKey, std::shared_ptr<AlphaNode>, detail::AlphaCacheHasher> alpha_cache_;
    void assign_mem_slot(ReteNode& node) {
        switch (node.kind) {
            case NodeKind::HashedJoin:
                node.mem_slot = mem_slot_counts.hashed_join++;
                break;
            case NodeKind::CrossProductJoin:
                node.mem_slot = mem_slot_counts.cross_product_join++;
                break;
            case NodeKind::Not:
            case NodeKind::Exists:
                node.mem_slot = mem_slot_counts.beta_condition++;
                break;
            case NodeKind::Accumulate:
                node.mem_slot = mem_slot_counts.accumulate++;
                break;
            case NodeKind::Terminal:
                node.mem_slot = mem_slot_counts.terminal++;
                break;
            case NodeKind::QueryTerminal:
                node.mem_slot = mem_slot_counts.query_terminal++;
                break;
            case NodeKind::Eval:
                node.mem_slot = mem_slot_counts.eval++;
                break;
            case NodeKind::Unnest:
                node.mem_slot = mem_slot_counts.unnest++;
                break;
            default:
                break; // Alpha, EntryPoint, QueryInput don't need mem_slot
        }
    }
};

#endif // COMPILED_NETWORK_HPP
