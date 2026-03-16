#ifndef RFL_TO_SCHEMA_HPP
#define RFL_TO_SCHEMA_HPP

#include "core/constraint_types.hpp"
#include <string>
#include <vector>

/**
 * @brief Convert RFL declare blocks to TBE schema format
 *
 * Input:  declare Person { name: String, age: int }
 * Output: message Person { string name; int32 age; }
 */
class RflToSchemaConverter {
public:
    /**
     * @brief Convert parsed declarations to schema string
     */
    static std::string convert(std::vector<ParsedDeclaration> const& declarations);

    /**
     * @brief Convert single declaration to schema message
     */
    static std::string convert_declaration(ParsedDeclaration const& decl);

private:
    static std::string map_field_type(FieldType type);
};

#endif // RFL_TO_SCHEMA_HPP
