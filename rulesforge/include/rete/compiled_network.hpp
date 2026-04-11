#ifndef COMPILED_NETWORK_HPP
#define COMPILED_NETWORK_HPP

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <algorithm>
#include <unordered_map>
#include <turbo_buffer.h>
#include "rete/rete_node.hpp"


// C++ allocator backed by mem_pool_t.
// allocate() bumps the arena pointer (fast, contiguous).
// deallocate() is a no-op — the arena frees everything at once on destruction.
template<typename T>
struct ArenaAllocator {
    using value_type = T;

    mem_pool_t* arena;

    explicit ArenaAllocator(mem_pool_t* a) noexcept : arena(a) {}

    template<typename U>
    ArenaAllocator(ArenaAllocator<U> const& o) noexcept : arena(o.arena) {}

    T* allocate(size_t n) {
        void* p = mem_alloc(arena, n * sizeof(T));
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

struct BetaCacheKey {
    NodeKind kind;
    std::vector<int> parent_ids;
    std::vector<ParsedConstraint> constraints;
    // Map comparison/hashing is expensive, but necessary for correctness.
    // Using a sorted vector of pairs might be faster for hashing/eq if map is small.
    // keys is sorted by assumption/construction if we convert map->vector?
    // For now, store the map.
    std::map<std::string, int> bindings;

    // Extra fields for HashedJoin
    std::string right_hash_field;
    std::pair<std::string, int> left_hash_info; // pair<name, depth>

    // EvalNode specific
    std::string eval_expr;
    std::map<std::string, std::string> eval_scalar_fields;

    bool operator==(BetaCacheKey const& o) const {
        return kind == o.kind
            && parent_ids == o.parent_ids
            && constraints == o.constraints
            && bindings == o.bindings
            && right_hash_field == o.right_hash_field
            && left_hash_info == o.left_hash_info
            && eval_expr == o.eval_expr
            && eval_scalar_fields == o.eval_scalar_fields;
    }
};

struct BetaCacheHasher {
    size_t operator()(BetaCacheKey const& k) const {
        size_t h = std::hash<int>{}(static_cast<int>(k.kind));
        for (int pid : k.parent_ids) {
            h ^= std::hash<int>{}(pid) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        for (auto const& c : k.constraints) {
             h ^= detail::constraint_hash(c) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }

        // Order-independent hash for bindings map
        size_t map_h = 0;
        for (auto const& kv : k.bindings) {
            size_t entry = std::hash<std::string>{}(kv.first) ^ std::hash<int>{}(kv.second);
            // scramble
            entry = (entry ^ (entry >> 16)) * 0x45d9f3b;
            map_h ^= entry;
        }
        h ^= map_h + 0x9e3779b9 + (h << 6) + (h >> 2);

        if (!k.right_hash_field.empty()) {
             h ^= std::hash<std::string>{}(k.right_hash_field) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        if (!k.left_hash_info.first.empty()) {
             h ^= std::hash<std::string>{}(k.left_hash_info.first);
             h ^= std::hash<int>{}(k.left_hash_info.second);
        }
        if (!k.eval_expr.empty()) {
            h ^= std::hash<std::string>{}(k.eval_expr) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        size_t scalar_h = 0;
        for (auto const& kv : k.eval_scalar_fields) {
            size_t entry = std::hash<std::string>{}(kv.first) ^ (std::hash<std::string>{}(kv.second) << 1);
            entry = (entry ^ (entry >> 16)) * 0x45d9f3b;
            scalar_h ^= entry;
        }
        h ^= scalar_h + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

} // namespace detail

struct CompiledNetwork {
    struct SegmentDescriptor {
        int id = -1;
        std::vector<int> node_ids;
        bool linked = true;
        uint64_t dirty_mask = 0;
    };

    struct PathDescriptor {
        int id = -1;
        int terminal_node_id = -1;
        std::vector<int> segment_ids;
        bool dirty = false;
        uint64_t dirty_segments_mask = 0;
    };

    // Arena MUST be declared before all_nodes so it outlives the shared_ptrs.
    // Destruction order: caches -> all_nodes -> arena_
    mem_pool_t arena_;

    std::vector<std::shared_ptr<ReteNode>> all_nodes;
    std::vector<ReteNode*> node_index;
    std::vector<ReteNode*> beta_root_nodes; // Optimization: only iterate these for priming
    std::map<std::string, std::shared_ptr<ReteNode>> alpha_entry_points;
    std::map<std::string, std::map<std::string, std::shared_ptr<ReteNode>>> named_entry_points;
    std::map<std::string, std::shared_ptr<QueryTerminalNode>> query_nodes;
    std::map<std::string, std::shared_ptr<QueryInputNode>> parameterized_query_inputs;
    std::vector<SegmentDescriptor> segment_descriptors;
    std::vector<PathDescriptor> path_descriptors;
    std::vector<std::vector<int>> segment_to_paths;
    std::unordered_map<std::string, std::vector<int>> type_to_segment_ids;
    std::unordered_map<std::string, std::vector<int>> type_to_path_ids;
    std::unordered_map<int, int> node_to_segment_id;
    std::unordered_map<int, int> terminal_to_path_id;
    MemSlotCounts mem_slot_counts;
    int next_node_id = 0;

    CompiledNetwork() {
        // 4MB initial arena — enough for ~10K nodes without realloc
        mem_init(&arena_, 4 * 1024 * 1024);
    }

    ~CompiledNetwork() {
        // Clear all shared_ptrs first (runs destructors while arena memory is still valid)
        alpha_cache_.clear();
        beta_cache_.clear();
        parameterized_query_inputs.clear();
        query_nodes.clear();
        named_entry_points.clear();
        alpha_entry_points.clear();
        node_index.clear();
        segment_descriptors.clear();
        path_descriptors.clear();
        segment_to_paths.clear();
        type_to_segment_ids.clear();
        type_to_path_ids.clear();
        node_to_segment_id.clear();
        terminal_to_path_id.clear();
        all_nodes.clear();
        // Now safe to free the arena
        mem_destroy(&arena_);
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
        if (static_cast<size_t>(node->id) >= node_index.size()) {
            node_index.resize(static_cast<size_t>(node->id + 1), nullptr);
        }
        node_index[static_cast<size_t>(node->id)] = node.get();
        return node;
    }

    // Build PHREAK skeleton metadata (segments + paths) without changing execution semantics.
    // Current policy: one segment per beta node; each terminal/query-terminal owns one path.
    void build_phreak_skeleton() {
        segment_descriptors.clear();
        path_descriptors.clear();
        segment_to_paths.clear();
        type_to_segment_ids.clear();
        type_to_path_ids.clear();
        node_to_segment_id.clear();
        terminal_to_path_id.clear();

        // 1) Build segment descriptors for beta nodes.
        for (auto const& node : all_nodes) {
            if (!node->is_beta_node()) {
                continue;
            }
            int seg_id = static_cast<int>(segment_descriptors.size());
            SegmentDescriptor seg;
            seg.id = seg_id;
            seg.node_ids.push_back(node->id);
            segment_descriptors.push_back(std::move(seg));
            node_to_segment_id[node->id] = seg_id;
        }

        // 2) Build path descriptors for each terminal.
        for (auto const& node : all_nodes) {
            if (node->kind != NodeKind::Terminal && node->kind != NodeKind::QueryTerminal) {
                continue;
            }

            PathDescriptor path;
            path.id = static_cast<int>(path_descriptors.size());
            path.terminal_node_id = node->id;

            // Walk first-parent chain upward, collect beta-node segments.
            std::shared_ptr<ReteNode> cursor = node;
            while (cursor && !cursor->get_parents().empty()) {
                auto const& parents = cursor->get_parents();
                auto parent_sp = parents.front().lock();
                if (!parent_sp) {
                    break;
                }
                if (parent_sp->is_beta_node()) {
                    auto it = node_to_segment_id.find(parent_sp->id);
                    if (it != node_to_segment_id.end()) {
                        path.segment_ids.push_back(it->second);
                    }
                }
                cursor = parent_sp;
            }
            std::reverse(path.segment_ids.begin(), path.segment_ids.end());

            terminal_to_path_id[path.terminal_node_id] = path.id;
            path_descriptors.push_back(std::move(path));
        }

        segment_to_paths.assign(segment_descriptors.size(), {});
        for (auto const& path : path_descriptors) {
            for (int seg_id : path.segment_ids) {
                if (seg_id >= 0 && static_cast<size_t>(seg_id) < segment_to_paths.size()) {
                    segment_to_paths[seg_id].push_back(path.id);
                }
            }
        }

        // 3) Build type -> (segment,path) quick lookup for runtime dirty marking.
        auto collect_for_entry = [&](std::string const& type, std::shared_ptr<ReteNode> const& entry) {
            if (!entry) return;
            std::vector<int> segs;
            std::vector<int> paths;
            std::unordered_set<int> seen_nodes;
            std::unordered_set<int> seen_segs;
            std::unordered_set<int> seen_paths;
            std::vector<ReteNode const*> stack;
            stack.push_back(entry.get());
            while (!stack.empty()) {
                ReteNode const* node = stack.back();
                stack.pop_back();
                if (!node) continue;
                if (!seen_nodes.insert(node->id).second) continue;
                auto it_seg = node_to_segment_id.find(node->id);
                if (it_seg != node_to_segment_id.end()) {
                    int seg_id = it_seg->second;
                    if (seen_segs.insert(seg_id).second) {
                        segs.push_back(seg_id);
                        if (seg_id >= 0 && static_cast<size_t>(seg_id) < segment_to_paths.size()) {
                            for (int path_id : segment_to_paths[seg_id]) {
                                if (seen_paths.insert(path_id).second) {
                                    paths.push_back(path_id);
                                }
                            }
                        }
                    }
                }
                for (auto const& child_w : node->get_children()) {
                    auto child = child_w.lock();
                    if (child) stack.push_back(child.get());
                }
            }
            type_to_segment_ids[type] = std::move(segs);
            type_to_path_ids[type] = std::move(paths);
        };

        for (auto const& [type, entry] : alpha_entry_points) {
            collect_for_entry(type, entry);
        }

        // Expose capacities for session memory allocation.
        mem_slot_counts.segment = static_cast<int>(segment_descriptors.size());
        mem_slot_counts.path = static_cast<int>(path_descriptors.size());
    }

    // Alpha node deduplication via structural hash — O(1) lookup
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

    // Generic Beta Node Sharing
    template<typename NodeType, typename... Args>
    std::shared_ptr<ReteNode> find_or_create_beta_node(
        std::vector<std::shared_ptr<ReteNode>> const& parents,
        detail::BetaCacheKey&& key,
        Args&&... args)
    {

        // key.kind must be set by the caller!
        key.parent_ids.reserve(parents.size());
        bool has_beta_parent = false;
        for (auto const& p : parents) {
            key.parent_ids.push_back(p->id);
            if (p->is_beta_node()) has_beta_parent = true;
        }
        std::sort(key.parent_ids.begin(), key.parent_ids.end()); // Canonical order for set-parents

        auto it = beta_cache_.find(key);
        if (it != beta_cache_.end()) {
            return it->second;
        }

        auto node = create_node<NodeType>(std::forward<Args>(args)...);
        for (auto& p : parents) {
            p->add_child(node);
        }

        beta_cache_[std::move(key)] = node;

        if (!has_beta_parent) {
            beta_root_nodes.push_back(node.get());
        }
        return node;
    }

private:
    // Cache for alpha node sharing
    std::unordered_map<detail::AlphaCacheKey, std::shared_ptr<AlphaNode>, detail::AlphaCacheHasher> alpha_cache_;
    // Cache for beta node sharing
    std::unordered_map<detail::BetaCacheKey, std::shared_ptr<ReteNode>, detail::BetaCacheHasher> beta_cache_;

    void assign_mem_slot(ReteNode& node) {
        switch (node.kind) {
            case NodeKind::Alpha:
                node.mem_slot = mem_slot_counts.alpha++;
                break;
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
            case NodeKind::Window:
                node.mem_slot = mem_slot_counts.window++;
                break;
            default:
                break; // Alpha, EntryPoint, QueryInput don't need mem_slot
        }
    }
};

#endif // COMPILED_NETWORK_HPP
