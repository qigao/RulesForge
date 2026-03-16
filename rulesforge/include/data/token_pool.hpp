#ifndef TOKEN_POOL_HPP
#define TOKEN_POOL_HPP

#include "core/token.hpp"
#include <object_pool.h>
#include <functional>
#include <stdexcept>

namespace rulesforge {

/**
 * @brief Object pool for TokenWME allocation
 *
 * Replaces TokenArena for long-running rule engines.
 * Supports individual token deallocation to prevent memory leaks.
 *
 * PERFORMANCE:
 * - Allocation: 50-100M ops/s (vs 100M+ for bump allocator)
 * - Trade-off: 2x slower but prevents memory leaks in RETRACT scenarios
 */
class TokenPool {
public:
    explicit TokenPool(size_t initial_capacity = 1024) {
        object_pool_config_t config = {
            .object_size = sizeof(TokenWME),
            .initial_capacity = initial_capacity,
            .max_capacity = 0,  // unlimited
            .zero_on_alloc = false
        };

        token_pool_ = object_pool_create(&config);
        if (!token_pool_) {
            throw std::runtime_error("Failed to create token pool");
        }

        init_root();
    }

    ~TokenPool() {
        object_pool_destroy(token_pool_);
    }

    // Non-copyable
    TokenPool(TokenPool const&) = delete;
    TokenPool& operator=(TokenPool const&) = delete;

    /**
     * @brief Create a new token
     * @param parent Parent token (nullptr = use root)
     * @param fact Associated fact
     * @return Allocated token
     */
    TokenWME* create_token(TokenWME const* parent, ::Fact const* fact) {
        TokenWME* token = allocate_raw();
        token->parent = parent ? parent : root_;
        token->fact = fact;
        token->depth = token->parent->depth + 1;

        // Compute hash
        size_t h = token->parent->hash;
        size_t fact_h = std::hash<const void*>{}(fact);
        h ^= fact_h + 0x9e3779b9 + (h << 6) + (h >> 2);
        token->hash = h;

        return token;
    }

    /**
     * @brief Destroy a token and return it to the pool
     * @param token Token to destroy (must not be root)
     */
    void destroy_token(TokenWME* token) {
        if (token && token != root_) {
            object_pool_free(token_pool_, token);
        }
    }

    TokenWME const* get_root() const { return root_; }

    /**
     * @brief Get pool statistics
     */
    size_t allocated_count() const {
        return object_pool_allocated_count(token_pool_);
    }

    size_t free_count() const {
        return object_pool_free_count(token_pool_);
    }

    size_t capacity() const {
        return object_pool_capacity(token_pool_);
    }

    size_t peak_usage() const {
        return object_pool_peak_usage(token_pool_);
    }

    void reset_stats() {
        object_pool_reset_stats(token_pool_);
    }

private:
    void init_root() {
        root_ = allocate_raw();
        root_->parent = nullptr;
        root_->fact = nullptr;
        root_->depth = 0;
        root_->hash = 0;
    }

    TokenWME* allocate_raw() {
        void* p = object_pool_alloc(token_pool_);
        if (!p) {
            throw std::bad_alloc();
        }
        // No need for placement new - TokenWME is POD-like
        return static_cast<TokenWME*>(p);
    }

    object_pool_t* token_pool_;
    TokenWME* root_;
};

/**
 * @brief Object pool for Activation allocation
 *
 * For large agendas, pooling Activation objects avoids vector reallocation overhead.
 */
class ActivationPool {
public:
    explicit ActivationPool(size_t initial_capacity = 512) {
        object_pool_config_t config = {
            .object_size = sizeof(Activation),
            .initial_capacity = initial_capacity,
            .max_capacity = 0,
            .zero_on_alloc = false
        };

        activation_pool_ = object_pool_create(&config);
        if (!activation_pool_) {
            throw std::runtime_error("Failed to create activation pool");
        }
    }

    ~ActivationPool() {
        object_pool_destroy(activation_pool_);
    }

    // Non-copyable
    ActivationPool(ActivationPool const&) = delete;
    ActivationPool& operator=(ActivationPool const&) = delete;

    /**
     * @brief Allocate an activation
     */
    Activation* allocate() {
        void* p = object_pool_alloc(activation_pool_);
        if (!p) {
            throw std::bad_alloc();
        }
        return static_cast<Activation*>(p);
    }

    /**
     * @brief Create an activation with initialization
     */
    Activation* create(ParsedRule const* rule, Token const& token,
                      size_t hash_value, std::map<std::string, int> const* bindings) {
        Activation* act = allocate();
        act->rule = rule;
        act->token = token;
        act->hash_value = hash_value;
        act->bindings = bindings;
        return act;
    }

    /**
     * @brief Destroy an activation and return it to the pool
     */
    void destroy(Activation* activation) {
        if (activation) {
            object_pool_free(activation_pool_, activation);
        }
    }

    size_t allocated_count() const {
        return object_pool_allocated_count(activation_pool_);
    }

    size_t free_count() const {
        return object_pool_free_count(activation_pool_);
    }

    size_t capacity() const {
        return object_pool_capacity(activation_pool_);
    }

    size_t peak_usage() const {
        return object_pool_peak_usage(activation_pool_);
    }

private:
    object_pool_t* activation_pool_;
};

} // namespace rulesforge

#endif // TOKEN_POOL_HPP
