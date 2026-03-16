#ifndef CODEC_REGISTRY_HPP
#define CODEC_REGISTRY_HPP

#include "codec/fact_value_adapter.hpp"
#include "codec/json_fact_parser.hpp"
#include "codec/csv_fact_parser.hpp"
#include "data/fact_arena.hpp"

extern "C" {
#include "data_bind.h"
}

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace rulesforge {

/**
 * @brief Registry that manages DataBind codecs for RFL types
 *
 * Auto-generates .schema from .rfl declares, caches data_bind instances
 */
class CodecRegistry {
public:
    CodecRegistry();
    ~CodecRegistry();

    /**
     * @brief Load declarations and generate schema
     */
    void load_declarations(std::vector<ParsedDeclaration> const& declarations);

    /**
     * @brief Get codec for a type (lazy initialization)
     */
    DataBind* get_codec(std::string const& type_name);

    /**
     * @brief Load codec from DLL/SO plugin
     * @param type_name Message type name (e.g., "Order")
     * @param dll_path Path to DLL/SO file
     * @return true on success, false on failure
     */
    bool load_codec_from_dll(std::string const& type_name, std::string const& dll_path);

    /**
     * @brief Parse binary data to Fact (uses provided arena)
     */
    Fact* parse_binary(rulesforge::FactArena& arena, std::string const& type_name, uint8_t const* buf, size_t len);

    /**
     * @brief Parse JSON string to Fact (uses provided arena)
     */
    Fact* parse_json(rulesforge::FactArena& arena, std::string const& type_name, std::string const& json_str);

    /**
     * @brief Parse CSV string to Facts (uses provided arena)
     */
    std::vector<Fact*> parse_csv(rulesforge::FactArena& arena, std::string const& type_name, std::string const& csv_str);

    /**
     * @brief Get last error message
     */
    std::string get_last_error() const { return last_error_; }

private:
    std::unique_ptr<FactValueAdapter> adapter_;
    rulesforge::FactArena temp_arena_;  // Temporary arena for adapter
    std::unordered_map<std::string, DataBind*> codecs_;
    DataBind* json_codec_ = nullptr;  // Shared JSON codec for all types
    std::vector<ParsedDeclaration> declarations_;
    std::string last_error_;

    // DLL plugin management
    struct DllHandle {
        void* handle;
        DataBind* codec;
    };
    std::unordered_map<std::string, std::unique_ptr<DllHandle>> dll_codecs_;

    DataBind* get_or_create_json_codec();
};

} // namespace rulesforge

#endif // CODEC_REGISTRY_HPP
