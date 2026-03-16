/**
 * @file binary_codec.h
 * @brief Binary codec interface for RulesForge integration
 */

#ifndef BINARY_CODEC_H
#define BINARY_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Forward declaration - RulesForge Value type */
typedef struct Value Value;

/**
 * @brief Binary codec vtable for parse/build operations
 */
typedef struct {
    /**
     * @brief Parse binary data to RulesForge Value object
     * @param buf Binary data buffer
     * @param len Buffer length
     * @return Parsed Value object, or NULL on failure
     * @note Caller owns returned Value and must free it
     */
    Value* (*parse)(const uint8_t* buf, size_t len);

    /**
     * @brief Build binary data from RulesForge Value object
     * @param obj Source Value object
     * @param buf Output buffer
     * @param cap Buffer capacity
     * @return Bytes written, or 0 on failure
     */
    size_t (*build)(const Value* obj, uint8_t* buf, size_t cap);
} BinaryCodec;

/**
 * @brief Message splitter for multi-message parsing
 */
typedef struct {
    /**
     * @brief Find next message boundary in buffer
     * @param state Opaque state for splitter
     * @param buf Binary data buffer
     * @param len Buffer length
     * @param[out] msg_start Start offset of message
     * @param[out] msg_len Length of message
     * @return true if message found, false if no more messages
     */
    bool (*next)(void* state, const uint8_t* buf, size_t len, size_t* msg_start, size_t* msg_len);

    /**
     * @brief Reset splitter state
     * @param state Opaque state for splitter
     */
    void (*reset)(void* state);

    /**
     * @brief Opaque state for splitter
     */
    void* state;
} BinaryMessageSplitter;

/**
 * @brief Extended codec with splitter support
 */
typedef struct {
    BinaryCodec codec;
    BinaryMessageSplitter* splitter; /* NULL if single-message only */
} BinaryCodecEx;

/* ========================================================================= */
/* Built-in Splitters                                                        */
/* ========================================================================= */

/**
 * @brief Length-prefixed splitter (4-byte little-endian length header)
 */
bool binary_splitter_length_prefixed(void* state, const uint8_t* buf, size_t len, size_t* msg_start,
                                      size_t* msg_len);

/**
 * @brief Fixed-size message splitter
 */
typedef struct {
    size_t message_size;
    size_t current_offset;
} BinaryFixedSizeSplitterState;

bool binary_splitter_fixed_size(void* state, const uint8_t* buf, size_t len, size_t* msg_start,
                                 size_t* msg_len);
void binary_splitter_fixed_size_reset(void* state);

#endif /* BINARY_CODEC_H */
