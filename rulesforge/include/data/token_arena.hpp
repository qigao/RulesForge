#ifndef TOKEN_ARENA_HPP
#define TOKEN_ARENA_HPP

#include "core/token.hpp"
#include <functional>
#include <memory>
#include <vector>

namespace rulesforge {

// Arena dedicated to TokenWMEs.
// Uses mem_pool_t for high-performance, contiguous allocation.
class TokenArena {
public:
    explicit TokenArena(size_t size = 64 * 1024 * 1024) : reserved_size_(size) {
         tokens_.reserve(1024);
         init_root();
    }

    ~TokenArena() = default;

    TokenWME* create_token(TokenWME const* parent, Fact const* fact) {
        TokenWME* token = allocate_raw();
        token->parent = parent ? parent : root_;
        token->fact = fact;
        token->depth = token->parent->depth + 1;

        // Compute hash
        size_t h = token->parent->hash;
        // Combine with fact pointer hash
        size_t fact_h = std::hash<const void*>{}(fact);
        h ^= fact_h + 0x9e3779b9 + (h << 6) + (h >> 2);
        token->hash = h;

        return token;
    }

    TokenWME const* get_root() const { return root_; }

    void reset() {
        tokens_.clear();
        init_root();
    }

    size_t memory_usage() const {
        return tokens_.size() * sizeof(TokenWME);
    }

private:
    void init_root() {
         root_ = allocate_raw();
         root_->parent = nullptr; // Root has no parent
         root_->fact = nullptr;
         root_->depth = 0;
         root_->hash = 0;
    }

    TokenWME* allocate_raw() {
        auto token = std::make_unique<TokenWME>();
        TokenWME* raw = token.get();
        tokens_.push_back(std::move(token));
        return raw;
    }

    size_t reserved_size_ = 0;
    std::vector<std::unique_ptr<TokenWME>> tokens_;
    TokenWME* root_;
};

} // namespace rulesforge

#endif // TOKEN_ARENA_HPP
