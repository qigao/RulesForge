#ifndef DROOLS_RETE_DEFS_HPP
#define DROOLS_RETE_DEFS_HPP

#include "lua_ast.hpp"

#include <algorithm>   // For std::reverse
#include <chrono>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <tao/pegtl/position.hpp>
#include <utility>
#include <variant>
#include <vector>

using json = nlohmann::json;

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
    std::map<std::string, ConstraintValue> fields;
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
    PropagationType type;
    Token(std::shared_ptr<TokenWME const> w, PropagationType pt);
    std::shared_ptr<Fact const> get_fact() const;
    int get_depth() const;
    std::vector<std::shared_ptr<Fact>> get_facts() const;
    std::shared_ptr<Fact> get_fact_at_depth(int d) const;
};

struct Activation {
    ParsedRule const* rule;
    std::shared_ptr<Token> token;
    size_t hash_value;
    std::map<std::string, int> bindings;
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
};

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
    std::string field;
    std::string accumulate_field_name;
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
    std::map<std::string, std::string> annotations;
    int salience = 0;
    bool salience_explicitly_set = false;
    std::optional<std::string> parent_rule_name;
    std::optional<std::string> agenda_group;
    std::optional<ParsedTimer> timer;
    std::vector<std::vector<ParsedPattern>> condition_groups;
    std::string rhs_code;
    size_t rhs_start_line = 0;
    std::string source_package;
    std::vector<std::string> source_imports;
    ParsedRule();
};

// --- JSON SERIALIZATION ---

namespace nlohmann {
    template <size_t I, typename... Tp>
    void from_json_variant_impl(json const& j, std::variant<Tp...>& v) {
        if (j.at("index").get<size_t>() == I) {
            using CurrentType = std::variant_alternative_t<I, std::variant<Tp...>>;
            if constexpr (std::is_same_v<CurrentType, std::monostate>) {
                v.template emplace<I>();
            } else {
                v.template emplace<I>(j.at("value").get<CurrentType>());
            }
            return;
        }
        if constexpr (I + 1 < sizeof...(Tp)) { from_json_variant_impl<I + 1>(j, v); }
    }

    template <typename... T>
    struct adl_serializer<std::variant<T...>> {
        inline static void to_json(json& j, std::variant<T...> const& v) {
            std::visit(
                [&](auto const& val) {
                    j = json::object();
                    j["index"] = v.index();
                    using Type = std::decay_t<decltype(val)>;
                    if constexpr (std::is_same_v<Type, std::monostate>) {
                        j["value"] = nullptr;
                    } else {
                        j["value"] = val;
                    }
                },
                v);
        }

        inline static void from_json(json const& j, std::variant<T...>& v) {
            if (!j.is_object() || !j.contains("index") || !j.contains("value")) {
                v = std::variant<T...>{};
                return;
            }
            from_json_variant_impl<0>(j, v);
        }
    };

    template <typename T>
    struct adl_serializer<std::optional<T>> {
        inline static void to_json(json& j, std::optional<T> const& opt) {
            if (opt.has_value()) {
                j = *opt;
            } else {
                j = nullptr;
            }
        }

        inline static void from_json(json const& j, std::optional<T>& opt) {
            if (j.is_null()) {
                opt = std::nullopt;
            } else {
                opt = j.get<T>();
            }
        }
    };
}   // namespace nlohmann

NLOHMANN_JSON_SERIALIZE_ENUM(PatternType, {{PatternType::STANDARD, "STANDARD"},
                                           {PatternType::NOT, "NOT"},
                                           {PatternType::EXISTS, "EXISTS"},
                                           {PatternType::FORALL, "FORALL"},
                                           {PatternType::EVAL, "EVAL"},
                                           {PatternType::QUERY_CALL, "QUERY_CALL"}})
NLOHMANN_JSON_SERIALIZE_ENUM(NodeType, {{NodeType::LEAF, "LEAF"}, {NodeType::AND, "AND"}, {NodeType::OR, "OR"}})

void to_json(json& j, FactList const& p);
void from_json(json const& j, FactList& p);
void to_json(json& j, NilValue const& p);
void from_json(json const& j, NilValue& p);
void to_json(json& j, ParsedConstraint const& p);
void from_json(json const& j, ParsedConstraint& p);
void to_json(json& j, ConstraintNode const& p);
void from_json(json const& j, ConstraintNode& p);
void to_json(json& j, std::unique_ptr<ConstraintNode> const& p);
void from_json(json const& j, std::unique_ptr<ConstraintNode>& p);
void to_json(json& j, ParsedAccumulate const& p);
void from_json(json const& j, ParsedAccumulate& p);
void to_json(json& j, ParsedQuery const& p);
void from_json(json const& j, ParsedQuery& p);
void to_json(json& j, ParsedPattern const& p);
void from_json(json const& j, ParsedPattern& p);
void to_json(json& j, std::unique_ptr<ParsedPattern> const& p);
void from_json(json const& j, std::unique_ptr<ParsedPattern>& p);
void to_json(json& j, ParsedRule const& p);
void from_json(json const& j, ParsedRule& p);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ParsedUnnest, source_binding, source_field)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ParsedQueryCall, query_name, arguments)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ParsedField, name, type)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ParsedDeclaration, type_name, fields, expires, source_package)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ParsedFunction, name, return_type, body, parameter_list)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ParsedGlobal, type, name)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ParsedTimer, initial_delay, repeat_interval)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ParsedTemporalConstraint, op, lhs_field, rhs_binding_and_field, window_ms)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ParsedForall, patterns)

// --- INLINE IMPLEMENTATIONS for trivial functions ---
inline bool operator==(FactList const&, FactList const&) { return false; }

inline bool operator!=(FactList const&, FactList const&) { return true; }

inline bool operator==(NilValue const&, NilValue const&) { return true; }

inline bool operator!=(NilValue const&, NilValue const&) { return false; }

#endif   // DROOLS_RETE_DEFS_HPP
