#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern "C" {

/**
 * @brief Helper used by JIT-compiled MIR code to extract a variable-length string.
 * This implementation uses explicit little-endian decoding (portable across architectures).
 */
char* tbe_read_varstring(const uint8_t* buf, size_t offset) {
    // Explicit little-endian: LSB first
    uint16_t len = (uint16_t)buf[offset] | ((uint16_t)buf[offset + 1] << 8);
    
    char* s = (char*)malloc(len + 1);
    if (!s) return NULL;
    
    memcpy(s, buf + offset + 2, len);
    s[len] = '\0';
    return s;
}

} // extern "C"
