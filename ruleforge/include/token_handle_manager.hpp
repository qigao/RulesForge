#ifndef TOKEN_HANDLE_MANAGER_HPP
#define TOKEN_HANDLE_MANAGER_HPP

#include <atomic>
#include <mutex>
#include "phmap.h"

// Forward declaration
struct Token;

/**
 * @brief Thread-safe handle manager for Token pointers during JS execution.
 *
 * This solves the dangling pointer problem where Token pointers were stored
 * directly in JavaScript global objects. Tokens are only valid during
 * execute_rhs() scope, so we use a handle-based approach:
 *
 * 1. Register token at start of execute_rhs() -> get handle ID
 * 2. Store handle ID (not raw pointer) in JS
 * 3. JS callbacks lookup token via handle
 * 4. Unregister token at end of execute_rhs()
 *
 * If JS tries to use an invalid handle, it gets nullptr instead of
 * accessing freed memory.
 */
class TokenHandleManager {
public:
    using HandleId = uint32_t;
    static constexpr HandleId INVALID_HANDLE = 0;

    static TokenHandleManager& instance() {
        static TokenHandleManager instance;
        return instance;
    }

    /**
     * @brief Register a token and get a handle ID.
     * @param token Pointer to token (must remain valid until unregister)
     * @return Handle ID that can be safely passed to JS
     */
    HandleId register_token(Token* token) {
        if (!token) return INVALID_HANDLE;
        std::lock_guard<std::mutex> lock(mutex_);
        HandleId id = next_id_.fetch_add(1);
        if (id == INVALID_HANDLE) {
            id = next_id_.fetch_add(1);  // Skip 0
        }
        handles_[id] = token;
        return id;
    }

    /**
     * @brief Get token by handle ID.
     * @param handle Handle ID from register_token()
     * @return Token pointer or nullptr if handle is invalid/expired
     */
    Token* get_token(HandleId handle) {
        if (handle == INVALID_HANDLE) return nullptr;
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = handles_.find(handle);
        return (it != handles_.end()) ? it->second : nullptr;
    }

    /**
     * @brief Unregister a handle (call at end of execute_rhs).
     * @param handle Handle to invalidate
     */
    void unregister(HandleId handle) {
        if (handle == INVALID_HANDLE) return;
        std::lock_guard<std::mutex> lock(mutex_);
        handles_.erase(handle);
    }

    /**
     * @brief Check if a handle is currently valid.
     */
    bool is_valid(HandleId handle) {
        if (handle == INVALID_HANDLE) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        return handles_.find(handle) != handles_.end();
    }

private:
    TokenHandleManager() = default;
    ~TokenHandleManager() = default;

    TokenHandleManager(TokenHandleManager const&) = delete;
    TokenHandleManager& operator=(TokenHandleManager const&) = delete;

    std::mutex mutex_;
    std::atomic<HandleId> next_id_{1};  // Start at 1, 0 is invalid
    phmap::flat_hash_map<HandleId, Token*> handles_;
};

/**
 * @brief RAII guard for automatic token handle registration/unregistration.
 *
 * Usage:
 *   void execute_rhs(..., Token& token, ...) {
 *       TokenHandleGuard guard(token);
 *       // Store guard.handle() in JS instead of raw pointer
 *       // ... execute JS ...
 *   } // Automatically unregisters when scope exits
 */
class TokenHandleGuard {
public:
    explicit TokenHandleGuard(Token& token)
        : handle_(TokenHandleManager::instance().register_token(&token)) {}

    ~TokenHandleGuard() {
        TokenHandleManager::instance().unregister(handle_);
    }

    TokenHandleGuard(TokenHandleGuard const&) = delete;
    TokenHandleGuard& operator=(TokenHandleGuard const&) = delete;

    TokenHandleManager::HandleId handle() const { return handle_; }

private:
    TokenHandleManager::HandleId handle_;
};

#endif // TOKEN_HANDLE_MANAGER_HPP


