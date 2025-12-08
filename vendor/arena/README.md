# TurboMemory Module

Unified memory management for TurboNet, providing both simple and advanced allocation strategies.

## Components

### 1. memory_pool.h - Simple Bump Allocator
**Purpose:** Fast, temporary allocations for request/message parsing

**Features:**
- Simple bump-pointer allocation
- Mark/rewind support (stack-style allocation)
- Statistics tracking (peak usage, allocation count)
- Single contiguous buffer
- ~80 lines of code

**Use Cases:**
- HTTP request parsing
- Temporary string buffers
- Frame parsing in parser module
- Any short-lived allocations

**Example:**
```c
#include "memory_pool.h"

MemoryPool *pool = pool_create(4096);
void *data = pool_alloc(pool, 256);

// Mark and rewind
size_t mark = pool_mark(pool);
void *temp = pool_alloc(pool, 128);
pool_rewind(pool, mark);  // Free temp

// Reset for reuse
pool_reset(pool);
pool_destroy(pool);
```

### 2. arena_buffer.h - Zero-Copy Buffer Management
**Purpose:** High-performance zero-copy message forwarding and buffer sharing

**Features:**
- Multi-region arena with auto-grow
- Reference-counted buffers
- Buffer pooling/recycling (eliminates malloc/free)
- External buffer wrapping (zero-copy)
- Buffer slicing
- ~540 lines of code

**Use Cases:**
- MQTT publish message forwarding
- TurboMQ pub/sub message sharing
- NetCore zero-copy sends (KCP/TLS/PIPE)
- WebSocket broadcasting
- Any scenario requiring buffer sharing without copying

**Example:**
```c
#include "arena_buffer.h"

// Initialize arena
turbo_arena_t arena;
turbo_arena_init(&arena, 1024 * 1024);

// Get pooled buffer (reused from previous messages)
turbo_arena_buffer_t *buf = turbo_arena_get_pooled_buffer(&arena, 4096);

// Share buffer with multiple subscribers (no copying!)
for (int i = 0; i < sub_count; i++) {
    turbo_arena_buffer_ref(buf);  // Increment refcount
    send_to_subscriber(subs[i], buf);
    turbo_arena_buffer_unref(buf);  // Decrement when done
}

// Return to pool for reuse
turbo_arena_return_buffer(buf);

// Cleanup
turbo_arena_free(&arena);
```

**Zero-Copy External Wrapping:**
```c
// Wrap user data without copying
turbo_arena_buffer_t *buf = turbo_arena_wrap_external(
    user_data, data_len,
    free_callback,  // Optional: called when refcount = 0
    user_data_ptr
);

// Send without copying
turbo_kcp_send_buffer(client, buf, data_len);
turbo_arena_buffer_unref(buf);  // Calls free_callback when done
```

## Architecture

```
memory/
├── include/
│   ├── memory_pool.h      # Simple bump allocator
│   └── arena_buffer.h     # Zero-copy buffer management
├── src/
│   ├── memory_pool.c
│   └── arena_buffer.c
├── tests/
│   ├── test_memory_pool.c
│   └── test_arena_buffer.c
└── CMakeLists.txt
```

## Module Dependencies

### memory_pool.h users:
- **parser** - Frame parsing, temporary buffers
- **iris** (can migrate) - HTTP request parsing
- **http** (can migrate) - Client request building
- **websocket** (can migrate) - Message handling

### arena_buffer.h users:
- **netcore** - Zero-copy sends, async clients/servers
- **mqtt** - PUBLISH packet pooling, session management
- **turbomq** - Pub/sub message sharing, command handling
- **websocket** - Broadcasting
- **iris** - Request/response arenas

## Migration from Old Locations

**Before:**
```
parser/include/memory_pool.h  → parser-specific
netcore/include/arena_buffer.h → netcore-specific
```

**After:**
```
memory/include/memory_pool.h   → Shared simple pool
memory/include/arena_buffer.h  → Shared zero-copy arena
```

## Performance Characteristics

### memory_pool.h
- ✅ **Extremely fast:** Bump pointer = single addition
- ✅ **Zero fragmentation:** Contiguous allocation
- ✅ **Predictable:** Fixed buffer size
- ❌ **No individual free:** Reset entire pool
- ❌ **Limited size:** Single buffer

### arena_buffer.h
- ✅ **Fast pooling:** Reuses buffers, eliminates malloc/free
- ✅ **Zero-copy:** Reference counting + external wrapping
- ✅ **Auto-grow:** Adds regions as needed
- ✅ **Flexible:** Individual buffer lifecycle
- ⚠️ **Overhead:** Reference counting, buffer headers

## When to Use Which?

| Scenario | Use | Reason |
|----------|-----|--------|
| HTTP request parsing | memory_pool | Temporary, short-lived |
| MQTT PUBLISH forwarding | arena_buffer | Zero-copy sharing |
| Frame parsing | memory_pool | Simple, fast |
| WebSocket broadcast | arena_buffer | Message shared across clients |
| Temp string buffers | memory_pool | Stack-like lifecycle |
| KCP/TLS sends | arena_buffer | External wrapping |

## Build Integration

```cmake
# Link against memory module
target_link_libraries(your_target PRIVATE
    TurboNet::Memory
)

# Includes both headers automatically
#include "memory_pool.h"
#include "arena_buffer.h"
```

## See Also

- MEMORY_POOL_ANALYSIS.md - Detailed feature usage analysis
- RENAME_SUMMARY.md - Migration history
