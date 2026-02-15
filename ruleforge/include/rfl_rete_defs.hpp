#ifndef RFL_RETE_DEFS_HPP
#define RFL_RETE_DEFS_HPP


#include <algorithm>   // For std::reverse
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include "phmap.h"

// Parser-agnostic source position (replaces tao::pegtl::position)
struct SourcePosition {
    std::string source;
    std::size_t line = 0;
    std::size_t column = 0;
};

// --- Forward Declarations ---
struct Fact;
struct ParsedPattern;
struct ConstraintNode;
struct TokenWME;
struct Token;
struct ParsedRule;
struct Activation;
struct ScheduledActivation;
struct ScheduledExpiration;

namespace ruleforge {
    class CompiledExpression;
}

// --- Enums and Basic Types ---
enum class PatternType { STANDARD, NOT, EXISTS, FORALL, EVAL, QUERY_CALL };
enum class NodeType { LEAF, AND, OR };
enum class PropagationType { ASSERT, RETRACT, MODIFY };

// --- Debug Info Structs ---

struct FactList {
    std::vector<std::shared_ptr<Fact>> facts;
};

struct NilValue {};

using ConstraintValue = std::variant<std::string, int64_t, double, FactList, NilValue>;

// --- Free Function Declarations ---
std::string to_string(ConstraintValue const& val);

struct IndexAccess {
    bool is_integer;
    int64_t int_index;
    std::string str_index;
};

struct PathSegment {
    std::string name;
    bool null_safe;
    std::vector<IndexAccess> indices;
};

// --- Core Data Structs ---
struct ConstraintValueHasher {
    std::size_t operator()(ConstraintValue const& v) const;
};

struct Fact {
    int64_t id = 0;
    std::string type;
    ruleforge::unordered_map<std::string, ConstraintValue> fields;
    std::optional<ConstraintValue> get_field(std::string const& name) const;
    std::optional<ConstraintValue> get_field(std::vector<PathSegment> const& path) const;
};

// Optimization: Expose parser for field paths
std::vector<PathSegment> parse_field_path(std::string const& path);
std::optional<ConstraintValue> apply_index(ConstraintValue const& val, IndexAccess const& idx, bool null_safe);

struct TokenWME {
    std::shared_ptr<TokenWME const> parent;
    std::shared_ptr<Fact const> fact;
    int depth;
    size_t hash;
    // Removed fact_index vector to save memory and avoid copy overhead
    bool operator==(TokenWME const& other) const;

    uintptr_t get_id() const { return reinterpret_cast<uintptr_t>(this); }
};

struct TokenWMEPtrHasher {
    size_t operator()(std::shared_ptr<TokenWME const> const& wme) const { return wme ? wme->hash : 0; }
};

struct TokenWMEPtrEquals {
    bool operator()(std::shared_ptr<TokenWME const> const& a, std::shared_ptr<TokenWME const> const& b) const {
        if (!a || !b) return !a && !b;
        return *a == *b;
    }
};

struct Token {
    std::shared_ptr<TokenWME const> wme;
    PropagationType type = PropagationType::ASSERT;
    Token() = default;
    Token(std::shared_ptr<TokenWME const> w, PropagationType pt);
    std::shared_ptr<Fact const> get_fact() const;
    int get_depth() const;
    std::vector<std::shared_ptr<Fact>> get_facts() const;
    std::shared_ptr<Fact> get_fact_at_depth(int d) const;
};

struct Activation {
    ParsedRule const* rule;
    Token token;
    size_t hash_value;
    ruleforge::map<std::string, int> bindings;
    bool operator<(Activation const& other) const;
};

struct ScheduledActivation {
    std::chrono::steady_clock::time_point activation_time;
    ParsedRule const* rule;
    int64_t repeat_interval;
    bool operator>(ScheduledActivation const& other) const;
};

struct ScheduledExpiration {
    std::chrono::steady_clock::time_point expiration_time;
    int64_t fact_id;
    bool operator>(ScheduledExpiration const& other) const;
};

// --- Parsed Struct Definitions ---
struct ParsedAccumulate;
struct ParsedUnnest;
struct ParsedQueryCall;
using PatternSource = std::variant<std::monostate, ParsedAccumulate, ParsedUnnest, ParsedQueryCall, std::string>;

/**
 * @brief Comparison operators as enum for O(1) dispatch
 */
enum class CompareOp : uint8_t {
    None,           // No operator (pure binding)
    EQ,             // ==
    NE,             // !=
    GT,             // >
    LT,             // <
    GE,             // >=
    LE,             // <=
    Contains,       // contains
    NotContains,    // not contains
    Matches,        // matches (regex)
    NotMatches,     // not matches
    StartsWith,     // startsWith
    EndsWith,       // endsWith
    LengthIs,       // lengthIs
    MemberOf,       // memberOf
    NotMemberOf,    // not memberOf
    In,             // in
    NotIn           // not in
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
    std::shared_ptr<ruleforge::CompiledExpression> compiled_expr;  // Compiled expression (replaces ArithExprValue AST)
    ParsedConstraint() = default;
    ParsedConstraint(ParsedConstraint&&) = default;
    ParsedConstraint& operator=(ParsedConstraint&&) = default;
    ParsedConstraint(ParsedConstraint const& other);
    ParsedConstraint& operator=(ParsedConstraint const& other);
    bool operator==(ParsedConstraint const& other) const;
 };

