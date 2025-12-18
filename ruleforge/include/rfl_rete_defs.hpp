#ifndef RFL_RETE_DEFS_HPP
#define RFL_RETE_DEFS_HPP


#include <algorithm>   // For std::reverse
#include <chrono>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tao/pegtl/position.hpp>
#include <utility>
#include <variant>
#include <vector>
#include "phmap.h"

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
struct ParsedRule;

// --- Enums and Basic Types ---
enum class PatternType { STANDARD, NOT, EXISTS, FORALL, EVAL, QUERY_CALL };
enum class NodeType { LEAF, AND, OR };
enum class PropagationType { ASSERT, RETRACT, MODIFY };

// --- Arithmetic Expression AST ---
enum class ArithOp { ADD, SUB, MUL, DIV };

struct ArithExprNode;

using ArithExprValue = std::variant<
    double,                                    // Numeric literal
    std::string,                               // Variable binding ($var) or field reference
    std::unique_ptr<ArithExprNode>             // Nested expression
>;

struct ArithExprNode {
    ArithOp op;
    ArithExprValue left;
    ArithExprValue right;

    ArithExprNode() : op(ArithOp::ADD) {}
    ArithExprNode(ArithOp o, ArithExprValue l, ArithExprValue r)
        : op(o), left(std::move(l)), right(std::move(r)) {}

    // Deep copy support
    ArithExprNode(ArithExprNode const& other);
    ArithExprNode& operator=(ArithExprNode const& other);
    ArithExprNode(ArithExprNode&&) = default;
    ArithExprNode& operator=(ArithExprNode&&) = default;
};

// Helper to deep copy ArithExprValue
ArithExprValue clone_arith_expr_value(ArithExprValue const& v);

// --- Debug Info Structs ---

struct FactList {
    std::vector<std::shared_ptr<Fact>> facts;
};

struct NilValue {};

using ConstraintValue = std::variant<std::string, int64_t, double, FactList, NilValue>;

// --- Free Function Declarations ---
std::string to_string(ConstraintValue const& val);

// --- Core Data Structs ---
struct ConstraintValueHasher {
    std::size_t operator()(ConstraintValue const& v) const;
};

struct Fact {
    int64_t id = 0;
    std::string type;
    map<std::string, ConstraintValue> fields;
    std::optional<ConstraintValue> get_field(std::string const& name) const;
};

struct TokenWME {
    std::shared_ptr<TokenWME const> parent;
    std::shared_ptr<Fact const> fact;
    int depth;
    size_t hash;
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
    map<std::string, int> bindings;
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

struct ParsedTemporalConstraint {
    std::string op;
    std::string lhs_field;
    std::pair<std::string, std::string> rhs_binding_and_field;
    int64_t window_ms = -1;
};

struct ParsedConstraint {
    std::optional<std::string> field_binding;
    std::optional<std::string> left_binding;
    std::string left_field;
    std::string op;
    std::optional<ConstraintValue> right_literal;
    std::optional<std::pair<std::string, std::string>> right_bound_field;
    std::optional<std::vector<ConstraintValue>> right_value_list;
    std::optional<ParsedTemporalConstraint> temporal_constraint;
    std::optional<std::string> right_arith_expr;  // Legacy: string form (deprecated)
    std::optional<ArithExprValue> right_arith_ast; 
    ParsedConstraint() = default;
    ParsedConstraint(ParsedConstraint&&) = default;
    ParsedConstraint& operator=(ParsedConstraint&&) = default;
    ParsedConstraint(ParsedConstraint const& other);
    ParsedConstraint& operator=(ParsedConstraint const& other);
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
    std::optional<ArithExprValue> accumulate_expr_ast; 
    map<std::string, std::string> inline_binding_to_field; 
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

struct ParsedField {
    std::string name;
    std::string type;
};

struct ParsedDeclaration {
    std::string type_name;
    std::vector<ParsedField> fields;
    std::optional<std::string> expires;
    std::string source_package;
};

struct ParsedQuery {
    std::string name;
    std::vector<ParsedPattern> patterns;
    tao::pegtl::position pos;
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
    tao::pegtl::position pos;
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
    tao::pegtl::position pos;
    std::string name;
    map<std::string, std::string> annotations;
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


