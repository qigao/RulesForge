#ifndef TOKEN_ARENA_HPP
#define TOKEN_ARENA_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

struct Fact;

/**
 * Arena-based memory management for TokenWME.
 * Eliminates shared_ptr overhead and improves cache locality.
 * 
 * Design principles (Linus-style):
 * 1. No special cases - all tokens in the arena, including root
 * 2. Simple pointer arithmetic - no complex memory management
 * 3. Batch allocation - reduce allocation overhead
 */
class TokenArena {
public:
    struct TokenWME {
        TokenWME* parent;           // Raw pointer, arena owns lifetime
        Fact const* fact;           // Raw pointer to fact
        uint32_t depth;            // Depth in the Rete network
        uint32_t hash;             // Pre-computed hash
        
        // Root token points to itself
        bool is_root() const { return parent == this; }
        
        bool operator==(TokenWME const& other) const {
            if (hash != other.hash) return false;
            if (depth != other.depth) return false;
            if (fact != other.fact) return false;
            
            // Compare parent chain
            TokenWME const* p1 = parent;
            TokenWME const* p2 = other.parent;
            while (p1 && p2 && !p1->is_root() && !p2->is_root()) {
                if (p1->fact != p2->fact) return false;
                p1 = p1->parent;
                p2 = p2->parent;
            }
            return p1 == p2;
        }
    };
    
    static constexpr size_t BLOCK_SIZE = 4096;  // Tokens per block
    
    TokenArena() {
        allocate_block();
        // Create root token that points to itself
        TokenWME* root = allocate();
        root->parent = root;
        root->fact = nullptr;
        root->depth = 0;
        root->hash = 0;
    }
    
    ~TokenArena() {
        for (auto& block : blocks_) {
            delete[] block;
        }
    }
    
    // Allocate a new token
    TokenWME* allocate() {
        if (next_free_ >= BLOCK_SIZE) {
            allocate_block();
        }
        return &blocks_.back()[next_free_++];
    }
    
    // Create a new token with parent
    TokenWME* create_token(TokenWME* parent, Fact const* fact) {
        TokenWME* token = allocate();
        token->parent = parent ? parent : get_root();
        token->fact = fact;
        token->depth = parent ? parent->depth + 1 : 0;
        
        // Compute hash combining parent hash and fact pointer
        token->hash = parent ? parent->hash : 0;
        token->hash ^= std::hash<const void*>{}(fact) + 0x9e3779b9 + (token->hash << 6) + (token->hash >> 2);
        
        return token;
    }
    
    TokenWME* get_root() {
        return &blocks_[0][0];
    }
    
    // Statistics for monitoring
    size_t allocated_count() const {
        return (blocks_.size() - 1) * BLOCK_SIZE + next_free_;
    }
    
    size_t memory_usage() const {
        return blocks_.size() * BLOCK_SIZE * sizeof(TokenWME);
    }
    
private:
    void allocate_block() {
        blocks_.push_back(new TokenWME[BLOCK_SIZE]);
        next_free_ = 0;
    }
    
    std::vector<TokenWME*> blocks_;
    size_t next_free_ = 0;
};

// Hasher for arena tokens
struct ArenaTokenHasher {
    size_t operator()(TokenArena::TokenWME const* token) const {
        return token ? token->hash : 0;
    }
};

// Equality for arena tokens
struct ArenaTokenEquals {
    bool operator()(TokenArena::TokenWME const* a, TokenArena::TokenWME const* b) const {
        if (!a || !b) return a == b;
        return *a == *b;
    }
};

#endif // TOKEN_ARENA_HPP