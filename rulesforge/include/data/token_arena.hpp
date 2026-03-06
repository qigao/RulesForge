#ifndef TOKEN_ARENA_HPP
#define TOKEN_ARENA_HPP

#include "core/token.hpp"
#include <turbo_buffer.h>
#include <new>
#include <functional>

namespace rulesforge {

// Arena dedicated to TokenWMEs.
// Uses turbo_pool_t for high-performance, contiguous allocation.
class TokenArena {
public:
    explicit TokenArena(size_t size = 64 * 1024 * 1024) {
         turbo_pool_init(&arena_, size);
         init_root();
    }

    ~TokenArena() {
        turbo_pool_free(&arena_);
    }

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
        turbo_pool_reset(&arena_);
        init_root();
    }

    size_t memory_usage() const {
        return arena_.total_used;
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
        void* p = turbo_pool_alloc(&arena_, sizeof(TokenWME));
        if (!p) throw std::bad_alloc();
        return new (p) TokenWME();
    }

    turbo_pool_t arena_;
    TokenWME* root_;
};

} // namespace rulesforge

#endif // TOKEN_ARENA_HPP
