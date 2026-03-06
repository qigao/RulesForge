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
#include "core/fact.hpp"

namespace rulesforge { class ExpressionEvaluator; }

struct SourcePosition {
    std::string source;
    std::size_t line = 0;
    std::size_t column = 0;
};

enum class NodeType { LEAF, AND, OR };

enum class CompareOp : uint8_t {
    None,           // No operator (pure binding)
    EQ,             // ==
    NE,             // !=
    GT,             // >
    LT,             // <
    GE,             // >=
    LE,             // <=
    Contains,       // contains (string substring or collection membership)
    NotContains,    // not contains
    Matches,        // matches (regex)
    NotMatches,     // not matches
    StartsWith,     // startsWith
    EndsWith,       // endsWith
    LengthIs,       // lengthIs
    MemberOf,       // memberOf
    NotMemberOf,    // not memberOf
    In,             // in
    NotIn,          // not in
    ContainsKey,    // containsKey (for Map)
    NotContainsKey  // not containsKey (for Map)
};

/**
 * @brief Parse comparison operator string to enum (called once at parse time)
 */
inline CompareOp parse_compare_op(std::string_view s) {
    if (s.empty()) return CompareOp::None;
    if (s == "==") return CompareOp::EQ;
    if (s == "!=") return CompareOp::NE;
    if (s == ">") return CompareOp::GT;
    if (s == "<") return CompareOp::LT;
    if (s == ">=") return CompareOp::GE;
    if (s == "<=") return CompareOp::LE;
    if (s == "contains") return CompareOp::Contains;
    if (s == "not contains") return CompareOp::NotContains;
    if (s == "matches") return CompareOp::Matches;
    if (s == "not matches") return CompareOp::NotMatches;
    if (s == "startsWith") return CompareOp::StartsWith;
    if (s == "endsWith") return CompareOp::EndsWith;
    if (s == "lengthIs") return CompareOp::LengthIs;
    if (s == "memberOf") return CompareOp::MemberOf;
    if (s == "not memberOf") return CompareOp::NotMemberOf;
    if (s == "in") return CompareOp::In;
    if (s == "not in") return CompareOp::NotIn;
    if (s == "containsKey") return CompareOp::ContainsKey;
    if (s == "not containsKey") return CompareOp::NotContainsKey;
    return CompareOp::None;
}

/**
 * @brief Get negated operator for NOT patterns
 */
inline CompareOp negate_compare_op(CompareOp op) {
    switch (op) {
        case CompareOp::EQ: return CompareOp::NE;
        case CompareOp::NE: return CompareOp::EQ;
        case CompareOp::GT: return CompareOp::LE;
        case CompareOp::LE: return CompareOp::GT;
        case CompareOp::GE: return CompareOp::LT;
        case CompareOp::LT: return CompareOp::GE;
        default: return op;
    }
}

/**
 * @brief Convert CompareOp to string for debugging
 */
inline std::string_view compare_op_str(CompareOp op) {
    switch (op) {
        case CompareOp::None: return "";
        case CompareOp::EQ: return "==";
        case CompareOp::NE: return "!=";
        case CompareOp::GT: return ">";
        case CompareOp::LT: return "<";
        case CompareOp::GE: return ">=";
        case CompareOp::LE: return "<=";
        case CompareOp::Contains: return "contains";
        case CompareOp::NotContains: return "not contains";
        case CompareOp::Matches: return "matches";
        case CompareOp::NotMatches: return "not matches";
        case CompareOp::StartsWith: return "startsWith";
        case CompareOp::EndsWith: return "endsWith";
        case CompareOp::LengthIs: return "lengthIs";
        case CompareOp::MemberOf: return "memberOf";
        case CompareOp::NotMemberOf: return "not memberOf";
        case CompareOp::In: return "in";
        case CompareOp::NotIn: return "not in";
        case CompareOp::ContainsKey: return "containsKey";
        case CompareOp::NotContainsKey: return "not containsKey";
    }
    return "";
}

/**
 * @brief Temporal operators as enum
 */
enum class TemporalOp : uint8_t {
    None,
    After,
    Before,
    Within,
    Coincides,
    During
};

inline TemporalOp parse_temporal_op(std::string_view s) {
    if (s == "after") return TemporalOp::After;
    if (s == "before") return TemporalOp::Before;
    if (s == "within") return TemporalOp::Within;
    if (s == "coincides") return TemporalOp::Coincides;
    if (s == "during") return TemporalOp::During;
    return TemporalOp::None;
}

inline char const* temporal_op_str(TemporalOp op) {
    switch (op) {
        case TemporalOp::None: return "";
        case TemporalOp::After: return "after";
        case TemporalOp::Before: return "before";
        case TemporalOp::Within: return "within";
        case TemporalOp::Coincides: return "coincides";
        case TemporalOp::During: return "during";
    }
    return "";
}

