#ifndef SCHEMA_VALIDATOR_HPP
#define SCHEMA_VALIDATOR_HPP

#include "rfl_rete_defs.hpp"
#include "rfl_parser_state.hpp"

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
 */
class SchemaValidator {
public:
    explicit SchemaValidator(std::vector<ParsedDeclaration> const& declarations) {
        // Build lookup map for declarations
        for (auto const& decl : declarations) {
            declarations_[decl.type_name] = &decl;
            // Also index by fully qualified name (package.type)
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

        // Find declaration for this fact type
        ParsedDeclaration const* decl = find_declaration(fact.type);
        if (!decl) {
            errors.push_back({fact.type, "", "Unknown fact type - no declaration found"});
            return errors;
        }

        // Check each declared field exists and has correct type
        for (auto const& field : decl->fields) {
            auto it = fact.fields.find(field.name);
            if (it == fact.fields.end()) {
                // Field is missing - check if it's nullable (we allow missing fields)
                // In Rules Forge Language, missing fields are typically allowed
                continue;
            }

            // Validate field type
            if (!validate_field_type(it->second, field.type)) {
                errors.push_back({
                    fact.type,
                    field.name,
                    "Type mismatch - expected '" + field.type + "', got '" + get_value_type_name(it->second) + "'"
                });
            }
        }

        return errors;
    }

    /**
     * @brief Check if a fact type is declared
     */
    bool has_declaration(std::string const& type_name) const {
        return find_declaration(type_name) != nullptr;
    }

private:
    ParsedDeclaration const* find_declaration(std::string const& type_name) const {
        auto it = declarations_.find(type_name);
        if (it != declarations_.end()) {
            return it->second;
        }

        // Try without package prefix
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

    bool validate_field_type(ConstraintValue const& value, std::string const& expected_type) const {
        return std::visit([&expected_type](auto const& v) -> bool {
            using T = std::decay_t<decltype(v)>;

            if constexpr (std::is_same_v<T, std::string>) {
                return expected_type == "String" || expected_type == "string" ||
                       expected_type == "Object" || expected_type == "object";
            } else if constexpr (std::is_same_v<T, int64_t>) {
                return expected_type == "int" || expected_type == "Integer" ||
                       expected_type == "long" || expected_type == "Long" ||
                       expected_type == "Number" || expected_type == "number" ||
                       expected_type == "Object" || expected_type == "object";
            } else if constexpr (std::is_same_v<T, double>) {
                return expected_type == "double" || expected_type == "Double" ||
                       expected_type == "float" || expected_type == "Float" ||
                       expected_type == "Number" || expected_type == "number" ||
                       expected_type == "Object" || expected_type == "object";
            } else if constexpr (std::is_same_v<T, NilValue>) {
                return true;  // Nil is valid for any type
            } else if constexpr (std::is_same_v<T, FactList>) {
                return expected_type == "List" || expected_type == "list" ||
                       expected_type == "Object" || expected_type == "object";
            }
            return false;
        }, value);
    }

    std::string get_value_type_name(ConstraintValue const& value) const {
        return std::visit([](auto const& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::string>) return "String";
            else if constexpr (std::is_same_v<T, int64_t>) return "int";
            else if constexpr (std::is_same_v<T, double>) return "double";
            else if constexpr (std::is_same_v<T, NilValue>) return "nil";
            else if constexpr (std::is_same_v<T, FactList>) return "List";
            return "unknown";
        }, value);
    }

    map<std::string, ParsedDeclaration const*> declarations_;
};

#endif // SCHEMA_VALIDATOR_HPP
