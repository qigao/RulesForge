#ifndef JS_HANDLE_MANAGER_HPP
#define JS_HANDLE_MANAGER_HPP

#include <atomic>
#include <mutex>
#include "phmap.h"

// Forward declaration
class JSScriptingManager;

/**
 * Thread-safe handle manager for JS-C++ communication.
 * Replaces raw pointer passing with opaque handle IDs.
 */
class JSHandleManager {
public:
    using HandleId = uint32_t;
    
    static JSHandleManager& instance() {
        static JSHandleManager instance;
        return instance;
    }
    
    // Register a manager and get a handle
    HandleId register_manager(JSScriptingManager* mgr) {
        std::lock_guard<std::mutex> lock(mutex_);
        HandleId id = next_id_.fetch_add(1);
        handles_[id] = mgr;
        return id;
    }
    
    // Get manager by handle
    JSScriptingManager* get_manager(HandleId handle) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = handles_.find(handle);
        return (it != handles_.end()) ? it->second : nullptr;
    }
    
    // Unregister a handle
    void unregister(HandleId handle) {
        std::lock_guard<std::mutex> lock(mutex_);
        handles_.erase(handle);
    }
    
private:
    JSHandleManager() = default;
    ~JSHandleManager() = default;
    
    JSHandleManager(JSHandleManager const&) = delete;
    JSHandleManager& operator=(JSHandleManager const&) = delete;
    
    std::mutex mutex_;
    std::atomic<HandleId> next_id_{1};  // Start at 1, 0 is invalid
    unordered_map<HandleId, JSScriptingManager*> handles_;
};

#endif // JS_HANDLE_MANAGER_HPP