struct ParsedTemporalConstraint {
    TemporalOp op = TemporalOp::None;
    std::string lhs_field;
    std::pair<std::string, std::string> rhs_binding_and_field;
    int64_t window_ms = -1;
};



struct ParsedConstraint {
    std::optional<std::string> field_binding;
    std::optional<std::string> left_binding;
    std::string left_field;
    std::vector<PathSegment> cached_left_field_path; // Optimization: parsed path
    CompareOp op = CompareOp::None;
    std::optional<ConstraintValue> right_literal;
    std::optional<std::pair<std::string, std::string>> right_bound_field;
    std::vector<PathSegment> cached_right_field_path; // Optimization: parsed path for RHS binding
    std::optional<std::vector<ConstraintValue>> right_value_list;
    std::optional<ParsedTemporalConstraint> temporal_constraint;
    std::optional<std::string> right_arith_expr;  // Original expression string for compilation
    std::shared_ptr<rulesforge::ExpressionEvaluator> compiled_expr;  // Compiled expression (replaces ArithExprValue AST)
    ParsedConstraint() = default;
    ParsedConstraint(ParsedConstraint&&) = default;
    ParsedConstraint& operator=(ParsedConstraint&&) = default;
    ParsedConstraint(ParsedConstraint const& other);
    ParsedConstraint& operator=(ParsedConstraint const& other);
    bool operator==(ParsedConstraint const& other) const;
 };

std::string constraint_to_string(ParsedConstraint const& constraint);


struct ConstraintNode {
    NodeType type;
    ParsedConstraint constraint;
    std::vector<std::unique_ptr<ConstraintNode>> children;
    ConstraintNode(NodeType t);
    ConstraintNode();
    ConstraintNode(ConstraintNode const& other);
    ConstraintNode(ConstraintNode&&) = default;
    ConstraintNode& operator=(ConstraintNode const& other);
    ConstraintNode& operator=(ConstraintNode&&) = default;
    ~ConstraintNode() = default;
};

enum FieldType : uint16_t {
    FT_String  = 1 << 0,
    FT_Int     = 1 << 1,
    FT_Long    = 1 << 2,
    FT_Double  = 1 << 3,
    FT_Float   = 1 << 4,
    FT_Number  = 1 << 5,
    FT_Boolean = 1 << 6,
    FT_List    = 1 << 7,
    FT_Object  = 1 << 8,
    FT_Unknown = 1 << 9,
    FT_Set     = 1 << 10,
    FT_Map     = 1 << 11
};

constexpr uint16_t FT_STRING_COMPAT = FT_String | FT_Object;
constexpr uint16_t FT_INT_COMPAT = FT_Int | FT_Long | FT_Number;
constexpr uint16_t FT_DOUBLE_COMPAT = FT_Double | FT_Float | FT_Number;
constexpr uint16_t FT_LIST_COMPAT = FT_List;
constexpr uint16_t FT_SET_COMPAT = FT_Set;
constexpr uint16_t FT_MAP_COMPAT = FT_Map;

inline FieldType parse_field_type(std::string_view s) {
    if (s == "String" || s == "string") return FT_String;
    if (s == "int" || s == "Integer") return FT_Int;
    if (s == "long" || s == "Long") return FT_Long;
    if (s == "double" || s == "Double") return FT_Double;
    if (s == "float" || s == "Float") return FT_Float;
    if (s == "Number" || s == "number") return FT_Number;
    if (s == "boolean" || s == "Boolean" || s == "bool") return FT_Boolean;
    if (s == "List" || s == "list") return FT_List;
    if (s == "Set" || s == "set") return FT_Set;
    if (s == "Map" || s == "map") return FT_Map;
    if (s == "Object" || s == "object") return FT_Object;
    return FT_Unknown;
}

struct TypeParameter {
    FieldType base_type = FT_Unknown;
    std::string custom_type;  // For FT_Object, stores the custom type name like "Item"
    std::unique_ptr<TypeParameter> nested;  // For nested generics like List<List<int>>

    TypeParameter() = default;
    TypeParameter(FieldType t) : base_type(t) {}
    TypeParameter(FieldType t, std::string custom) : base_type(t), custom_type(std::move(custom)) {}
    TypeParameter(TypeParameter const& other);
    TypeParameter(TypeParameter&&) = default;
    TypeParameter& operator=(TypeParameter const& other);
    TypeParameter& operator=(TypeParameter&&) = default;
};

struct ParsedField {
    std::string name;
    FieldType type;
    std::vector<TypeParameter> type_params;  // Generic type parameters: <T> or <K,V>
};

struct ParsedDeclaration {
    std::string type_name;
    std::vector<ParsedField> fields;
    std::map<std::string, std::string> annotations;
    std::optional<std::string> expires;
    std::string source_package;
};
