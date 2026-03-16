#ifndef JSON_FACT_PARSER_HPP
#define JSON_FACT_PARSER_HPP

#include "core/fact.hpp"
#include "core/constraint_types.hpp"
#include "data/fact_arena.hpp"
#include <string>
#include <vector>

namespace rulesforge {

/**
 * @brief Minimal JSON parser for Fact creation
 *
 * Parses simple JSON objects: {"name":"John","age":30}
 * Uses ParsedDeclaration schema for type validation
 */
class JsonFactParser {
public:
    /**
     * @brief Parse JSON string to Fact
     * @param arena Arena for Fact allocation
     * @param type_name Fact type (must exist in declarations)
     * @param json_str JSON string
     * @param declarations Type schemas from RFL
     * @return Fact pointer or nullptr on error
     */
    static Fact* parse(
        rulesforge::FactArena& arena,
        std::string const& type_name,
        std::string const& json_str,
        std::vector<ParsedDeclaration> const& declarations
    );

    static std::string get_last_error() { return last_error_; }

private:
    static thread_local std::string last_error_;

    static ParsedDeclaration const* find_declaration(
        std::string const& type_name,
        std::vector<ParsedDeclaration> const& declarations
    );
};

} // namespace rulesforge

#endif // JSON_FACT_PARSER_HPP
