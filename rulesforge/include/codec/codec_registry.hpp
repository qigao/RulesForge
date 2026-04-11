#ifndef CODEC_REGISTRY_HPP
#define CODEC_REGISTRY_HPP

#include "codec/fact_value_adapter.hpp"
#include "data/fact_arena.hpp"
#include "core/constraint_types.hpp"

 
#include "data_bind.h"
 

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
    void load_declarations(std::vector<ParsedDeclaration> const& declarations,
                           std::vector<ParsedEnum> const& enums);

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
    DataBind* binary_codec_ = nullptr;
    DataBind* json_codec_ = nullptr;
    DataBind* csv_codec_ = nullptr;
    std::vector<ParsedDeclaration> declarations_;
    std::vector<ParsedEnum> enums_;
    std::string last_error_;

    void reset_codecs();
    DataBind* get_or_create_binary_codec();
    DataBind* get_or_create_json_codec();
    DataBind* get_or_create_csv_codec();
};

} // namespace rulesforge

#endif // CODEC_REGISTRY_HPP
