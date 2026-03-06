#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <variant>
#include <chrono>

#include "core/rfl_strings.hpp"


#include "core/value_types.hpp"

namespace rulesforge { class ExpressionEvaluator; }

enum class RhsActionType : uint8_t {
    UPDATE,
    INSERT,
    INSERT_LOGICAL,
    RETRACT,
    HALT,
    SET_FOCUS,
    INVOKE,
    IF,
    FOR,
    WHILE,
    SWITCH,
    BREAK,
    CONTINUE
};

enum class RhsValueType : uint8_t {
    NUMERIC,      // Compiled numeric expression
    STRING,       // String literal
    VAR_REF,      // Variable reference ($var or $var.field)
    BOOLEAN,      // Boolean literal (true/false)
    NATIVE_CALL   // Native function call: fn(arg1, arg2)
};

struct FieldAssignment {
    std::string field_name;
    std::string_view field_key;
    std::shared_ptr<rulesforge::ExpressionEvaluator> numeric_expr;
    std::string string_literal;
    std::string var_ref;  // For VAR_REF type: "$var" or "$var.field"
    std::string native_call_name;
    std::vector<FieldAssignment> native_call_args;
    RhsValueType type = RhsValueType::STRING;
    bool has_precomputed_literal = false;
    ConstraintValue precomputed_literal;
};

struct CompiledAction;

struct SwitchCase {
    std::shared_ptr<rulesforge::ExpressionEvaluator> value;  // case value (nullptr for default)
    std::vector<CompiledAction> actions;
    bool is_default = false;
};

struct CompiledAction {
    RhsActionType type = RhsActionType::UPDATE;
    std::string target_var;           // UPDATE/RETRACT target ($var)
    std::string target_type;          // INSERT type name
    std::vector<FieldAssignment> assignments;
    std::string focus_group;          // SET_FOCUS group name
    std::shared_ptr<rulesforge::ExpressionEvaluator> condition;  // IF/WHILE condition
    std::vector<CompiledAction> then_actions;
    std::vector<CompiledAction> else_actions;
    std::string iter_var;             // FOR loop variable
    std::string iter_source_var;      // FOR source variable ($var)
    std::string iter_source_field;    // FOR field iteration ($var.field)
    std::vector<std::string> iter_source_list;  // FOR value list iteration ($a, $b, $c)
    std::vector<CompiledAction> body_actions;  // FOR/WHILE body
    int max_iterations = 1000;        // WHILE safety limit
    std::shared_ptr<rulesforge::ExpressionEvaluator> switch_expr;  // SWITCH expression
    std::vector<SwitchCase> switch_cases;  // SWITCH cases
    std::string invoke_function;       // INVOKE function name
    std::vector<FieldAssignment> invoke_args;  // INVOKE arguments
};