// Convert a ParsedConstraint to a human-readable string for debugging
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

struct ParsedAccumulate {
    std::unique_ptr<ParsedPattern> source_pattern;
    std::string function;
    std::string field;  // Original field string from parser
    std::string accumulate_field_name;  // Simple field name (for non-arithmetic)
    std::shared_ptr<ruleforge::CompiledExpression> compiled_expr;  // Compiled expression (replaces ArithExprValue AST)
    ruleforge::map<std::string, std::string> inline_binding_to_field;
    ParsedAccumulate();
    ParsedAccumulate(ParsedAccumulate&&) = default;
    ParsedAccumulate& operator=(ParsedAccumulate&&) = default;
    ParsedAccumulate(ParsedAccumulate const& other);
    ParsedAccumulate& operator=(ParsedAccumulate const& other);
};

struct ParsedUnnest {
    std::string source_binding;
    std::string source_field;
};

struct ParsedQueryCall {
    std::string query_name;
    std::vector<std::string> arguments;
};

struct ParsedForall {
    std::vector<ParsedPattern> patterns;
};

/**
 * @brief Bitmap-based field types for O(1) type validation via bitwise AND
 */
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
    FT_Unknown = 1 << 9
};

// Compatible type masks for single-instruction validation
constexpr uint16_t FT_STRING_COMPAT = FT_String | FT_Object;
constexpr uint16_t FT_INT_COMPAT    = FT_Int | FT_Long | FT_Number | FT_Object;
constexpr uint16_t FT_DOUBLE_COMPAT = FT_Double | FT_Float | FT_Number | FT_Object;
constexpr uint16_t FT_LIST_COMPAT   = FT_List | FT_Object;

/**
 * @brief Parse field type string to bitmap (called once at parse time)
 */
inline FieldType parse_field_type(std::string_view s) {
    if (s == "String" || s == "string") return FT_String;
    if (s == "int" || s == "Integer") return FT_Int;
    if (s == "long" || s == "Long") return FT_Long;
    if (s == "double" || s == "Double") return FT_Double;
    if (s == "float" || s == "Float") return FT_Float;
    if (s == "Number" || s == "number") return FT_Number;
    if (s == "boolean" || s == "Boolean" || s == "bool") return FT_Boolean;
    if (s == "List" || s == "list") return FT_List;
    if (s == "Object" || s == "object") return FT_Object;
    return FT_Unknown;
}

struct ParsedField {
    std::string name;
    FieldType type;
};

struct ParsedDeclaration {
    std::string type_name;
    std::vector<ParsedField> fields;
    ruleforge::map<std::string, std::string> annotations;
    std::optional<std::string> expires;
    std::string source_package;
};

struct ParsedQuery {
    std::string name;
    std::vector<ParsedPattern> patterns;
    SourcePosition pos;
    std::vector<std::string> parameter_types;
    int parameter_count = 0;
    std::string source_package;
    std::vector<std::string> source_imports;
    ParsedQuery();
};

struct ParsedFunction {
    std::string name;
    std::string return_type;
    std::string body;
    std::string parameter_list;
};

struct ParsedGlobal {
    std::string type;
    std::string name;
};

struct ParsedTimer {
    int64_t initial_delay;
    int64_t repeat_interval = -1;
};

struct ParsedPattern {
    PatternType type = PatternType::STANDARD;
    SourcePosition pos;
    std::string binding;
    std::string fact_type;
    std::unique_ptr<ConstraintNode> constraint_root;
    std::vector<ParsedPattern> nested_patterns;
    std::optional<std::string> eval_expression;
    std::optional<ParsedForall> forall_info;
    PatternSource source;
    ParsedPattern();
    ParsedPattern(ParsedPattern const& other);
    ParsedPattern(ParsedPattern&&) = default;
    ParsedPattern& operator=(ParsedPattern const& other);
    ParsedPattern& operator=(ParsedPattern&&) = default;
    ~ParsedPattern() = default;
};

struct ParsedRule {
    SourcePosition pos;
    std::string name;
    ruleforge::map<std::string, std::string> annotations;
    int salience = 0;
    bool salience_explicitly_set = false;
    bool no_loop = false;
    bool lock_on_active = false;  // P1 FIX: lock-on-active attribute
    bool enabled = true;
    bool auto_focus = false;
    int64_t duration = 0;
    std::optional<std::string> parent_rule_name;
    std::optional<std::string> agenda_group;
    std::optional<std::string> activation_group;  // P1 FIX: activation-group attribute
    std::optional<ParsedTimer> timer;
    std::vector<std::vector<ParsedPattern>> condition_groups;
    std::string rhs_code;
    size_t rhs_start_line = 0;
    std::string source_package;
    std::vector<std::string> source_imports;
    ParsedRule();
};

// --- INLINE IMPLEMENTATIONS for trivial functions ---
inline bool operator==(FactList const&, FactList const&) { return false; }

inline bool operator!=(FactList const&, FactList const&) { return true; }

inline bool operator==(NilValue const&, NilValue const&) { return true; }

inline bool operator!=(NilValue const&, NilValue const&) { return false; }

#endif   // RFL_RETE_DEFS_HPP


