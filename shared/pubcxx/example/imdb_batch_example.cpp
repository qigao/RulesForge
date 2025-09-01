#include "pubcxx/imdb/imdb.hpp"

#include <cstdint>   // For uint8_t
#include <iostream>
#include <string>
#include <vector>

int main() {
    simple_db::InMemoryDb<std::string, std::vector<uint8_t>> byte_db;

    std::string key = "binary_data";

    // Ensure the key exists with a vector<uint8_t> value first
    byte_db.Put(key, std::vector<uint8_t>{});   // Start with an empty byte vector

    // Simulate having a buffer of bytes
    uint8_t buffer[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    size_t buffer_size = sizeof(buffer);

    // Append the contents of the buffer to the vector value
    std::cout << "Appending buffer..." << std::endl;
    bool appended = byte_db.AppendBatch(key, buffer, buffer + buffer_size);   // Pass pointers as iterators

    if (appended) {
        auto data_opt = byte_db.Get(key);
        if (data_opt) {
            std::cout << key << " after append: ";
            for (uint8_t byte : *data_opt) { std::cout << std::hex << static_cast<int>(byte) << " "; }
            std::cout << std::dec << std::endl;   // Output: 1 2 3 4 5
        }
    }

    // Append another buffer
    uint8_t another_buffer[] = {0xAA, 0xBB};
    size_t another_buffer_size = sizeof(another_buffer);

    std::cout << "Appending another buffer..." << std::endl;
    bool appended2 = byte_db.AppendBatch(key, another_buffer, another_buffer + another_buffer_size);

    if (appended2) {
        auto data_opt = byte_db.Get(key);
        if (data_opt) {
            std::cout << key << " after second append: ";
            for (uint8_t byte : *data_opt) { std::cout << std::hex << static_cast<int>(byte) << " "; }
            std::cout << std::dec << std::endl;   // Output: 1 2 3 4 5 aa bb
        }
    }

    // Example appending from initializer list (which also works)
    std::cout << "Appending from initializer list..." << std::endl;
    bool appended3 = byte_db.AppendBatch(key, {0xEE, 0xFF});
    if (appended3) {
        auto data_opt = byte_db.Get(key);
        if (data_opt) {
            std::cout << key << " after third append: ";
            for (uint8_t byte : *data_opt) { std::cout << std::hex << static_cast<int>(byte) << " "; }
            std::cout << std::dec << std::endl;   // Output: 1 2 3 4 5 aa bb ee ff
        }
    }

    return 0;
}
