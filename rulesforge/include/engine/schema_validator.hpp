#ifndef SCHEMA_VALIDATOR_HPP
#define SCHEMA_VALIDATOR_HPP

#include "core/constraint_types.hpp"
#include "core/fact.hpp"
#include "core/rfl_parser_state.hpp"

#include <stdexcept>
#include <map>
#include <string>
#include <vector>
#include <optional>

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
            short_name_declarations_[short_type_name(decl.type_name)].push_back(&decl);
        }
    }

    /**
     * @brief Validate a fact against its declared schema
     * @param fact The fact to validate
     * @return Vector of validation errors (empty if valid)
     */
    std::vector<ValidationError> validate(Fact const& fact) const {
        std::vector<ValidationError> errors;

        auto resolution = resolve_declaration(fact.type);
        if (resolution.is_ambiguous()) {
            errors.push_back({fact.type, "", build_ambiguous_type_message(fact.type, resolution.ambiguous_matches)});
            return errors;
        }

        ParsedDeclaration const* decl = resolution.declaration;
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
        auto resolution = resolve_declaration(type_name);
        return resolution.declaration != nullptr && !resolution.is_ambiguous();
    }

    std::optional<FieldType> get_field_type(std::string const& type_name,
                                            std::string_view field_name) const {
        auto resolution = resolve_declaration(type_name);
        if (resolution.is_ambiguous() || !resolution.declaration) {
            return std::nullopt;
        }

        for (auto const& field : resolution.declaration->fields) {
            if (field.name == field_name) {
                return field.type;
            }
        }
        return std::nullopt;
    }

    std::string canonicalize_type_name(std::string const& type_name) const {
        auto resolution = resolve_declaration(type_name);
        if (resolution.is_ambiguous()) {
            throw std::runtime_error(build_ambiguous_type_message(type_name, resolution.ambiguous_matches));
        }
        if (resolution.declaration) {
            return resolution.declaration->type_name;
        }
        return type_name;
    }

private:
    struct DeclarationResolution {
        ParsedDeclaration const* declaration = nullptr;
        std::vector<ParsedDeclaration const*> ambiguous_matches;

        bool is_ambiguous() const { return !ambiguous_matches.empty(); }
    };

    ParsedDeclaration const* find_declaration(std::string const& type_name) const {
        auto resolution = resolve_declaration(type_name);
        return resolution.is_ambiguous() ? nullptr : resolution.declaration;
    }

    DeclarationResolution resolve_declaration(std::string const& type_name) const {
        auto it = declarations_.find(type_name);
        if (it != declarations_.end()) {
            return {it->second, {}};
        }

        if (type_name.find('.') != std::string::npos) {
            return {};
        }

        auto short_it = short_name_declarations_.find(type_name);
        if (short_it == short_name_declarations_.end()) {
            return {};
        }
        if (short_it->second.size() == 1) {
            return {short_it->second.front(), {}};
        }
        return {nullptr, short_it->second};
    }

    static std::string short_type_name(std::string const& type_name) {
        size_t dot_pos = type_name.rfind('.');
        if (dot_pos == std::string::npos) {
            return type_name;
        }
        return type_name.substr(dot_pos + 1);
    }

    static std::string build_ambiguous_type_message(
        std::string const& type_name,
        std::vector<ParsedDeclaration const*> const& matches)
    {
        std::string msg = "Ambiguous fact type - matches ";
        for (size_t i = 0; i < matches.size(); ++i) {
            if (i > 0) {
                msg += ", ";
            }
            msg += "'";
            msg += matches[i]->type_name;
            msg += "'";
        }
        if (!type_name.empty()) {
            msg += " for input '";
            msg += type_name;
            msg += "'";
        }
        return msg;
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
            } else if constexpr (std::is_same_v<T, turbo_uuid_t>) {
                return (FT_UUID_COMPAT & expected) != 0;
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
            } else {
                return false;
            }
        }, value);
    }

    static std::string get_value_type_name(ConstraintValue const& value) {
        return std::visit([](auto const& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::string>) return "String";
            else if constexpr (std::is_same_v<T, int64_t>) return "int";
            else if constexpr (std::is_same_v<T, double>) return "double";
            else if constexpr (std::is_same_v<T, turbo_uuid_t>) return "uuid";
            else if constexpr (std::is_same_v<T, NilValue>) return "nil";
            else if constexpr (std::is_same_v<T, FactList>) return "List";
            else if constexpr (std::is_same_v<T, std::shared_ptr<TypedList>>) return "List";
            else if constexpr (std::is_same_v<T, std::shared_ptr<ValueSet>>) return "Set";
            else if constexpr (std::is_same_v<T, std::shared_ptr<ValueMap>>) return "Map";
            else return "unknown";
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
            case FT_Uuid: return "uuid";
            case FT_Object: return "Object";
            default: return "unknown";
        }
    }

    std::map<std::string, ParsedDeclaration const*> declarations_;
    std::map<std::string, std::vector<ParsedDeclaration const*>> short_name_declarations_;
};

#endif // SCHEMA_VALIDATOR_HPP
