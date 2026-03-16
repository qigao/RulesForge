#include "codec/json_fact_parser.hpp"
#include <sstream>
#include <cctype>

namespace rulesforge {

thread_local std::string JsonFactParser::last_error_;

ParsedDeclaration const* JsonFactParser::find_declaration(
    std::string const& type_name,
    std::vector<ParsedDeclaration> const& declarations
) {
    for (auto const& decl : declarations) {
        if (decl.type_name == type_name) {
            return &decl;
        }
    }
    return nullptr;
}

// Minimal JSON parser - handles {"key":"value","key2":123}
static std::string parse_string_value(std::string const& json, size_t& pos) {
    if (json[pos] != '"') return "";
    pos++; // skip opening "

    std::string result;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            pos++; // skip escape
        }
        result += json[pos++];
    }
    pos++; // skip closing "
    return result;
}

static std::string parse_number_value(std::string const& json, size_t& pos) {
    std::string result;
    while (pos < json.size() && (std::isdigit(json[pos]) || json[pos] == '.' || json[pos] == '-')) {
        result += json[pos++];
    }
    return result;
}

static void skip_whitespace(std::string const& json, size_t& pos) {
    while (pos < json.size() && std::isspace(json[pos])) pos++;
}

Fact* JsonFactParser::parse(
    rulesforge::FactArena& arena,
    std::string const& type_name,
    std::string const& json_str,
    std::vector<ParsedDeclaration> const& declarations
) {
    last_error_.clear();

    auto const* decl = find_declaration(type_name, declarations);
    if (!decl) {
        last_error_ = "Type not found: " + type_name;
        return nullptr;
    }

    Fact* fact = arena.create_fact();
    fact->type = type_name;

    size_t pos = 0;
    skip_whitespace(json_str, pos);

    if (pos >= json_str.size() || json_str[pos] != '{') {
        last_error_ = "Expected '{'";
        return nullptr;
    }
    pos++; // skip {

    while (pos < json_str.size()) {
        skip_whitespace(json_str, pos);
        if (json_str[pos] == '}') break;

        // Parse key
        if (json_str[pos] != '"') {
            last_error_ = "Expected field name";
            return nullptr;
        }
        std::string key = parse_string_value(json_str, pos);

        skip_whitespace(json_str, pos);
        if (json_str[pos] != ':') {
            last_error_ = "Expected ':'";
            return nullptr;
        }
        pos++; // skip :

        skip_whitespace(json_str, pos);

        // Find field type
        ParsedField const* field = nullptr;
        for (auto const& f : decl->fields) {
            if (f.name == key) {
                field = &f;
                break;
            }
        }

        if (!field) {
            last_error_ = "Unknown field: " + key;
            return nullptr;
        }

        // Parse value based on type
        if (json_str[pos] == '"') {
            std::string value = parse_string_value(json_str, pos);
            fact->fields[key] = value;
        } else if (std::isdigit(json_str[pos]) || json_str[pos] == '-') {
            std::string num_str = parse_number_value(json_str, pos);
            if (field->type & (FT_Int | FT_Long)) {
                fact->fields[key] = static_cast<int64_t>(std::stoll(num_str));
            } else {
                fact->fields[key] = std::stod(num_str);
            }
        } else if (json_str.substr(pos, 4) == "true") {
            fact->fields[key] = static_cast<int64_t>(1);
            pos += 4;
        } else if (json_str.substr(pos, 5) == "false") {
            fact->fields[key] = static_cast<int64_t>(0);
            pos += 5;
        } else {
            last_error_ = "Unsupported value type";
            return nullptr;
        }

        skip_whitespace(json_str, pos);
        if (json_str[pos] == ',') pos++;
    }

    return fact;
}

} // namespace rulesforge
