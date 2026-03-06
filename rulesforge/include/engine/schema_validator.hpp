#ifndef SCHEMA_VALIDATOR_HPP
#define SCHEMA_VALIDATOR_HPP

#include "core/constraint_types.hpp"
#include "core/fact.hpp"
#include "core/rfl_parser_state.hpp"

#include <stdexcept>
#include <string>
#include <vector>

/**
 * @brief PROD-002: Validation mode for fact insertion
 */
enum class ValidationMode {
    None,   // No validation (default, fastest)
    Warn,   // Log warnings but allow insertion
    Strict  // Throw exception on validation failure
};

/**
 * @brief PROD-002: Structured validation error
 */
struct ValidationError {
    std::string fact_type;
    std::string field_name;
    std::string message;

    std::string to_string() const {
        if (field_name.empty()) {
            return "Type '" + fact_type + "': " + message;
        }
        return "Type '" + fact_type + "', field '" + field_name + "': " + message;
    }
};

/**
 * @brief PROD-002: Exception thrown when schema validation fails in Strict mode
 */
class SchemaValidationException : public std::runtime_error {
public:
    explicit SchemaValidationException(std::vector<ValidationError> errors)
        : std::runtime_error(build_message(errors)), errors_(std::move(errors)) {}

    std::vector<ValidationError> const& get_errors() const noexcept { return errors_; }

private:
    static std::string build_message(std::vector<ValidationError> const& errors) {
        std::string msg = "Schema validation failed with " + std::to_string(errors.size()) + " error(s):";
        for (auto const& e : errors) {
            msg += "\n  - " + e.to_string();
        }
        return msg;
    }

    std::vector<ValidationError> errors_;
};

/**
 * @brief PROD-002: Schema validator for fact insertion
 *
 * Validates facts against declared type schemas before insertion.
 * Uses FieldType enum for O(1) type matching.
 */
class SchemaValidator {
public:
    explicit SchemaValidator(std::vector<ParsedDeclaration> const& declarations) {
        for (auto const& decl : declarations) {
            declarations_[decl.type_name] = &decl;
            if (!decl.source_package.empty()) {
                declarations_[decl.source_package + "." + decl.type_name] = &decl;
            }
        }
    }

    /**
     * @brief Validate a fact against its declared schema
     * @param fact The fact to validate
     * @return Vector of validation errors (empty if valid)
     */
    std::vector<ValidationError> validate(Fact const& fact) const {
        std::vector<ValidationError> errors;

        ParsedDeclaration const* decl = find_declaration(fact.type);
        if (!decl) {
            errors.push_back({fact.type, "", "Unknown fact type - no declaration found"});
            return errors;
        }

        for (auto const& field : decl->fields) {
            auto it = fact.fields.find(field.name);
            if (it == fact.fields.end()) {
                continue;  // Missing fields allowed
            }

            if (!validate_field_type(it->second, field.type)) {
                errors.push_back({
                    fact.type,
                    field.name,
                    "Type mismatch - expected '" + field_type_name(field.type) +
                    "', got '" + get_value_type_name(it->second) + "'"
                });
            }
        }

        return errors;
    }

    bool has_declaration(std::string const& type_name) const {
        return find_declaration(type_name) != nullptr;
    }

private:
    ParsedDeclaration const* find_declaration(std::string const& type_name) const {
        auto it = declarations_.find(type_name);
        if (it != declarations_.end()) {
            return it->second;
        }

        size_t dot_pos = type_name.rfind('.');
        if (dot_pos != std::string::npos) {
            std::string short_name = type_name.substr(dot_pos + 1);
            it = declarations_.find(short_name);
            if (it != declarations_.end()) {
                return it->second;
            }
        }

        return nullptr;
    }

    /**
     * @brief O(1) type validation using single bitwise AND
     */
    static bool validate_field_type(ConstraintValue const& value, FieldType expected) {
        return std::visit([expected](auto const& v) -> bool {
            using T = std::decay_t<decltype(v)>;

            if constexpr (std::is_same_v<T, std::string>) {
                return (FT_STRING_COMPAT & expected) != 0;
            } else if constexpr (std::is_same_v<T, int64_t>) {
                return (FT_INT_COMPAT & expected) != 0;
            } else if constexpr (std::is_same_v<T, double>) {
                return (FT_DOUBLE_COMPAT & expected) != 0;
            } else if constexpr (std::is_same_v<T, NilValue>) {
                return true;  // Nil is valid for any type
            } else if constexpr (std::is_same_v<T, FactList>) {
                return (FT_LIST_COMPAT & expected) != 0;
            } else if constexpr (std::is_same_v<T, std::shared_ptr<TypedList>>) {
                return (FT_LIST_COMPAT & expected) != 0;
            } else if constexpr (std::is_same_v<T, std::shared_ptr<ValueSet>>) {
                return (FT_SET_COMPAT & expected) != 0;
            } else if constexpr (std::is_same_v<T, std::shared_ptr<ValueMap>>) {
                return (FT_MAP_COMPAT & expected) != 0;
            }
            return false;
        }, value);
    }

    static std::string get_value_type_name(ConstraintValue const& value) {
        return std::visit([](auto const& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::string>) return "String";
            else if constexpr (std::is_same_v<T, int64_t>) return "int";
            else if constexpr (std::is_same_v<T, double>) return "double";
            else if constexpr (std::is_same_v<T, NilValue>) return "nil";
            else if constexpr (std::is_same_v<T, FactList>) return "List";
            else if constexpr (std::is_same_v<T, std::shared_ptr<TypedList>>) return "List";
            else if constexpr (std::is_same_v<T, std::shared_ptr<ValueSet>>) return "Set";
            else if constexpr (std::is_same_v<T, std::shared_ptr<ValueMap>>) return "Map";
            return "unknown";
        }, value);
    }

    static std::string field_type_name(FieldType t) {
        switch (t) {
            case FT_String: return "String";
            case FT_Int: return "int";
            case FT_Long: return "long";
            case FT_Double: return "double";
            case FT_Float: return "float";
            case FT_Number: return "Number";
            case FT_Boolean: return "boolean";
            case FT_List: return "List";
            case FT_Set: return "Set";
            case FT_Map: return "Map";
            case FT_Object: return "Object";
            default: return "unknown";
        }
    }

    std::map<std::string, ParsedDeclaration const*> declarations_;
};

#endif // SCHEMA_VALIDATOR_HPP
