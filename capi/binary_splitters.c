/**
 * @file binary_splitters.c
 * @brief Built-in message splitters for binary codec
 */

#include "binary_codec.h"
#include <string.h>

/* ========================================================================= */
/* Length-Prefixed Splitter                                                  */
/* ========================================================================= */

bool binary_splitter_length_prefixed(void* state, const uint8_t* buf, size_t len, size_t* msg_start,
                                      size_t* msg_len) {
    (void)state;
    if (!buf || !msg_start || !msg_len || len < 4) {
        return false;
    }

    /* Read 4-byte little-endian length */
    uint32_t msg_size = buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);

    if (len < 4 + msg_size) {
        return false; /* Incomplete message */
    }

    *msg_start = 4;
    *msg_len = msg_size;
    return true;
}

/* ========================================================================= */
/* Fixed-Size Splitter                                                       */
/* ========================================================================= */

bool binary_splitter_fixed_size(void* state, const uint8_t* buf, size_t len, size_t* msg_start,
                                 size_t* msg_len) {
    BinaryFixedSizeSplitterState* st = (BinaryFixedSizeSplitterState*)state;
    if (!buf || !msg_start || !msg_len || !st) {
        return false;
    }

    if (st->current_offset >= len) {
        return false; /* No more messages */
    }

    size_t remaining = len - st->current_offset;
    if (remaining < st->message_size) {
        return false; /* Incomplete message */
    }

    *msg_start = st->current_offset;
    *msg_len = st->message_size;
    st->current_offset += st->message_size;
    return true;
}

void binary_splitter_fixed_size_reset(void* state) {
    BinaryFixedSizeSplitterState* st = (BinaryFixedSizeSplitterState*)state;
    if (st) {
        st->current_offset = 0;
    }
}
