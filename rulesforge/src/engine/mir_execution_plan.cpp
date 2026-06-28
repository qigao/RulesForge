#include "engine/mir_execution_plan.hpp"

#include "core/parsed_rule.hpp"
#include "engine/external_eval.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

extern "C" {
#include <mir.h>
#include <mir-gen.h>
}

namespace rulesforge {

extern "C" std::int64_t rulesforge_mir_string_compare(char const* lhs, char const* rhs) {
    if (lhs == nullptr || rhs == nullptr) {
        if (lhs == rhs) return 0;
        return lhs == nullptr ? -1 : 1;
    }
    int const result = std::strcmp(lhs, rhs);
    if (result < 0) return -1;
    if (result > 0) return 1;
    return 0;
}

extern "C" std::int64_t rulesforge_mir_string_contains(char const* lhs, char const* rhs) {
    if (lhs == nullptr || rhs == nullptr) {
        return 0;
    }
    return std::strstr(lhs, rhs) != nullptr ? 1 : 0;
}

extern "C" std::int64_t rulesforge_mir_string_regex_match(char const* lhs, char const* rhs) {
    if (lhs == nullptr || rhs == nullptr) {
        return -1;
    }
    try {
        std::regex re(rhs);
        return std::regex_search(lhs, re) ? 1 : 0;
    } catch (std::regex_error const&) {
        return -1;
    }
}

extern "C" std::int64_t rulesforge_mir_string_starts_with(char const* lhs, char const* rhs) {
    if (lhs == nullptr || rhs == nullptr) {
        return 0;
    }
    std::size_t const rhs_len = std::strlen(rhs);
    return std::strncmp(lhs, rhs, rhs_len) == 0 ? 1 : 0;
}

extern "C" std::int64_t rulesforge_mir_string_ends_with(char const* lhs, char const* rhs) {
    if (lhs == nullptr || rhs == nullptr) {
        return 0;
    }
    std::size_t const lhs_len = std::strlen(lhs);
    std::size_t const rhs_len = std::strlen(rhs);
    if (rhs_len > lhs_len) {
        return 0;
    }
    return std::strcmp(lhs + (lhs_len - rhs_len), rhs) == 0 ? 1 : 0;
}

extern "C" std::int64_t rulesforge_mir_string_length_is(char const* lhs, std::int64_t rhs) {
    if (lhs == nullptr || rhs < 0) {
        return 0;
    }
    return static_cast<std::int64_t>(std::strlen(lhs)) == rhs ? 1 : 0;
}

extern "C" std::int64_t rulesforge_mir_map_contains_key(std::int64_t op,
                                                         void const* lhs_ptr,
                                                         void const* rhs_ptr) {
    if (lhs_ptr == nullptr || rhs_ptr == nullptr) {
        return -1;
    }
    auto const& lhs = *static_cast<ConstraintValue const*>(lhs_ptr);
    auto const& rhs = *static_cast<ConstraintValue const*>(rhs_ptr);
    auto const* map_owner = std::get_if<std::shared_ptr<ValueMap>>(&lhs);
    if (map_owner == nullptr || !*map_owner) {
        return -1;
    }
    bool const contains = (*map_owner)->entries.find(rhs) != (*map_owner)->entries.end();
    if (op == static_cast<std::int64_t>(CompareOp::ContainsKey)) {
        return contains ? 1 : 0;
    }
    if (op == static_cast<std::int64_t>(CompareOp::NotContainsKey)) {
        return contains ? 0 : 1;
    }
    return -1;
}

extern "C" std::int64_t rulesforge_mir_collection_contains(std::int64_t op,
                                                            void const* lhs_ptr,
                                                            void const* rhs_ptr) {
    if (lhs_ptr == nullptr || rhs_ptr == nullptr) {
        return -1;
    }
    auto const& lhs = *static_cast<ConstraintValue const*>(lhs_ptr);
    auto const& rhs = *static_cast<ConstraintValue const*>(rhs_ptr);

    ConstraintValue const* collection_value = nullptr;
    ConstraintValue const* element_value = nullptr;
    bool negate = false;
    if (op == static_cast<std::int64_t>(CompareOp::Contains)
        || op == static_cast<std::int64_t>(CompareOp::NotContains)) {
        collection_value = &lhs;
        element_value = &rhs;
        negate = op == static_cast<std::int64_t>(CompareOp::NotContains);
    } else if (op == static_cast<std::int64_t>(CompareOp::MemberOf)
               || op == static_cast<std::int64_t>(CompareOp::NotMemberOf)) {
        collection_value = &rhs;
        element_value = &lhs;
        negate = op == static_cast<std::int64_t>(CompareOp::NotMemberOf);
    } else {
        return -1;
    }

    ConstraintValueCompare compare;
    auto same_value = [&compare](ConstraintValue const& left, ConstraintValue const& right) {
        return !compare(left, right) && !compare(right, left);
    };

    bool contains = false;
    if (auto const* list_owner = std::get_if<std::shared_ptr<TypedList>>(collection_value)) {
        if (*list_owner == nullptr) {
            return -1;
        }
        contains = std::any_of(
            (*list_owner)->values.begin(),
            (*list_owner)->values.end(),
            [&](ConstraintValue const& value) { return same_value(value, *element_value); });
    } else if (auto const* set_owner = std::get_if<std::shared_ptr<ValueSet>>(collection_value)) {
        if (*set_owner == nullptr) {
            return -1;
        }
        contains = (*set_owner)->values.find(*element_value) != (*set_owner)->values.end();
    } else if (auto const* fact_list = std::get_if<FactList>(collection_value)) {
        auto const* fact_type = std::get_if<std::string>(element_value);
        if (fact_type == nullptr) {
            return -1;
        }
        contains = std::any_of(
            fact_list->facts.begin(),
            fact_list->facts.end(),
            [&](Fact const* fact) { return fact != nullptr && fact->type == *fact_type; });
    } else if (std::holds_alternative<int64_t>(*collection_value)
               || std::holds_alternative<double>(*collection_value)
               || std::holds_alternative<std::string>(*collection_value)
               || std::holds_alternative<NilValue>(*collection_value)) {
        contains = same_value(*collection_value, *element_value);
    } else {
        return -1;
    }
    if (negate) {
        contains = !contains;
    }
    return contains ? 1 : 0;
}

extern "C" double rulesforge_mir_math_unary(std::int64_t op, double value) {
    switch (op) {
        case 0: return std::floor(value);
        case 1: return std::fabs(value);
        case 2: return std::sqrt(value);
        case 3: return std::sin(value);
        case 4: return std::cos(value);
        case 6: return std::ceil(value);
        case 7: return std::log(value);
        case 8: return std::exp(value);
        case 9: return std::acos(value);
        case 12: return std::asin(value);
        case 13: return std::atan(value);
        case 14: return std::log10(value);
        case 15: return std::round(value);
        case 16: return std::tan(value);
        case 17: return std::trunc(value);
        case 18: return std::sinh(value);
        case 19: return std::cosh(value);
        case 20: return std::tanh(value);
        case 21: return std::log2(value);
        case 22: return std::log1p(value);
        case 23: return std::expm1(value);
        case 24: return std::erf(value);
        case 28: return (value > 0.0) ? 1.0 : ((value < 0.0) ? -1.0 : 0.0);
        default: return std::numeric_limits<double>::quiet_NaN();
    }
}

extern "C" double rulesforge_mir_math_binary(std::int64_t op, double lhs, double rhs) {
    switch (op) {
        case 5: return std::pow(lhs, rhs);
        case 10: return std::min(lhs, rhs);
        case 11: return std::max(lhs, rhs);
        case 25: return std::fmod(lhs, rhs);
        case 26: return std::atan2(lhs, rhs);
        case 27: return std::hypot(lhs, rhs);
        case 29: return std::pow(lhs, 1.0 / rhs);
        default: return std::numeric_limits<double>::quiet_NaN();
    }
}

extern "C" std::int64_t rulesforge_mir_aggregate_count(std::int64_t current, std::int64_t direction) {
    return current + direction;
}

extern "C" double rulesforge_mir_aggregate_sum(double current, double value, std::int64_t direction) {
    return current + (direction >= 0 ? value : -value);
}

extern "C" double rulesforge_mir_aggregate_min(double current, double value) {
    return std::min(current, value);
}

extern "C" double rulesforge_mir_aggregate_max(double current, double value) {
    return std::max(current, value);
}

extern "C" std::int64_t rulesforge_mir_collect_list_update(void* facts_ptr,
                                                            void* fact_ptr,
                                                            std::int64_t direction) {
    if (facts_ptr == nullptr || fact_ptr == nullptr) {
        return -1;
    }
    auto& facts = *static_cast<std::vector<Fact*>*>(facts_ptr);
    auto* fact = static_cast<Fact*>(fact_ptr);
    if (direction >= 0) {
        facts.push_back(fact);
        return 1;
    }
    auto it = std::find(facts.begin(), facts.end(), fact);
    if (it == facts.end()) {
        return 0;
    }
    *it = facts.back();
    facts.pop_back();
    return 1;
}

extern "C" std::int64_t rulesforge_mir_collect_set_update(void* facts_ptr,
                                                           void* fact_ptr,
                                                           std::int64_t direction) {
    if (facts_ptr == nullptr || fact_ptr == nullptr) {
        return -1;
    }
    auto& facts = *static_cast<std::unordered_set<Fact*>*>(facts_ptr);
    auto* fact = static_cast<Fact*>(fact_ptr);
    if (direction >= 0) {
        return facts.insert(fact).second ? 1 : 0;
    }
    return facts.erase(fact) > 0 ? 1 : 0;
}

extern "C" std::int64_t rulesforge_mir_collect_list_result(void const* facts_ptr, void* result_ptr) {
    if (facts_ptr == nullptr || result_ptr == nullptr) {
        return -1;
    }
    auto const& facts = *static_cast<std::vector<Fact*> const*>(facts_ptr);
    auto& result = *static_cast<FactList*>(result_ptr);
    result.facts = facts;
    return 1;
}

extern "C" std::int64_t rulesforge_mir_collect_set_result(void const* facts_ptr, void* result_ptr) {
    if (facts_ptr == nullptr || result_ptr == nullptr) {
        return -1;
    }
    auto const& facts = *static_cast<std::unordered_set<Fact*> const*>(facts_ptr);
    auto& result = *static_cast<FactList*>(result_ptr);
    result.facts.assign(facts.begin(), facts.end());
    return 1;
}

struct MirExecutionPlan::Impl {
    using RuleCountFn = std::int64_t (*)();
    using RuleLookupFn = std::int64_t (*)(std::int64_t);
    using CompareI64Fn = std::int64_t (*)(std::int64_t, std::int64_t, std::int64_t);
    using CompareDoubleFn = std::int64_t (*)(std::int64_t, double, double);
    using TemporalPredicateFn = std::int64_t (*)(std::int64_t, std::int64_t, std::int64_t, std::int64_t);
    using FixedCompareStringFn = std::int64_t (*)(char const*, char const*);
    using StringContainsFn = std::int64_t (*)(char const*, char const*);
    using StringRegexMatchFn = std::int64_t (*)(char const*, char const*);
    using StringAffixFn = std::int64_t (*)(char const*, char const*);
    using StringLengthIsFn = std::int64_t (*)(char const*, std::int64_t);
    using MapContainsKeyFn = std::int64_t (*)(std::int64_t, void const*, void const*);
    using CollectionContainsFn = std::int64_t (*)(std::int64_t, void const*, void const*);
    using AggregateCountFn = std::int64_t (*)(std::int64_t, std::int64_t);
    using AggregateSumFn = double (*)(double, double, std::int64_t);
    using AggregateExtremeFn = double (*)(double, double);
    using AggregateCollectUpdateFn = std::int64_t (*)(void*, void*, std::int64_t);
    using AggregateCollectResultFn = std::int64_t (*)(void const*, void*);
    using FixedCompareI64Fn = std::int64_t (*)(std::int64_t, std::int64_t);
    using FixedCompareDoubleFn = std::int64_t (*)(double, double);
    using ValueListDoublePredicateFn = std::int64_t (*)(double);
    using ValueListStringPredicateFn = std::int64_t (*)(char const*);
    using NumericExpressionFn = std::int64_t (*)(double, std::int64_t, double const*);
    using NumericValueExpressionFn = double (*)(std::int64_t, double const*);
    using EvalExpressionFn = std::int64_t (*)(std::int64_t, double const*);

    struct LiteralPredicateEntry {
        CompareOp op = CompareOp::None;
        ConstraintValue literal;
    };

    struct NumericExpressionPredicateEntry {
        void* fn = nullptr;
        std::vector<std::string> variables;
    };

    struct NumericValueExpressionEntry {
        NumericValueExpressionFn fn = nullptr;
        std::vector<std::string> variables;
    };

    struct ValueListPredicateEntry {
        bool is_string = false;
        ValueListDoublePredicateFn double_fn = nullptr;
        ValueListStringPredicateFn string_fn = nullptr;
    };

    struct EvalExpressionPredicateEntry {
        void* fn = nullptr;
        std::vector<std::string> variables;
    };

    MIR_context_t ctx = nullptr;
    RuleCountFn rule_count_fn = nullptr;
    RuleLookupFn rule_salience_fn = nullptr;
    RuleLookupFn rule_enabled_fn = nullptr;
    CompareI64Fn compare_i64_fn = nullptr;
    CompareDoubleFn compare_double_fn = nullptr;
    TemporalPredicateFn temporal_predicate_fn = nullptr;
    StringContainsFn string_contains_fn = nullptr;
    StringRegexMatchFn string_regex_match_fn = nullptr;
    StringAffixFn string_starts_with_fn = nullptr;
    StringAffixFn string_ends_with_fn = nullptr;
    StringLengthIsFn string_length_is_fn = nullptr;
    MapContainsKeyFn map_contains_key_fn = nullptr;
    CollectionContainsFn collection_contains_fn = nullptr;
    AggregateCountFn aggregate_count_fn = nullptr;
    AggregateSumFn aggregate_sum_fn = nullptr;
    AggregateExtremeFn aggregate_min_fn = nullptr;
    AggregateExtremeFn aggregate_max_fn = nullptr;
    AggregateCollectUpdateFn collect_list_update_fn = nullptr;
    AggregateCollectUpdateFn collect_set_update_fn = nullptr;
    AggregateCollectResultFn collect_list_result_fn = nullptr;
    AggregateCollectResultFn collect_set_result_fn = nullptr;
    std::array<FixedCompareI64Fn, 6> fixed_compare_i64_fns{};
    std::array<FixedCompareDoubleFn, 6> fixed_compare_double_fns{};
    std::array<FixedCompareStringFn, 6> fixed_compare_string_fns{};
    std::vector<LiteralPredicateEntry> literal_predicates;
    std::unordered_map<std::string, std::size_t> literal_predicate_ids;
    std::vector<NumericExpressionPredicateEntry> numeric_expression_predicates;
    std::unordered_map<std::string, std::size_t> numeric_expression_predicate_ids;
    std::vector<NumericValueExpressionEntry> numeric_value_expressions;
    std::unordered_map<std::string, std::size_t> numeric_value_expression_ids;
    std::vector<ValueListPredicateEntry> value_list_predicates;
    std::unordered_map<std::string, std::size_t> value_list_predicate_ids;
    std::vector<std::vector<std::string>> value_list_string_storage;
    std::vector<EvalExpressionPredicateEntry> eval_expression_predicates;
    std::unordered_map<std::string, std::size_t> eval_expression_predicate_ids;
    bool gen_initialized = false;

    ~Impl() {
        if (ctx != nullptr) {
            if (gen_initialized) {
                MIR_gen_finish(ctx);
            }
            MIR_finish(ctx);
            ctx = nullptr;
        }
    }
};

struct FieldInfo {
    FieldType type = FT_Unknown;
    std::vector<TypeParameter> type_params;
};

using FieldTypeByFact = std::unordered_map<std::string, std::unordered_map<std::string, FieldInfo>>;
using BindingFactTypes = std::unordered_map<std::string, std::string>;

namespace {

enum class NumericExpressionFunction : std::int64_t {
    Floor = 0,
    Abs = 1,
    Sqrt = 2,
    Sin = 3,
    Cos = 4,
    Pow = 5,
    Ceil = 6,
    Log = 7,
    Exp = 8,
    Acos = 9,
    Min = 10,
    Max = 11,
    Asin = 12,
    Atan = 13,
    Log10 = 14,
    Round = 15,
    Tan = 16,
    Trunc = 17,
    Sinh = 18,
    Cosh = 19,
    Tanh = 20,
    Log2 = 21,
    Log1p = 22,
    Expm1 = 23,
    Erf = 24,
    Fmod = 25,
    Atan2 = 26,
    Hypot = 27,
    Sgn = 28,
    Root = 29,
};

std::optional<NumericExpressionFunction> numeric_expression_function(std::string_view name) {
    if (name == "floor") {
        return NumericExpressionFunction::Floor;
    }
    if (name == "abs") {
        return NumericExpressionFunction::Abs;
    }
    if (name == "sqrt") {
        return NumericExpressionFunction::Sqrt;
    }
    if (name == "sin") {
        return NumericExpressionFunction::Sin;
    }
    if (name == "cos") {
        return NumericExpressionFunction::Cos;
    }
    if (name == "tan") {
        return NumericExpressionFunction::Tan;
    }
    if (name == "pow") {
        return NumericExpressionFunction::Pow;
    }
    if (name == "ceil") {
        return NumericExpressionFunction::Ceil;
    }
    if (name == "log") {
        return NumericExpressionFunction::Log;
    }
    if (name == "exp") {
        return NumericExpressionFunction::Exp;
    }
    if (name == "acos") {
        return NumericExpressionFunction::Acos;
    }
    if (name == "asin") {
        return NumericExpressionFunction::Asin;
    }
    if (name == "atan") {
        return NumericExpressionFunction::Atan;
    }
    if (name == "log10") {
        return NumericExpressionFunction::Log10;
    }
    if (name == "round") {
        return NumericExpressionFunction::Round;
    }
    if (name == "trunc") {
        return NumericExpressionFunction::Trunc;
    }
    if (name == "sinh") {
        return NumericExpressionFunction::Sinh;
    }
    if (name == "cosh") {
        return NumericExpressionFunction::Cosh;
    }
    if (name == "tanh") {
        return NumericExpressionFunction::Tanh;
    }
    if (name == "log2") {
        return NumericExpressionFunction::Log2;
    }
    if (name == "log1p") {
        return NumericExpressionFunction::Log1p;
    }
    if (name == "expm1") {
        return NumericExpressionFunction::Expm1;
    }
    if (name == "erf") {
        return NumericExpressionFunction::Erf;
    }
    if (name == "sgn") {
        return NumericExpressionFunction::Sgn;
    }
    if (name == "min") {
        return NumericExpressionFunction::Min;
    }
    if (name == "max") {
        return NumericExpressionFunction::Max;
    }
    if (name == "fmod") {
        return NumericExpressionFunction::Fmod;
    }
    if (name == "atan2") {
        return NumericExpressionFunction::Atan2;
    }
    if (name == "hypot") {
        return NumericExpressionFunction::Hypot;
    }
    if (name == "root") {
        return NumericExpressionFunction::Root;
    }
    return std::nullopt;
}

bool is_binary_numeric_expression_function(NumericExpressionFunction function) {
    return function == NumericExpressionFunction::Pow
        || function == NumericExpressionFunction::Min
        || function == NumericExpressionFunction::Max
        || function == NumericExpressionFunction::Fmod
        || function == NumericExpressionFunction::Atan2
        || function == NumericExpressionFunction::Hypot
        || function == NumericExpressionFunction::Root;
}

struct NumericExpressionNode {
    enum class Kind {
        Constant,
        Variable,
        Binary,
        Function,
    };

    Kind kind = Kind::Constant;
    double constant = 0.0;
    std::size_t variable_index = 0;
    char binary_op = 0;
    NumericExpressionFunction function = NumericExpressionFunction::Floor;
    std::shared_ptr<NumericExpressionNode> left;
    std::shared_ptr<NumericExpressionNode> right;
};

struct NumericExpressionSpec {
    std::string key;
    CompareOp op = CompareOp::None;
    std::string expression;
    std::shared_ptr<NumericExpressionNode> root;
    std::vector<std::string> variables;
};

std::optional<NumericExpressionSpec> parse_numeric_expression_predicate(ParsedConstraint const& constraint);

struct NumericLiteralPredicateSpec {
    std::string key;
    CompareOp op = CompareOp::None;
    ConstraintValue literal;
};

enum class ValueListPredicateKind {
    Numeric,
    String,
};

struct ValueListPredicateSpec {
    std::string key;
    CompareOp op = CompareOp::None;
    ValueListPredicateKind kind = ValueListPredicateKind::Numeric;
    std::vector<ConstraintValue> values;
};

struct EvalExpressionSpec {
    std::string key;
    std::string expression;
    struct Node {
        enum class Kind {
            Compare,
            And,
            Or,
        };
        Kind kind = Kind::Compare;
        CompareOp op = CompareOp::None;
        std::shared_ptr<NumericExpressionNode> lhs;
        std::shared_ptr<NumericExpressionNode> rhs;
        std::shared_ptr<Node> left;
        std::shared_ptr<Node> right;
    };
    std::shared_ptr<Node> root;
    std::vector<std::string> variables;
};

bool supported_numeric_compare_op(CompareOp op) {
    return op == CompareOp::EQ || op == CompareOp::NE || op == CompareOp::GT || op == CompareOp::LT
        || op == CompareOp::GE || op == CompareOp::LE;
}

bool supported_compare_predicate_op(CompareOp op) {
    return supported_numeric_compare_op(op);
}

bool supported_temporal_predicate_op(TemporalOp op, std::int64_t window_ms) {
    switch (op) {
        case TemporalOp::After:
        case TemporalOp::Before:
        case TemporalOp::Coincides:
        case TemporalOp::During:
            return true;
        case TemporalOp::Within:
            return window_ms >= 0;
        case TemporalOp::None:
            return false;
    }
    return false;
}

bool complex_runtime_operator(CompareOp op) {
    return op == CompareOp::Contains
        || op == CompareOp::NotContains
        || op == CompareOp::Matches
        || op == CompareOp::NotMatches
        || op == CompareOp::StartsWith
        || op == CompareOp::EndsWith
        || op == CompareOp::LengthIs
        || op == CompareOp::MemberOf
        || op == CompareOp::NotMemberOf
        || op == CompareOp::In
        || op == CompareOp::NotIn
        || op == CompareOp::ContainsKey
        || op == CompareOp::NotContainsKey;
}

bool supported_string_runtime_operator(CompareOp op) {
    return op == CompareOp::Contains
        || op == CompareOp::NotContains
        || op == CompareOp::Matches
        || op == CompareOp::NotMatches
        || op == CompareOp::StartsWith
        || op == CompareOp::EndsWith
        || op == CompareOp::LengthIs;
}

bool supported_string_runtime_literal(CompareOp op, ConstraintValue const& literal) {
    if (op == CompareOp::LengthIs) {
        return std::holds_alternative<int64_t>(literal) || std::holds_alternative<double>(literal);
    }
    if (!std::holds_alternative<std::string>(literal)) {
        return false;
    }
    if (op == CompareOp::Matches || op == CompareOp::NotMatches) {
        try {
            std::regex re(std::get<std::string>(literal));
            (void)re;
        } catch (std::regex_error const&) {
            return false;
        }
    }
    return true;
}

char const* lowering_reason_for_complex_operator(ParsedConstraint const& constraint,
                                                 std::optional<FieldType> left_field_type) {
    switch (constraint.op) {
        case CompareOp::Contains:
        case CompareOp::NotContains:
            (void)left_field_type;
            return "collection_helper_unlowered";
        case CompareOp::MemberOf:
        case CompareOp::NotMemberOf:
        case CompareOp::In:
        case CompareOp::NotIn:
            return "collection_helper_unlowered";
        case CompareOp::ContainsKey:
        case CompareOp::NotContainsKey:
            return "map_helper_unlowered";
        case CompareOp::Matches:
        case CompareOp::NotMatches:
            if (constraint.right_literal && std::holds_alternative<std::string>(*constraint.right_literal)
                && !supported_string_runtime_literal(constraint.op, *constraint.right_literal)) {
                return "regex_literal_invalid";
            }
            return "dynamic_regex_unlowered";
        case CompareOp::StartsWith:
        case CompareOp::EndsWith:
        case CompareOp::LengthIs:
            return "string_helper_unlowered";
        default:
            return "unsupported_operator";
    }
}

char const* lowering_reason_for_pattern_type(PatternType type) {
    switch (type) {
        case PatternType::EVAL: return "eval_pattern_unlowered";
        case PatternType::QUERY_CALL: return nullptr;
        case PatternType::FORALL: return "forall_pattern_unlowered";
        case PatternType::NOT:
        case PatternType::EXISTS:
        case PatternType::STANDARD: return nullptr;
    }
    return nullptr;
}

FieldTypeByFact build_field_type_index(std::vector<ParsedDeclaration> const& declarations) {
    FieldTypeByFact result;
    for (auto const& declaration : declarations) {
        auto& fields = result[declaration.type_name];
        for (auto const& field : declaration.fields) {
            fields[field.name] = FieldInfo{field.type, field.type_params};
        }
    }
    return result;
}

FieldInfo const* lookup_field_info(FieldTypeByFact const& field_types,
                                   std::string const& fact_type,
                                   std::string const& field_name) {
    auto fact_it = field_types.find(fact_type);
    if (fact_it == field_types.end()) {
        return nullptr;
    }
    auto field_it = fact_it->second.find(field_name);
    if (field_it == fact_it->second.end()) {
        return nullptr;
    }
    return &field_it->second;
}

FieldInfo const* lookup_first_path_field_info(FieldTypeByFact const& field_types,
                                              std::string const& fact_type,
                                              std::vector<PathSegment> const& path) {
    if (path.empty() || path.front().name.empty()) {
        return nullptr;
    }
    return lookup_field_info(field_types, fact_type, path.front().name);
}

std::string root_field_name(std::string const& field_name) {
    auto const end = field_name.find_first_of(".[!");
    if (end == std::string::npos) {
        return field_name;
    }
    return field_name.substr(0, end);
}

std::optional<FieldType> lookup_field_type(FieldTypeByFact const& field_types,
                                           std::string const& fact_type,
                                           std::string const& field_name) {
    auto const* field_info = lookup_field_info(field_types, fact_type, field_name);
    if (field_info == nullptr) {
        return std::nullopt;
    }
    return field_info->type;
}

bool is_declared_string_field(std::optional<FieldType> type) {
    return type.has_value() && (*type == FT_String);
}

bool is_declared_numeric_field(std::optional<FieldType> type) {
    if (!type.has_value()) {
        return false;
    }
    switch (*type) {
        case FT_Int:
        case FT_Long:
        case FT_Double:
        case FT_Float:
        case FT_Number:
            return true;
        default:
            return false;
    }
}

bool is_declared_mir_scalar_field(std::optional<FieldType> type) {
    if (!type.has_value()) {
        return false;
    }
    switch (*type) {
        case FT_String:
        case FT_Int:
        case FT_Long:
        case FT_Double:
        case FT_Float:
        case FT_Number:
        case FT_Boolean:
            return true;
        default:
            return false;
    }
}

bool is_declared_map_field(std::optional<FieldType> type) {
    return type.has_value() && *type == FT_Map;
}

bool is_declared_collection_field(std::optional<FieldType> type) {
    return type.has_value() && (*type == FT_List || *type == FT_Set);
}

bool is_scalar_type_parameter(TypeParameter const& type) {
    if (type.nested) {
        return false;
    }
    switch (type.base_type) {
        case FT_String:
        case FT_Int:
        case FT_Long:
        case FT_Double:
        case FT_Float:
        case FT_Number:
        case FT_Boolean:
            return true;
        default:
            return false;
    }
}

bool is_declared_scalar_collection_field(FieldInfo const* field_info) {
    if (field_info == nullptr || !is_declared_collection_field(field_info->type)) {
        return false;
    }
    return field_info->type_params.size() == 1 && is_scalar_type_parameter(field_info->type_params.front());
}

bool is_declared_fact_list_field(FieldInfo const* field_info) {
    return field_info != nullptr
        && field_info->type == FT_Object
        && field_info->type_params.size() == 1
        && field_info->type_params.front().base_type == FT_Object
        && field_info->type_params.front().custom_type == "FactList";
}

bool is_fact_list_projection(FieldInfo const* first_field_info,
                             std::vector<PathSegment> const& path,
                             std::string const& field_name) {
    return is_declared_fact_list_field(first_field_info)
        && (path.size() > 1 || field_name.find('.') != std::string::npos);
}

enum class MirCompareFieldKind {
    None,
    Scalar,
    List,
    Set,
    Map,
    FactList,
};

MirCompareFieldKind mir_compare_field_kind(FieldInfo const* field_info, std::optional<FieldType> type) {
    if (is_declared_mir_scalar_field(type)) {
        return MirCompareFieldKind::Scalar;
    }
    if (field_info != nullptr) {
        if (is_declared_fact_list_field(field_info)) {
            return MirCompareFieldKind::FactList;
        }
        type = field_info->type;
    }
    if (!type.has_value()) {
        return MirCompareFieldKind::None;
    }
    switch (*type) {
        case FT_List:
            return MirCompareFieldKind::List;
        case FT_Set:
            return MirCompareFieldKind::Set;
        case FT_Map:
            return MirCompareFieldKind::Map;
        default:
            return MirCompareFieldKind::None;
    }
}

bool is_mir_compare_value_supported(ConstraintValue const& value) {
    return std::holds_alternative<int64_t>(value)
        || std::holds_alternative<double>(value)
        || std::holds_alternative<std::string>(value)
        || std::holds_alternative<NilValue>(value);
}

bool is_mir_compare_literal_supported(ConstraintValue const& value, MirCompareFieldKind field_kind) {
    if (std::holds_alternative<NilValue>(value)) {
        return true;
    }
    switch (field_kind) {
        case MirCompareFieldKind::Scalar:
            return is_mir_compare_value_supported(value);
        case MirCompareFieldKind::List:
            return std::holds_alternative<std::shared_ptr<TypedList>>(value);
        case MirCompareFieldKind::Set:
            return std::holds_alternative<std::shared_ptr<ValueSet>>(value);
        case MirCompareFieldKind::Map:
            return std::holds_alternative<std::shared_ptr<ValueMap>>(value);
        case MirCompareFieldKind::FactList:
            return std::holds_alternative<FactList>(value);
        case MirCompareFieldKind::None:
            return false;
    }
    return false;
}

bool is_mir_map_key_supported(ConstraintValue const& value) {
    return std::holds_alternative<int64_t>(value)
        || std::holds_alternative<double>(value)
        || std::holds_alternative<std::string>(value)
        || std::holds_alternative<NilValue>(value)
        || std::holds_alternative<FactList>(value)
        || std::holds_alternative<std::shared_ptr<TypedList>>(value)
        || std::holds_alternative<std::shared_ptr<ValueSet>>(value)
        || std::holds_alternative<std::shared_ptr<ValueMap>>(value);
}

std::optional<FieldType> lookup_bound_field_type(FieldTypeByFact const& field_types,
                                                 BindingFactTypes const& binding_fact_types,
                                                 std::pair<std::string, std::string> const& bound_field);

FieldInfo const* lookup_bound_field_info(FieldTypeByFact const& field_types,
                                         BindingFactTypes const& binding_fact_types,
                                         std::pair<std::string, std::string> const& bound_field);

bool supported_compare_constraint(ParsedConstraint const& constraint,
                                  FieldInfo const* left_field_info,
                                  std::optional<FieldType> left_field_type,
                                  FieldTypeByFact const& field_types,
                                  BindingFactTypes const& binding_fact_types) {
    if (!supported_compare_predicate_op(constraint.op)) {
        return false;
    }
    auto const left_kind = mir_compare_field_kind(left_field_info, left_field_type);
    if (left_kind == MirCompareFieldKind::None) {
        return false;
    }
    bool const equality_op = constraint.op == CompareOp::EQ || constraint.op == CompareOp::NE;
    if (constraint.right_literal) {
        if (!equality_op && left_kind != MirCompareFieldKind::Scalar) {
            return false;
        }
        if (std::holds_alternative<NilValue>(*constraint.right_literal) && !equality_op) {
            return false;
        }
        return is_mir_compare_literal_supported(*constraint.right_literal, left_kind);
    }
    if (constraint.right_bound_field) {
        auto const right_field_type = lookup_bound_field_type(
            field_types,
            binding_fact_types,
            *constraint.right_bound_field);
        auto const* right_field_info = lookup_bound_field_info(
            field_types,
            binding_fact_types,
            *constraint.right_bound_field);
        auto const right_kind = mir_compare_field_kind(right_field_info, right_field_type);
        if (left_kind == MirCompareFieldKind::Scalar) {
            return right_kind == MirCompareFieldKind::Scalar;
        }
        return equality_op && left_kind == right_kind;
    }
    if (left_kind != MirCompareFieldKind::Scalar) {
        return false;
    }
    if (constraint.right_arith_expr) {
        return parse_numeric_expression_predicate(constraint).has_value();
    }
    return true;
}

std::optional<FieldType> lookup_bound_field_type(FieldTypeByFact const& field_types,
                                                 BindingFactTypes const& binding_fact_types,
                                                 std::pair<std::string, std::string> const& bound_field) {
    if (bound_field.second == "this") {
        return FT_Long;
    }
    auto binding_it = binding_fact_types.find(bound_field.first);
    if (binding_it == binding_fact_types.end()) {
        return std::nullopt;
    }
    return lookup_field_type(field_types, binding_it->second, bound_field.second);
}

FieldInfo const* lookup_bound_field_info(FieldTypeByFact const& field_types,
                                         BindingFactTypes const& binding_fact_types,
                                         std::pair<std::string, std::string> const& bound_field) {
    if (bound_field.second == "this") {
        return nullptr;
    }
    auto binding_it = binding_fact_types.find(bound_field.first);
    if (binding_it == binding_fact_types.end()) {
        return nullptr;
    }
    return lookup_field_info(field_types, binding_it->second, bound_field.second);
}

FieldInfo const* lookup_bound_first_path_field_info(
    FieldTypeByFact const& field_types,
    BindingFactTypes const& binding_fact_types,
    std::pair<std::string, std::string> const& bound_field,
    std::vector<PathSegment> const& path) {
    auto binding_it = binding_fact_types.find(bound_field.first);
    if (binding_it == binding_fact_types.end()) {
        return nullptr;
    }
    if (path.empty()) {
        auto const root = root_field_name(bound_field.second);
        if (!root.empty() && root != bound_field.second) {
            return lookup_field_info(field_types, binding_it->second, root);
        }
    }
    return lookup_first_path_field_info(field_types, binding_it->second, path);
}

bool is_declared_mir_map_key_field(FieldInfo const* field_info) {
    if (field_info == nullptr) {
        return false;
    }
    if (is_declared_mir_scalar_field(field_info->type)) {
        return true;
    }
    return field_info->type == FT_List
        || field_info->type == FT_Set
        || field_info->type == FT_Map
        || is_declared_fact_list_field(field_info);
}

bool supported_map_key_constraint(ParsedConstraint const& constraint,
                                  std::optional<FieldType> left_field_type,
                                  FieldTypeByFact const& field_types,
                                  BindingFactTypes const& binding_fact_types) {
    if ((constraint.op != CompareOp::ContainsKey && constraint.op != CompareOp::NotContainsKey)
        || !is_declared_map_field(left_field_type)) {
        return false;
    }
    if (constraint.right_literal && is_mir_map_key_supported(*constraint.right_literal)) {
        return true;
    }
    if (constraint.right_bound_field) {
        auto const* right_field_info = lookup_bound_field_info(
            field_types,
            binding_fact_types,
            *constraint.right_bound_field);
        return is_declared_mir_map_key_field(right_field_info);
    }
    return false;
}

bool supported_collection_constraint(ParsedConstraint const& constraint,
                                     FieldInfo const* left_field_info,
                                     FieldTypeByFact const& field_types,
                                     BindingFactTypes const& binding_fact_types) {
    if (constraint.op == CompareOp::Contains || constraint.op == CompareOp::NotContains) {
        if (is_declared_fact_list_field(left_field_info)) {
            if (is_fact_list_projection(left_field_info, constraint.cached_left_field_path, constraint.left_field)) {
                if (constraint.right_literal) {
                    return is_mir_compare_value_supported(*constraint.right_literal);
                }
                if (constraint.right_bound_field) {
                    auto const right_field_type = lookup_bound_field_type(
                        field_types,
                        binding_fact_types,
                        *constraint.right_bound_field);
                    return is_declared_mir_scalar_field(right_field_type);
                }
                return false;
            }
            if (constraint.right_literal) {
                return std::holds_alternative<std::string>(*constraint.right_literal);
            }
            if (constraint.right_bound_field) {
                auto const right_field_type = lookup_bound_field_type(
                    field_types,
                    binding_fact_types,
                    *constraint.right_bound_field);
                return is_declared_string_field(right_field_type);
            }
            return false;
        }
        if (!is_declared_scalar_collection_field(left_field_info)) {
            return false;
        }
        if (constraint.right_literal && is_mir_compare_value_supported(*constraint.right_literal)) {
            return true;
        }
        if (constraint.right_bound_field) {
            auto const right_field_type = lookup_bound_field_type(
                field_types,
                binding_fact_types,
                *constraint.right_bound_field);
            return is_declared_mir_scalar_field(right_field_type);
        }
        return false;
    }

    if (constraint.op == CompareOp::MemberOf || constraint.op == CompareOp::NotMemberOf) {
        if (is_declared_string_field(left_field_info ? std::optional<FieldType>{left_field_info->type}
                                                     : std::nullopt)
            && constraint.right_bound_field) {
            auto const* right_field_info = lookup_bound_field_info(
                field_types,
                binding_fact_types,
                *constraint.right_bound_field);
            if (is_declared_fact_list_field(right_field_info)) {
                return true;
            }
        }
        if (!is_declared_mir_scalar_field(left_field_info ? std::optional<FieldType>{left_field_info->type}
                                                         : std::nullopt)
            || !constraint.right_bound_field) {
            return false;
        }
        auto const* right_field_info = lookup_bound_field_info(
            field_types,
            binding_fact_types,
            *constraint.right_bound_field);
        if (right_field_info == nullptr) {
            right_field_info = lookup_bound_first_path_field_info(
                field_types,
                binding_fact_types,
                *constraint.right_bound_field,
                constraint.cached_right_field_path);
            if (is_fact_list_projection(right_field_info,
                                        constraint.cached_right_field_path,
                                        constraint.right_bound_field->second)) {
                return true;
            }
        }
        return is_declared_scalar_collection_field(right_field_info);
    }

    return false;
}

bool supported_string_runtime_rhs(ParsedConstraint const& constraint,
                                  FieldTypeByFact const& field_types,
                                  BindingFactTypes const& binding_fact_types) {
    if (constraint.right_literal) {
        return supported_string_runtime_literal(constraint.op, *constraint.right_literal);
    }
    if (!constraint.right_bound_field) {
        return false;
    }
    auto const right_field_type = lookup_bound_field_type(
        field_types,
        binding_fact_types,
        *constraint.right_bound_field);
    if (constraint.op == CompareOp::LengthIs) {
        return is_declared_numeric_field(right_field_type);
    }
    return is_declared_string_field(right_field_type);
}

bool supported_value_list_constraint(ParsedConstraint const& constraint) {
    if ((constraint.op != CompareOp::In && constraint.op != CompareOp::NotIn)
        || !constraint.right_value_list.has_value()) {
        return false;
    }
    if (constraint.right_value_list->empty()) {
        return false;
    }
    return std::all_of(
        constraint.right_value_list->begin(),
        constraint.right_value_list->end(),
        [](ConstraintValue const& value) {
            return is_mir_compare_value_supported(value);
        });
}

std::optional<NumericExpressionSpec> parse_numeric_value_expression(std::string const& expression);
std::string normalize_expression_whitespace(std::string const& value);

bool looks_like_accumulate_expression(std::string const& field) {
    return field.find_first_of("+-*/(") != std::string::npos;
}

bool supported_accumulate_kernel(ParsedAccumulate const& accumulate) {
    if (accumulate.uses_mir_value_expression) {
        return (accumulate.function == "sum"
                || accumulate.function == "average"
                || accumulate.function == "min"
                || accumulate.function == "max")
            && parse_numeric_value_expression(accumulate.field).has_value();
    }
    if (looks_like_accumulate_expression(accumulate.field)) {
        return false;
    }
    if (accumulate.function == "count") {
        return true;
    }
    if (accumulate.function == "sum") {
        return !accumulate.accumulate_field_name.empty();
    }
    if (accumulate.function == "average"
        || accumulate.function == "min"
        || accumulate.function == "max") {
        return !accumulate.accumulate_field_name.empty();
    }
    if (accumulate.function == "collect"
        || accumulate.function == "collectList"
        || accumulate.function == "collectSet") {
        return true;
    }
    return false;
}

std::optional<std::size_t> numeric_compare_op_index(CompareOp op) {
    switch (op) {
        case CompareOp::EQ: return 0;
        case CompareOp::NE: return 1;
        case CompareOp::GT: return 2;
        case CompareOp::LT: return 3;
        case CompareOp::GE: return 4;
        case CompareOp::LE: return 5;
        default: return std::nullopt;
    }
}

std::optional<std::size_t> string_compare_op_index(CompareOp op) {
    return numeric_compare_op_index(op);
}

std::uint64_t double_bits(double value) {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(value));
    return bits;
}

std::string numeric_literal_key(CompareOp op, ConstraintValue const& literal) {
    if (std::holds_alternative<int64_t>(literal)) {
        return "i64:" + std::to_string(static_cast<int>(op)) + ":" + std::to_string(std::get<int64_t>(literal));
    }
    if (std::holds_alternative<double>(literal)) {
        return "double:" + std::to_string(static_cast<int>(op)) + ":" + std::to_string(double_bits(std::get<double>(literal)));
    }
    return {};
}

std::string numeric_expression_key(CompareOp op, std::string const& expression) {
    return std::to_string(static_cast<int>(op)) + ":" + expression;
}

std::optional<ValueListPredicateKind> value_list_kind(std::vector<ConstraintValue> const& values) {
    if (values.empty()) {
        return std::nullopt;
    }
    bool const all_numeric = std::all_of(
        values.begin(),
        values.end(),
        [](ConstraintValue const& value) {
            return std::holds_alternative<int64_t>(value) || std::holds_alternative<double>(value);
        });
    if (all_numeric) {
        return ValueListPredicateKind::Numeric;
    }
    bool const all_string = std::all_of(
        values.begin(),
        values.end(),
        [](ConstraintValue const& value) {
            return std::holds_alternative<std::string>(value);
        });
    if (all_string) {
        return ValueListPredicateKind::String;
    }
    return std::nullopt;
}

std::string value_list_key(CompareOp op, std::vector<ConstraintValue> const& values) {
    auto kind = value_list_kind(values);
    if (!kind || (op != CompareOp::In && op != CompareOp::NotIn)) {
        return {};
    }
    std::ostringstream out;
    out << static_cast<int>(op) << ":";
    if (*kind == ValueListPredicateKind::Numeric) {
        out << "num:";
        for (auto const& value : values) {
            double const numeric_value = std::holds_alternative<int64_t>(value)
                ? static_cast<double>(std::get<int64_t>(value))
                : std::get<double>(value);
            out << double_bits(numeric_value) << ";";
        }
    } else {
        out << "str:";
        for (auto const& value : values) {
            auto const& text = std::get<std::string>(value);
            out << text.size() << ":" << text << ";";
        }
    }
    return out.str();
}

class NumericExpressionParser {
public:
    explicit NumericExpressionParser(std::string const& input) : input_(input) {}

    std::optional<NumericExpressionSpec> parse_value() {
        auto root = parse_expression();
        skip_ws();
        if (!root || pos_ != input_.size()) {
            return std::nullopt;
        }
        NumericExpressionSpec spec;
        spec.key = normalize_expression_whitespace(input_);
        spec.expression = input_;
        spec.root = std::move(root);
        spec.variables = std::move(variables_);
        return spec;
    }

    std::optional<NumericExpressionSpec> parse(CompareOp op) {
        if (!supported_numeric_compare_op(op)) {
            return std::nullopt;
        }
        auto root = parse_expression();
        skip_ws();
        if (!root || pos_ != input_.size()) {
            return std::nullopt;
        }
        NumericExpressionSpec spec;
        spec.key = numeric_expression_key(op, input_);
        spec.op = op;
        spec.expression = input_;
        spec.root = std::move(root);
        spec.variables = std::move(variables_);
        return spec;
    }

private:
    void skip_ws() {
        while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_]))) {
            ++pos_;
        }
    }

    std::shared_ptr<NumericExpressionNode> parse_expression() {
        auto lhs = parse_term();
        if (!lhs) return nullptr;
        while (true) {
            skip_ws();
            if (pos_ >= input_.size() || (input_[pos_] != '+' && input_[pos_] != '-')) {
                return lhs;
            }
            char const op = input_[pos_++];
            auto rhs = parse_term();
            if (!rhs) return nullptr;
            lhs = make_binary(op, std::move(lhs), std::move(rhs));
        }
    }

    std::shared_ptr<NumericExpressionNode> parse_term() {
        auto lhs = parse_factor();
        if (!lhs) return nullptr;
        while (true) {
            skip_ws();
            if (pos_ >= input_.size() || (input_[pos_] != '*' && input_[pos_] != '/')) {
                return lhs;
            }
            char const op = input_[pos_++];
            auto rhs = parse_factor();
            if (!rhs) return nullptr;
            lhs = make_binary(op, std::move(lhs), std::move(rhs));
        }
    }

    std::shared_ptr<NumericExpressionNode> parse_factor() {
        skip_ws();
        if (pos_ >= input_.size()) return nullptr;
        if (input_[pos_] == '(') {
            ++pos_;
            auto node = parse_expression();
            skip_ws();
            if (pos_ >= input_.size() || input_[pos_] != ')') {
                return nullptr;
            }
            ++pos_;
            return node;
        }
        if (input_[pos_] == '+') {
            ++pos_;
            return parse_factor();
        }
        if (input_[pos_] == '-') {
            ++pos_;
            auto rhs = parse_factor();
            if (!rhs) return nullptr;
            return make_binary('-', make_constant(0.0), std::move(rhs));
        }
        if (input_[pos_] == '$') {
            return parse_variable();
        }
        if (std::isalpha(static_cast<unsigned char>(input_[pos_])) || input_[pos_] == '_') {
            return parse_function();
        }
        if (std::isdigit(static_cast<unsigned char>(input_[pos_])) || input_[pos_] == '.') {
            return parse_number();
        }
        return nullptr;
    }

    std::shared_ptr<NumericExpressionNode> parse_number() {
        char const* begin = input_.c_str() + pos_;
        char* end = nullptr;
        double const value = std::strtod(begin, &end);
        if (end == begin) {
            return nullptr;
        }
        pos_ = static_cast<std::size_t>(end - input_.c_str());
        return make_constant(value);
    }

    std::shared_ptr<NumericExpressionNode> parse_variable() {
        std::size_t start = pos_;
        ++pos_;
        skip_ws();
        std::string name = "$";
        if (!parse_identifier_segment(name)) {
            return nullptr;
        }
        while (true) {
            std::size_t checkpoint = pos_;
            skip_ws();
            if (pos_ >= input_.size() || input_[pos_] != '.') {
                pos_ = checkpoint;
                break;
            }
            ++pos_;
            skip_ws();
            name.push_back('.');
            if (!parse_identifier_segment(name)) {
                pos_ = start;
                return nullptr;
            }
        }

        auto it = std::find(variables_.begin(), variables_.end(), name);
        std::size_t variable_index = 0;
        if (it == variables_.end()) {
            variable_index = variables_.size();
            variables_.push_back(name);
        } else {
            variable_index = static_cast<std::size_t>(std::distance(variables_.begin(), it));
        }

        auto node = std::make_shared<NumericExpressionNode>();
        node->kind = NumericExpressionNode::Kind::Variable;
        node->variable_index = variable_index;
        return node;
    }

    std::shared_ptr<NumericExpressionNode> parse_function() {
        std::size_t const start = pos_;
        std::string name;
        if (!parse_identifier_segment(name)) {
            return nullptr;
        }
        auto function = numeric_expression_function(name);
        if (!function) {
            pos_ = start;
            return nullptr;
        }
        skip_ws();
        if (pos_ >= input_.size() || input_[pos_] != '(') {
            pos_ = start;
            return nullptr;
        }
        ++pos_;
        auto lhs = parse_expression();
        skip_ws();
        if (!lhs) {
            pos_ = start;
            return nullptr;
        }
        if (is_binary_numeric_expression_function(*function)) {
            if (pos_ >= input_.size() || input_[pos_] != ',') {
                pos_ = start;
                return nullptr;
            }
            ++pos_;
            auto rhs = parse_expression();
            skip_ws();
            if (!rhs || pos_ >= input_.size() || input_[pos_] != ')') {
                pos_ = start;
                return nullptr;
            }
            ++pos_;
            return make_function(*function, std::move(lhs), std::move(rhs));
        }
        if (pos_ >= input_.size() || input_[pos_] != ')') {
            pos_ = start;
            return nullptr;
        }
        ++pos_;
        return make_function(*function, std::move(lhs), nullptr);
    }

    bool parse_identifier_segment(std::string& out) {
        std::size_t start = pos_;
        while (pos_ < input_.size()) {
            unsigned char const ch = static_cast<unsigned char>(input_[pos_]);
            if (!std::isalnum(ch) && input_[pos_] != '_') {
                break;
            }
            ++pos_;
        }
        if (pos_ == start) {
            return false;
        }
        out.append(input_, start, pos_ - start);
        return true;
    }

    static std::shared_ptr<NumericExpressionNode> make_constant(double value) {
        auto node = std::make_shared<NumericExpressionNode>();
        node->kind = NumericExpressionNode::Kind::Constant;
        node->constant = value;
        return node;
    }

    static std::shared_ptr<NumericExpressionNode> make_binary(char op,
                                                             std::shared_ptr<NumericExpressionNode> lhs,
                                                             std::shared_ptr<NumericExpressionNode> rhs) {
        auto node = std::make_shared<NumericExpressionNode>();
        node->kind = NumericExpressionNode::Kind::Binary;
        node->binary_op = op;
        node->left = std::move(lhs);
        node->right = std::move(rhs);
        return node;
    }

    static std::shared_ptr<NumericExpressionNode> make_function(NumericExpressionFunction function,
                                                               std::shared_ptr<NumericExpressionNode> lhs,
                                                               std::shared_ptr<NumericExpressionNode> rhs) {
        auto node = std::make_shared<NumericExpressionNode>();
        node->kind = NumericExpressionNode::Kind::Function;
        node->function = function;
        node->left = std::move(lhs);
        node->right = std::move(rhs);
        return node;
    }

    std::string const& input_;
    std::size_t pos_ = 0;
    std::vector<std::string> variables_;
};

class SharedNumericExpressionParser {
public:
    SharedNumericExpressionParser(std::string const& input, std::vector<std::string>& variables) :
        input_(input), variables_(variables) {}

    std::shared_ptr<NumericExpressionNode> parse() {
        auto root = parse_expression();
        skip_ws();
        if (!root || pos_ != input_.size()) {
            return nullptr;
        }
        return root;
    }

private:
    void skip_ws() {
        while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_]))) {
            ++pos_;
        }
    }

    std::shared_ptr<NumericExpressionNode> parse_expression() {
        auto lhs = parse_term();
        if (!lhs) return nullptr;
        while (true) {
            skip_ws();
            if (pos_ >= input_.size() || (input_[pos_] != '+' && input_[pos_] != '-')) {
                return lhs;
            }
            char const op = input_[pos_++];
            auto rhs = parse_term();
            if (!rhs) return nullptr;
            lhs = make_binary(op, std::move(lhs), std::move(rhs));
        }
    }

    std::shared_ptr<NumericExpressionNode> parse_term() {
        auto lhs = parse_factor();
        if (!lhs) return nullptr;
        while (true) {
            skip_ws();
            if (pos_ >= input_.size() || (input_[pos_] != '*' && input_[pos_] != '/')) {
                return lhs;
            }
            char const op = input_[pos_++];
            auto rhs = parse_factor();
            if (!rhs) return nullptr;
            lhs = make_binary(op, std::move(lhs), std::move(rhs));
        }
    }

    std::shared_ptr<NumericExpressionNode> parse_factor() {
        skip_ws();
        if (pos_ >= input_.size()) return nullptr;
        if (input_[pos_] == '(') {
            ++pos_;
            auto node = parse_expression();
            skip_ws();
            if (pos_ >= input_.size() || input_[pos_] != ')') {
                return nullptr;
            }
            ++pos_;
            return node;
        }
        if (input_[pos_] == '+') {
            ++pos_;
            return parse_factor();
        }
        if (input_[pos_] == '-') {
            ++pos_;
            auto rhs = parse_factor();
            if (!rhs) return nullptr;
            return make_binary('-', make_constant(0.0), std::move(rhs));
        }
        if (input_[pos_] == '$') {
            return parse_variable();
        }
        if (std::isalpha(static_cast<unsigned char>(input_[pos_])) || input_[pos_] == '_') {
            return parse_function();
        }
        if (std::isdigit(static_cast<unsigned char>(input_[pos_])) || input_[pos_] == '.') {
            return parse_number();
        }
        return nullptr;
    }

    std::shared_ptr<NumericExpressionNode> parse_number() {
        char const* begin = input_.c_str() + pos_;
        char* end = nullptr;
        double const value = std::strtod(begin, &end);
        if (end == begin) {
            return nullptr;
        }
        pos_ = static_cast<std::size_t>(end - input_.c_str());
        return make_constant(value);
    }

    std::shared_ptr<NumericExpressionNode> parse_variable() {
        std::size_t start = pos_;
        ++pos_;
        skip_ws();
        std::string name = "$";
        if (!parse_identifier_segment(name)) {
            return nullptr;
        }
        while (true) {
            std::size_t checkpoint = pos_;
            skip_ws();
            if (pos_ >= input_.size() || input_[pos_] != '.') {
                pos_ = checkpoint;
                break;
            }
            ++pos_;
            skip_ws();
            name.push_back('.');
            if (!parse_identifier_segment(name)) {
                pos_ = start;
                return nullptr;
            }
        }

        auto it = std::find(variables_.begin(), variables_.end(), name);
        std::size_t variable_index = 0;
        if (it == variables_.end()) {
            variable_index = variables_.size();
            variables_.push_back(name);
        } else {
            variable_index = static_cast<std::size_t>(std::distance(variables_.begin(), it));
        }

        auto node = std::make_shared<NumericExpressionNode>();
        node->kind = NumericExpressionNode::Kind::Variable;
        node->variable_index = variable_index;
        return node;
    }

    std::shared_ptr<NumericExpressionNode> parse_function() {
        std::size_t const start = pos_;
        std::string name;
        if (!parse_identifier_segment(name)) {
            return nullptr;
        }
        auto function = numeric_expression_function(name);
        if (!function) {
            pos_ = start;
            return nullptr;
        }
        skip_ws();
        if (pos_ >= input_.size() || input_[pos_] != '(') {
            pos_ = start;
            return nullptr;
        }
        ++pos_;
        auto lhs = parse_expression();
        skip_ws();
        if (!lhs) {
            pos_ = start;
            return nullptr;
        }
        if (is_binary_numeric_expression_function(*function)) {
            if (pos_ >= input_.size() || input_[pos_] != ',') {
                pos_ = start;
                return nullptr;
            }
            ++pos_;
            auto rhs = parse_expression();
            skip_ws();
            if (!rhs || pos_ >= input_.size() || input_[pos_] != ')') {
                pos_ = start;
                return nullptr;
            }
            ++pos_;
            return make_function(*function, std::move(lhs), std::move(rhs));
        }
        if (pos_ >= input_.size() || input_[pos_] != ')') {
            pos_ = start;
            return nullptr;
        }
        ++pos_;
        return make_function(*function, std::move(lhs), nullptr);
    }

    bool parse_identifier_segment(std::string& out) {
        std::size_t start = pos_;
        while (pos_ < input_.size()) {
            unsigned char const ch = static_cast<unsigned char>(input_[pos_]);
            if (!std::isalnum(ch) && input_[pos_] != '_') {
                break;
            }
            ++pos_;
        }
        if (pos_ == start) {
            return false;
        }
        out.append(input_, start, pos_ - start);
        return true;
    }

    static std::shared_ptr<NumericExpressionNode> make_constant(double value) {
        auto node = std::make_shared<NumericExpressionNode>();
        node->kind = NumericExpressionNode::Kind::Constant;
        node->constant = value;
        return node;
    }

    static std::shared_ptr<NumericExpressionNode> make_binary(char op,
                                                             std::shared_ptr<NumericExpressionNode> lhs,
                                                             std::shared_ptr<NumericExpressionNode> rhs) {
        auto node = std::make_shared<NumericExpressionNode>();
        node->kind = NumericExpressionNode::Kind::Binary;
        node->binary_op = op;
        node->left = std::move(lhs);
        node->right = std::move(rhs);
        return node;
    }

    static std::shared_ptr<NumericExpressionNode> make_function(NumericExpressionFunction function,
                                                               std::shared_ptr<NumericExpressionNode> lhs,
                                                               std::shared_ptr<NumericExpressionNode> rhs) {
        auto node = std::make_shared<NumericExpressionNode>();
        node->kind = NumericExpressionNode::Kind::Function;
        node->function = function;
        node->left = std::move(lhs);
        node->right = std::move(rhs);
        return node;
    }

    std::string const& input_;
    std::vector<std::string>& variables_;
    std::size_t pos_ = 0;
};

std::optional<CompareOp> parse_eval_compare_op(std::string const& input,
                                               std::size_t& op_pos,
                                               std::size_t& op_len) {
    int paren_depth = 0;
    for (std::size_t index = 0; index < input.size(); ++index) {
        char const ch = input[index];
        if (ch == '(') {
            ++paren_depth;
            continue;
        }
        if (ch == ')') {
            --paren_depth;
            if (paren_depth < 0) return std::nullopt;
            continue;
        }
        if (paren_depth != 0) {
            continue;
        }
        if (index + 1 < input.size()) {
            std::string_view two(input.data() + index, 2);
            if (two == "==") {
                op_pos = index;
                op_len = 2;
                return CompareOp::EQ;
            }
            if (two == "!=") {
                op_pos = index;
                op_len = 2;
                return CompareOp::NE;
            }
            if (two == ">=") {
                op_pos = index;
                op_len = 2;
                return CompareOp::GE;
            }
            if (two == "<=") {
                op_pos = index;
                op_len = 2;
                return CompareOp::LE;
            }
        }
        if (ch == '>') {
            op_pos = index;
            op_len = 1;
            return CompareOp::GT;
        }
        if (ch == '<') {
            op_pos = index;
            op_len = 1;
            return CompareOp::LT;
        }
    }
    return std::nullopt;
}

std::string trim_copy(std::string const& input) {
    std::size_t begin = 0;
    while (begin < input.size() && std::isspace(static_cast<unsigned char>(input[begin]))) {
        ++begin;
    }
    std::size_t end = input.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1]))) {
        --end;
    }
    return input.substr(begin, end - begin);
}

std::string normalize_expression_whitespace(std::string const& value) {
    std::string normalized;
    normalized.reserve(value.size());
    bool pending_space = false;
    for (char const ch : value) {
        if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
            pending_space = !normalized.empty();
            continue;
        }
        if (pending_space) {
            normalized.push_back(' ');
            pending_space = false;
        }
        normalized.push_back(ch);
    }
    return trim_copy(normalized);
}

bool is_external_eval_expression(std::string const& expression) {
    auto call = parse_external_eval_call(expression);
    if (!call) {
        return false;
    }
    for (auto const& argument : call->arguments) {
        if (argument.kind == ExternalEvalArgumentKind::NumericExpression
            && !parse_numeric_value_expression(argument.text)) {
            return false;
        }
    }
    return true;
}

bool wraps_entire_expression(std::string const& input) {
    if (input.size() < 2 || input.front() != '(' || input.back() != ')') {
        return false;
    }
    int depth = 0;
    for (std::size_t index = 0; index < input.size(); ++index) {
        char const ch = input[index];
        if (ch == '(') {
            ++depth;
        } else if (ch == ')') {
            --depth;
            if (depth < 0) {
                return false;
            }
            if (depth == 0 && index + 1 < input.size()) {
                return false;
            }
        }
    }
    return depth == 0;
}

std::string strip_outer_parentheses(std::string input) {
    input = trim_copy(input);
    while (wraps_entire_expression(input)) {
        input = trim_copy(input.substr(1, input.size() - 2));
    }
    return input;
}

std::optional<std::size_t> find_top_level_token(std::string const& input, std::string_view token) {
    if (token.empty() || input.size() < token.size()) {
        return std::nullopt;
    }
    int depth = 0;
    for (std::size_t index = input.size() - token.size() + 1; index-- > 0;) {
        char const ch = input[index];
        if (ch == ')') {
            ++depth;
        } else if (ch == '(') {
            --depth;
            if (depth < 0) {
                return std::nullopt;
            }
        }
        if (depth == 0 && input.compare(index, token.size(), token) == 0) {
            return index;
        }
        if (index == 0) {
            break;
        }
    }
    return std::nullopt;
}

std::shared_ptr<EvalExpressionSpec::Node> parse_eval_bool_node(
    std::string const& expression,
    std::vector<std::string>& variables,
    bool negated = false) {
    std::string text = strip_outer_parentheses(expression);
    if (text.empty()) {
        return nullptr;
    }
    while (!text.empty() && text.front() == '!') {
        negated = !negated;
        text = strip_outer_parentheses(text.substr(1));
    }
    if (text.empty()) {
        return nullptr;
    }

    if (auto pos = find_top_level_token(text, "||")) {
        auto lhs = parse_eval_bool_node(text.substr(0, *pos), variables, negated);
        auto rhs = parse_eval_bool_node(text.substr(*pos + 2), variables, negated);
        if (!lhs || !rhs) return nullptr;
        auto node = std::make_shared<EvalExpressionSpec::Node>();
        node->kind = negated ? EvalExpressionSpec::Node::Kind::And : EvalExpressionSpec::Node::Kind::Or;
        node->left = std::move(lhs);
        node->right = std::move(rhs);
        return node;
    }
    if (auto pos = find_top_level_token(text, "&&")) {
        auto lhs = parse_eval_bool_node(text.substr(0, *pos), variables, negated);
        auto rhs = parse_eval_bool_node(text.substr(*pos + 2), variables, negated);
        if (!lhs || !rhs) return nullptr;
        auto node = std::make_shared<EvalExpressionSpec::Node>();
        node->kind = negated ? EvalExpressionSpec::Node::Kind::Or : EvalExpressionSpec::Node::Kind::And;
        node->left = std::move(lhs);
        node->right = std::move(rhs);
        return node;
    }
    if (text.front() == '!') {
        return nullptr;
    }

    std::size_t op_pos = 0;
    std::size_t op_len = 0;
    auto op = parse_eval_compare_op(text, op_pos, op_len);
    if (!op || !supported_numeric_compare_op(*op)) {
        return nullptr;
    }
    if (negated) {
        op = negate_compare_op(*op);
    }

    std::string lhs_text = trim_copy(text.substr(0, op_pos));
    std::string rhs_text = trim_copy(text.substr(op_pos + op_len));
    if (lhs_text.empty() || rhs_text.empty()) {
        return nullptr;
    }

    SharedNumericExpressionParser lhs_parser(lhs_text, variables);
    auto lhs = lhs_parser.parse();
    SharedNumericExpressionParser rhs_parser(rhs_text, variables);
    auto rhs = rhs_parser.parse();
    if (!lhs || !rhs) {
        return nullptr;
    }
    auto node = std::make_shared<EvalExpressionSpec::Node>();
    node->kind = EvalExpressionSpec::Node::Kind::Compare;
    node->op = *op;
    node->lhs = std::move(lhs);
    node->rhs = std::move(rhs);
    return node;
}

std::optional<NumericExpressionSpec> parse_numeric_expression_predicate(ParsedConstraint const& constraint) {
    if (!constraint.right_arith_expr) {
        return std::nullopt;
    }
    return NumericExpressionParser(*constraint.right_arith_expr).parse(constraint.op);
}

std::optional<NumericExpressionSpec> parse_numeric_value_expression(std::string const& expression) {
    return NumericExpressionParser(expression).parse_value();
}

std::optional<EvalExpressionSpec> parse_eval_expression_predicate(std::string const& expression) {
    EvalExpressionSpec spec;
    spec.key = expression;
    spec.expression = expression;
    spec.root = parse_eval_bool_node(expression, spec.variables);
    if (!spec.root) {
        return std::nullopt;
    }
    return spec;
}

void collect_external_eval_numeric_value_expressions(std::string const& expression,
                                                     std::vector<NumericExpressionSpec>& out,
                                                     std::unordered_set<std::string>& seen) {
    auto call = parse_external_eval_call(expression);
    if (!call) {
        return;
    }
    for (auto const& argument : call->arguments) {
        if (argument.kind != ExternalEvalArgumentKind::NumericExpression) {
            continue;
        }
        if (auto spec = parse_numeric_value_expression(argument.text)) {
            if (seen.insert(spec->key).second) {
                out.push_back(std::move(*spec));
            }
        }
    }
}

std::unique_ptr<MirExecutionPlan> mir_compile_failure(std::string* error_out, char const* reason) {
    if (error_out != nullptr) {
        *error_out = reason;
    }
    return nullptr;
}

void collect_numeric_literal_predicates(ConstraintNode const* node,
                                        std::vector<NumericLiteralPredicateSpec>& out,
                                        std::unordered_set<std::string>& seen) {
    if (node == nullptr) return;
    if (node->type == NodeType::LEAF) {
        ParsedConstraint const& constraint = node->constraint;
        if (constraint.right_literal && supported_numeric_compare_op(constraint.op)
            && (std::holds_alternative<int64_t>(*constraint.right_literal)
                || std::holds_alternative<double>(*constraint.right_literal))) {
            std::string key = numeric_literal_key(constraint.op, *constraint.right_literal);
            if (!key.empty() && seen.insert(key).second) {
                out.push_back(NumericLiteralPredicateSpec{std::move(key), constraint.op, *constraint.right_literal});
            }
        }
    }
    for (auto const& child : node->children) {
        collect_numeric_literal_predicates(child.get(), out, seen);
    }
}

void collect_numeric_expression_predicates(ConstraintNode const* node,
                                           std::vector<NumericExpressionSpec>& out,
                                           std::unordered_set<std::string>& seen) {
    if (node == nullptr) return;
    if (node->type == NodeType::LEAF) {
        if (node->constraint.right_arith_expr) {
            auto spec = parse_numeric_expression_predicate(node->constraint);
            if (spec && seen.insert(spec->key).second) {
                out.push_back(std::move(*spec));
            }
        }
    }
    for (auto const& child : node->children) {
        collect_numeric_expression_predicates(child.get(), out, seen);
    }
}

void collect_value_list_predicates(ConstraintNode const* node,
                                   std::vector<ValueListPredicateSpec>& out,
                                   std::unordered_set<std::string>& seen) {
    if (node == nullptr) return;
    if (node->type == NodeType::LEAF) {
        ParsedConstraint const& constraint = node->constraint;
        if (supported_value_list_constraint(constraint)) {
            auto kind = value_list_kind(*constraint.right_value_list);
            if (kind) {
                std::string key = value_list_key(constraint.op, *constraint.right_value_list);
                if (!key.empty() && seen.insert(key).second) {
                out.push_back(ValueListPredicateSpec{
                    std::move(key),
                    constraint.op,
                    *kind,
                    *constraint.right_value_list});
                }
            }
        }
    }
    for (auto const& child : node->children) {
        collect_value_list_predicates(child.get(), out, seen);
    }
}

void collect_numeric_literal_predicates(ParsedPattern const& pattern,
                                        std::vector<NumericLiteralPredicateSpec>& out,
                                        std::unordered_set<std::string>& seen);

void collect_numeric_expression_predicates(ParsedPattern const& pattern,
                                           std::vector<NumericExpressionSpec>& out,
                                           std::unordered_set<std::string>& seen);

void collect_numeric_value_expressions(ParsedPattern const& pattern,
                                       std::vector<NumericExpressionSpec>& out,
                                       std::unordered_set<std::string>& seen);

void collect_value_list_predicates(ParsedPattern const& pattern,
                                   std::vector<ValueListPredicateSpec>& out,
                                   std::unordered_set<std::string>& seen);

void collect_eval_expression_predicates(ParsedPattern const& pattern,
                                        std::vector<EvalExpressionSpec>& out,
                                        std::unordered_set<std::string>& seen);

void collect_numeric_literal_predicates(PatternSource const& source,
                                        std::vector<NumericLiteralPredicateSpec>& out,
                                        std::unordered_set<std::string>& seen) {
    if (auto const* accumulate = std::get_if<ParsedAccumulate>(&source)) {
        if (accumulate->source_pattern) {
            collect_numeric_literal_predicates(*accumulate->source_pattern, out, seen);
        }
    }
}

void collect_numeric_expression_predicates(PatternSource const& source,
                                           std::vector<NumericExpressionSpec>& out,
                                           std::unordered_set<std::string>& seen) {
    if (auto const* accumulate = std::get_if<ParsedAccumulate>(&source)) {
        if (accumulate->source_pattern) {
            collect_numeric_expression_predicates(*accumulate->source_pattern, out, seen);
        }
    }
}

void collect_numeric_value_expressions(PatternSource const& source,
                                       std::vector<NumericExpressionSpec>& out,
                                       std::unordered_set<std::string>& seen) {
    if (auto const* accumulate = std::get_if<ParsedAccumulate>(&source)) {
        if (accumulate->uses_mir_value_expression) {
            if (auto spec = parse_numeric_value_expression(accumulate->field)) {
                if (seen.insert(spec->key).second) {
                    out.push_back(std::move(*spec));
                }
            }
        }
        if (accumulate->source_pattern) {
            collect_numeric_value_expressions(*accumulate->source_pattern, out, seen);
        }
    }
}

void collect_value_list_predicates(PatternSource const& source,
                                   std::vector<ValueListPredicateSpec>& out,
                                   std::unordered_set<std::string>& seen) {
    if (auto const* accumulate = std::get_if<ParsedAccumulate>(&source)) {
        if (accumulate->source_pattern) {
            collect_value_list_predicates(*accumulate->source_pattern, out, seen);
        }
    }
}

void collect_eval_expression_predicates(PatternSource const& source,
                                        std::vector<EvalExpressionSpec>& out,
                                        std::unordered_set<std::string>& seen) {
    if (auto const* accumulate = std::get_if<ParsedAccumulate>(&source)) {
        if (accumulate->source_pattern) {
            collect_eval_expression_predicates(*accumulate->source_pattern, out, seen);
        }
    }
}

void collect_numeric_literal_predicates(ParsedPattern const& pattern,
                                        std::vector<NumericLiteralPredicateSpec>& out,
                                        std::unordered_set<std::string>& seen) {
    collect_numeric_literal_predicates(pattern.constraint_root.get(), out, seen);
    collect_numeric_literal_predicates(pattern.source, out, seen);
    if (pattern.forall_info) {
        for (auto const& nested : pattern.forall_info->patterns) {
            collect_numeric_literal_predicates(nested, out, seen);
        }
    }
    for (auto const& nested : pattern.nested_patterns) {
        collect_numeric_literal_predicates(nested, out, seen);
    }
}

void collect_numeric_expression_predicates(ParsedPattern const& pattern,
                                           std::vector<NumericExpressionSpec>& out,
                                           std::unordered_set<std::string>& seen) {
    collect_numeric_expression_predicates(pattern.constraint_root.get(), out, seen);
    collect_numeric_expression_predicates(pattern.source, out, seen);
    if (pattern.forall_info) {
        for (auto const& nested : pattern.forall_info->patterns) {
            collect_numeric_expression_predicates(nested, out, seen);
        }
    }
    for (auto const& nested : pattern.nested_patterns) {
        collect_numeric_expression_predicates(nested, out, seen);
    }
}

void collect_numeric_value_expressions(ParsedPattern const& pattern,
                                       std::vector<NumericExpressionSpec>& out,
                                       std::unordered_set<std::string>& seen) {
    collect_numeric_value_expressions(pattern.source, out, seen);
    if (pattern.type == PatternType::EVAL && pattern.eval_expression) {
        collect_external_eval_numeric_value_expressions(*pattern.eval_expression, out, seen);
    }
    if (pattern.forall_info) {
        for (auto const& nested : pattern.forall_info->patterns) {
            collect_numeric_value_expressions(nested, out, seen);
        }
    }
    for (auto const& nested : pattern.nested_patterns) {
        collect_numeric_value_expressions(nested, out, seen);
    }
}

void collect_value_list_predicates(ParsedPattern const& pattern,
                                   std::vector<ValueListPredicateSpec>& out,
                                   std::unordered_set<std::string>& seen) {
    collect_value_list_predicates(pattern.constraint_root.get(), out, seen);
    collect_value_list_predicates(pattern.source, out, seen);
    if (pattern.forall_info) {
        for (auto const& nested : pattern.forall_info->patterns) {
            collect_value_list_predicates(nested, out, seen);
        }
    }
    for (auto const& nested : pattern.nested_patterns) {
        collect_value_list_predicates(nested, out, seen);
    }
}

void collect_eval_expression_predicates(ParsedPattern const& pattern,
                                        std::vector<EvalExpressionSpec>& out,
                                        std::unordered_set<std::string>& seen) {
    if (pattern.type == PatternType::EVAL && pattern.eval_expression) {
        auto spec = parse_eval_expression_predicate(*pattern.eval_expression);
        if (spec && seen.insert(spec->key).second) {
            out.push_back(std::move(*spec));
        }
    }
    collect_eval_expression_predicates(pattern.source, out, seen);
    if (pattern.forall_info) {
        for (auto const& nested : pattern.forall_info->patterns) {
            collect_eval_expression_predicates(nested, out, seen);
        }
    }
    for (auto const& nested : pattern.nested_patterns) {
        collect_eval_expression_predicates(nested, out, seen);
    }
}

std::vector<NumericLiteralPredicateSpec>
collect_numeric_literal_predicates(std::vector<ParsedRule> const& rules,
                                   std::vector<ParsedQuery> const& queries) {
    std::vector<NumericLiteralPredicateSpec> specs;
    std::unordered_set<std::string> seen;
    for (auto const& rule : rules) {
        for (auto const& group : rule.condition_groups) {
            for (auto const& pattern : group) {
                collect_numeric_literal_predicates(pattern, specs, seen);
            }
        }
    }
    for (auto const& query : queries) {
        for (auto const& pattern : query.patterns) {
            collect_numeric_literal_predicates(pattern, specs, seen);
        }
    }
    return specs;
}

std::vector<NumericExpressionSpec>
collect_numeric_expression_predicates(std::vector<ParsedRule> const& rules,
                                      std::vector<ParsedQuery> const& queries) {
    std::vector<NumericExpressionSpec> specs;
    std::unordered_set<std::string> seen;
    for (auto const& rule : rules) {
        for (auto const& group : rule.condition_groups) {
            for (auto const& pattern : group) {
                collect_numeric_expression_predicates(pattern, specs, seen);
            }
        }
    }
    for (auto const& query : queries) {
        for (auto const& pattern : query.patterns) {
            collect_numeric_expression_predicates(pattern, specs, seen);
        }
    }
    return specs;
}

std::vector<NumericExpressionSpec>
collect_numeric_value_expressions(std::vector<ParsedRule> const& rules) {
    std::vector<NumericExpressionSpec> specs;
    std::unordered_set<std::string> seen;
    for (auto const& rule : rules) {
        for (auto const& group : rule.condition_groups) {
            for (auto const& pattern : group) {
                collect_numeric_value_expressions(pattern, specs, seen);
            }
        }
    }
    return specs;
}

std::vector<ValueListPredicateSpec>
collect_value_list_predicates(std::vector<ParsedRule> const& rules,
                              std::vector<ParsedQuery> const& queries) {
    std::vector<ValueListPredicateSpec> specs;
    std::unordered_set<std::string> seen;
    for (auto const& rule : rules) {
        for (auto const& group : rule.condition_groups) {
            for (auto const& pattern : group) {
                collect_value_list_predicates(pattern, specs, seen);
            }
        }
    }
    for (auto const& query : queries) {
        for (auto const& pattern : query.patterns) {
            collect_value_list_predicates(pattern, specs, seen);
        }
    }
    return specs;
}

std::vector<EvalExpressionSpec>
collect_eval_expression_predicates(std::vector<ParsedRule> const& rules,
                                   std::vector<ParsedQuery> const& queries) {
    std::vector<EvalExpressionSpec> specs;
    std::unordered_set<std::string> seen;
    for (auto const& rule : rules) {
        for (auto const& group : rule.condition_groups) {
            for (auto const& pattern : group) {
                collect_eval_expression_predicates(pattern, specs, seen);
            }
        }
    }
    for (auto const& query : queries) {
        for (auto const& pattern : query.patterns) {
            collect_eval_expression_predicates(pattern, specs, seen);
        }
    }
    return specs;
}

void add_unique_reason(MirRuleCoverage& coverage, char const* reason) {
    if (std::find(coverage.lowering_errors.begin(), coverage.lowering_errors.end(), reason)
        == coverage.lowering_errors.end()) {
        coverage.lowering_errors.emplace_back(reason);
    }
}

void collect_rule_coverage(ConstraintNode const* node,
                           std::string const& fact_type,
                           FieldTypeByFact const& field_types,
                           BindingFactTypes const& binding_fact_types,
                           MirRuleCoverage& coverage) {
    if (node == nullptr) return;
    if (node->type == NodeType::LEAF) {
        ParsedConstraint const& constraint = node->constraint;
        ++coverage.constraint_count;
        bool fully_covered = false;
        auto const* left_field_info = lookup_field_info(field_types, fact_type, constraint.left_field);
        if (left_field_info == nullptr) {
            left_field_info = lookup_first_path_field_info(
                field_types,
                fact_type,
                constraint.cached_left_field_path);
        }
        if (left_field_info == nullptr) {
            auto const dot_pos = constraint.left_field.find('.');
            if (dot_pos != std::string::npos) {
                left_field_info = lookup_field_info(field_types, fact_type, constraint.left_field.substr(0, dot_pos));
            }
        }
        auto const left_field_type = lookup_field_type(field_types, fact_type, constraint.left_field);

        if (constraint.temporal_constraint) {
            if (supported_temporal_predicate_op(constraint.temporal_constraint->op,
                                                constraint.temporal_constraint->window_ms)) {
                ++coverage.compare_predicate_count;
                fully_covered = true;
            } else {
                add_unique_reason(coverage, "temporal_unlowered");
            }
        } else if (supported_compare_constraint(
                       constraint,
                       left_field_info,
                       left_field_type,
                       field_types,
                       binding_fact_types)) {
            ++coverage.compare_predicate_count;
            fully_covered = true;
            if (constraint.right_literal
                && (std::holds_alternative<int64_t>(*constraint.right_literal)
                    || std::holds_alternative<double>(*constraint.right_literal))) {
                ++coverage.literal_predicate_count;
            }
        } else if (supported_collection_constraint(
                       constraint,
                       left_field_info,
                       field_types,
                       binding_fact_types)) {
            ++coverage.compare_predicate_count;
            fully_covered = true;
        } else if (supported_string_runtime_operator(constraint.op)) {
            if (is_declared_string_field(left_field_type)
                && supported_string_runtime_rhs(constraint, field_types, binding_fact_types)) {
                ++coverage.compare_predicate_count;
                fully_covered = true;
            } else {
                add_unique_reason(coverage, lowering_reason_for_complex_operator(constraint, left_field_type));
            }
        } else if (supported_value_list_constraint(constraint)
                   && is_declared_mir_scalar_field(left_field_type)) {
            ++coverage.compare_predicate_count;
            fully_covered = true;
        } else if (supported_map_key_constraint(
                       constraint,
                       left_field_type,
                       field_types,
                       binding_fact_types)) {
            ++coverage.compare_predicate_count;
            fully_covered = true;
        } else if (complex_runtime_operator(constraint.op)) {
            add_unique_reason(coverage, lowering_reason_for_complex_operator(constraint, left_field_type));
        } else if (supported_compare_predicate_op(constraint.op)) {
            add_unique_reason(coverage, "unsupported_type");
        } else if (constraint.op != CompareOp::None) {
            add_unique_reason(coverage, "unsupported_operator");
        }

        if (constraint.right_value_list && !supported_value_list_constraint(constraint)) {
            fully_covered = false;
            add_unique_reason(coverage, "collection_helper_unlowered");
        }
        if (constraint.right_arith_expr && !parse_numeric_expression_predicate(constraint).has_value()) {
            fully_covered = false;
            add_unique_reason(coverage, "expression_unlowered");
        }

        if (!fully_covered && constraint.op != CompareOp::None) {
            ++coverage.lowering_error_constraint_count;
        }
    }
    for (auto const& child : node->children) {
        collect_rule_coverage(child.get(), fact_type, field_types, binding_fact_types, coverage);
    }
}

void collect_rule_coverage(ParsedPattern const& pattern,
                           FieldTypeByFact const& field_types,
                           BindingFactTypes& binding_fact_types,
                           MirRuleCoverage& coverage);

void collect_rule_coverage(PatternSource const& source,
                           FieldTypeByFact const& field_types,
                           BindingFactTypes& binding_fact_types,
                           MirRuleCoverage& coverage) {
    if (auto const* accumulate = std::get_if<ParsedAccumulate>(&source)) {
        if (accumulate->source_pattern) {
            collect_rule_coverage(*accumulate->source_pattern, field_types, binding_fact_types, coverage);
        }
        if (!supported_accumulate_kernel(*accumulate)) {
            add_unique_reason(coverage, "accumulate_unlowered");
        }
    }
}

void collect_rule_coverage(ParsedPattern const& pattern,
                           FieldTypeByFact const& field_types,
                           BindingFactTypes& binding_fact_types,
                           MirRuleCoverage& coverage) {
    if (!pattern.binding.empty() && !pattern.fact_type.empty()) {
        binding_fact_types[pattern.binding] = pattern.fact_type;
    }

    collect_rule_coverage(pattern.constraint_root.get(),
                          pattern.fact_type,
                          field_types,
                          binding_fact_types,
                          coverage);
    collect_rule_coverage(pattern.source, field_types, binding_fact_types, coverage);
    if (pattern.type == PatternType::EVAL
        && pattern.eval_expression
        && (parse_eval_expression_predicate(*pattern.eval_expression).has_value()
            || is_external_eval_expression(*pattern.eval_expression))) {
        ++coverage.compare_predicate_count;
    } else if (auto const* reason = lowering_reason_for_pattern_type(pattern.type)) {
        add_unique_reason(coverage, reason);
    }
    if (pattern.forall_info) {
        for (auto const& nested : pattern.forall_info->patterns) {
            BindingFactTypes nested_binding_fact_types = binding_fact_types;
            collect_rule_coverage(nested, field_types, nested_binding_fact_types, coverage);
        }
    }
    for (auto const& nested : pattern.nested_patterns) {
        BindingFactTypes nested_binding_fact_types = binding_fact_types;
        collect_rule_coverage(nested, field_types, nested_binding_fact_types, coverage);
    }
}

std::vector<MirRuleCoverage> collect_rule_coverages(std::vector<ParsedRule> const& rules,
                                                    std::vector<ParsedDeclaration> const& declarations) {
    auto const field_types = build_field_type_index(declarations);
    std::vector<MirRuleCoverage> coverages;
    coverages.reserve(rules.size());
    for (std::size_t rule_index = 0; rule_index < rules.size(); ++rule_index) {
        auto const& rule = rules[rule_index];
        MirRuleCoverage coverage;
        coverage.rule_index = rule_index;
        coverage.rule_name = rule.name;
        for (auto const& group : rule.condition_groups) {
            BindingFactTypes binding_fact_types;
            for (auto const& pattern : group) {
                collect_rule_coverage(pattern, field_types, binding_fact_types, coverage);
            }
        }
        coverages.push_back(std::move(coverage));
    }
    return coverages;
}

std::vector<MirQueryCoverage> collect_query_coverages(std::vector<ParsedQuery> const& queries,
                                                      std::vector<ParsedDeclaration> const& declarations) {
    auto const field_types = build_field_type_index(declarations);
    std::vector<MirQueryCoverage> coverages;
    coverages.reserve(queries.size());
    for (std::size_t query_index = 0; query_index < queries.size(); ++query_index) {
        auto const& query = queries[query_index];
        MirRuleCoverage rule_like_coverage;
        BindingFactTypes binding_fact_types;
        for (auto const& pattern : query.patterns) {
            collect_rule_coverage(pattern, field_types, binding_fact_types, rule_like_coverage);
        }

        MirQueryCoverage coverage;
        coverage.query_index = query_index;
        coverage.query_name = query.name;
        coverage.constraint_count = rule_like_coverage.constraint_count;
        coverage.compare_predicate_count = rule_like_coverage.compare_predicate_count;
        coverage.literal_predicate_count = rule_like_coverage.literal_predicate_count;
        coverage.lowering_error_constraint_count = rule_like_coverage.lowering_error_constraint_count;
        coverage.lowering_errors = std::move(rule_like_coverage.lowering_errors);
        coverages.push_back(std::move(coverage));
    }
    return coverages;
}

void collect_query_graph_constraints(ConstraintNode const* node,
                                     std::vector<ParsedConstraint const*>& out) {
    if (node == nullptr) {
        return;
    }
    if (node->type == NodeType::LEAF) {
        out.push_back(&node->constraint);
        return;
    }
    for (auto const& child : node->children) {
        collect_query_graph_constraints(child.get(), out);
    }
}

void collect_graph_inline_bindings(ConstraintNode const* node,
                                   std::string const& fact_type,
                                   BindingFactTypes& binding_fact_types) {
    if (node == nullptr || fact_type.empty()) {
        return;
    }
    if (node->type == NodeType::LEAF) {
        if (node->constraint.field_binding) {
            binding_fact_types[*node->constraint.field_binding] = fact_type;
        }
        return;
    }
    for (auto const& child : node->children) {
        collect_graph_inline_bindings(child.get(), fact_type, binding_fact_types);
    }
}

std::optional<std::size_t> find_predicate_id(std::unordered_map<std::string, std::size_t> const& ids,
                                             std::string const& key) {
    auto it = ids.find(key);
    if (it == ids.end()) {
        return std::nullopt;
    }
    return it->second;
}

MirRuleGraphNodeKind rule_graph_node_kind(ParsedPattern const& pattern) {
    if (std::holds_alternative<ParsedAccumulate>(pattern.source)) {
        return MirRuleGraphNodeKind::Accumulate;
    }
    if (std::holds_alternative<ParsedUnnest>(pattern.source)) {
        return MirRuleGraphNodeKind::Unnest;
    }
    if (std::holds_alternative<ParsedQueryCall>(pattern.source)) {
        return MirRuleGraphNodeKind::QueryCall;
    }
    if (pattern.window_info) {
        return MirRuleGraphNodeKind::Window;
    }
    switch (pattern.type) {
        case PatternType::STANDARD: return MirRuleGraphNodeKind::Standard;
        case PatternType::NOT: return MirRuleGraphNodeKind::Not;
        case PatternType::EXISTS: return MirRuleGraphNodeKind::Exists;
        case PatternType::FORALL: return MirRuleGraphNodeKind::Forall;
        case PatternType::EVAL: return MirRuleGraphNodeKind::Eval;
        case PatternType::QUERY_CALL: return MirRuleGraphNodeKind::QueryCall;
    }
    return MirRuleGraphNodeKind::Standard;
}

char const* rule_graph_node_kind_name(MirRuleGraphNodeKind kind) {
    switch (kind) {
        case MirRuleGraphNodeKind::Standard: return "standard";
        case MirRuleGraphNodeKind::Not: return "not";
        case MirRuleGraphNodeKind::Exists: return "exists";
        case MirRuleGraphNodeKind::Forall: return "forall";
        case MirRuleGraphNodeKind::Eval: return "eval";
        case MirRuleGraphNodeKind::QueryCall: return "query_call";
        case MirRuleGraphNodeKind::Accumulate: return "accumulate";
        case MirRuleGraphNodeKind::Unnest: return "unnest";
        case MirRuleGraphNodeKind::Window: return "window";
    }
    return "standard";
}

char const* rule_graph_predicate_role_name(MirRulePredicateRole role) {
    switch (role) {
        case MirRulePredicateRole::Alpha: return "alpha";
        case MirRulePredicateRole::Join: return "join";
        case MirRulePredicateRole::Eval: return "eval";
    }
    return "alpha";
}

char const* runtime_predicate_backend_name(RuntimePredicateBackend backend) {
    switch (backend) {
        case RuntimePredicateBackend::MirJit: return "mir";
        case RuntimePredicateBackend::Native: return "native";
    }
    return "unknown";
}

std::string rule_graph_source_kind(ParsedPattern const& pattern) {
    if (std::holds_alternative<ParsedAccumulate>(pattern.source)) {
        return "accumulate";
    }
    if (std::holds_alternative<ParsedUnnest>(pattern.source)) {
        return "unnest";
    }
    if (std::holds_alternative<ParsedQueryCall>(pattern.source)) {
        return "query_call";
    }
    if (auto const* entry_point = std::get_if<std::string>(&pattern.source)) {
        return entry_point->empty() ? "default_entry" : "entry_point";
    }
    return "default_entry";
}

bool rule_graph_constraint_is_join(ParsedConstraint const& constraint) {
    return constraint.right_bound_field.has_value()
        || constraint.temporal_constraint.has_value()
        || constraint.right_arith_expr.has_value();
}

template <typename ImplT>
std::optional<MirRuntimePredicateRef>
query_graph_runtime_predicate_ref(ImplT const& impl,
                                  ParsedConstraint const& constraint,
                                  std::string const& fact_type,
                                  FieldTypeByFact const& field_types,
    BindingFactTypes const& binding_fact_types) {
    auto const* left_field_info = lookup_field_info(field_types, fact_type, constraint.left_field);
    if (left_field_info == nullptr) {
        left_field_info = lookup_first_path_field_info(
            field_types,
            fact_type,
            constraint.cached_left_field_path);
    }
    if (left_field_info == nullptr) {
        auto const dot_pos = constraint.left_field.find('.');
        if (dot_pos != std::string::npos) {
            left_field_info = lookup_field_info(field_types, fact_type, constraint.left_field.substr(0, dot_pos));
        }
    }
    auto const left_field_type = lookup_field_type(field_types, fact_type, constraint.left_field);

    if (constraint.temporal_constraint) {
        if (!supported_temporal_predicate_op(
                constraint.temporal_constraint->op,
                constraint.temporal_constraint->window_ms)) {
            return std::nullopt;
        }
        return MirRuntimePredicateRef{
            MirRuntimePredicateKind::Temporal,
            0,
            CompareOp::None,
            constraint.temporal_constraint->op,
            constraint.temporal_constraint->window_ms};
    }

    if (constraint.right_arith_expr) {
        auto predicate_id = find_predicate_id(
            impl.numeric_expression_predicate_ids,
            numeric_expression_key(constraint.op, *constraint.right_arith_expr));
        if (!predicate_id) {
            return std::nullopt;
        }
        return MirRuntimePredicateRef{MirRuntimePredicateKind::NumericExpression, *predicate_id};
    }

    if (constraint.op == CompareOp::In || constraint.op == CompareOp::NotIn) {
        if (!constraint.right_value_list || !supported_value_list_constraint(constraint)) {
            return std::nullopt;
        }
        if (auto kind = value_list_kind(*constraint.right_value_list)) {
            auto predicate_id = find_predicate_id(
                impl.value_list_predicate_ids,
                value_list_key(constraint.op, *constraint.right_value_list));
            if (predicate_id) {
                return MirRuntimePredicateRef{MirRuntimePredicateKind::ValueList, *predicate_id};
            }
        }
        return MirRuntimePredicateRef{
            MirRuntimePredicateKind::CollectionContains,
            0,
            constraint.op == CompareOp::In ? CompareOp::MemberOf : CompareOp::NotMemberOf};
    }

    if (constraint.op == CompareOp::Matches || constraint.op == CompareOp::NotMatches) {
        if (!is_declared_string_field(left_field_type)
            || !supported_string_runtime_rhs(constraint, field_types, binding_fact_types)) {
            return std::nullopt;
        }
        return MirRuntimePredicateRef{MirRuntimePredicateKind::StringMatches, 0, constraint.op};
    }
    if (constraint.op == CompareOp::StartsWith || constraint.op == CompareOp::EndsWith) {
        if (!is_declared_string_field(left_field_type)
            || !supported_string_runtime_rhs(constraint, field_types, binding_fact_types)) {
            return std::nullopt;
        }
        return MirRuntimePredicateRef{MirRuntimePredicateKind::StringAffix, 0, constraint.op};
    }
    if (constraint.op == CompareOp::LengthIs) {
        if (!is_declared_string_field(left_field_type)
            || !supported_string_runtime_rhs(constraint, field_types, binding_fact_types)) {
            return std::nullopt;
        }
        return MirRuntimePredicateRef{MirRuntimePredicateKind::StringLengthIs};
    }
    if (constraint.op == CompareOp::ContainsKey || constraint.op == CompareOp::NotContainsKey) {
        if (!supported_map_key_constraint(constraint, left_field_type, field_types, binding_fact_types)) {
            return std::nullopt;
        }
        return MirRuntimePredicateRef{MirRuntimePredicateKind::MapContainsKey, 0, constraint.op};
    }
    if (constraint.op == CompareOp::Contains || constraint.op == CompareOp::NotContains) {
        if (is_declared_string_field(left_field_type)) {
            if (!supported_string_runtime_rhs(constraint, field_types, binding_fact_types)) {
                return std::nullopt;
            }
            return MirRuntimePredicateRef{MirRuntimePredicateKind::StringContains, 0, constraint.op};
        }
        if (!supported_collection_constraint(constraint, left_field_info, field_types, binding_fact_types)) {
            return std::nullopt;
        }
        return MirRuntimePredicateRef{MirRuntimePredicateKind::CollectionContains, 0, constraint.op};
    }
    if (constraint.op == CompareOp::MemberOf || constraint.op == CompareOp::NotMemberOf) {
        if (!supported_collection_constraint(constraint, left_field_info, field_types, binding_fact_types)) {
            return std::nullopt;
        }
        return MirRuntimePredicateRef{MirRuntimePredicateKind::CollectionContains, 0, constraint.op};
    }

    if (constraint.right_literal
        && is_declared_numeric_field(left_field_type)
        && (std::holds_alternative<int64_t>(*constraint.right_literal)
            || std::holds_alternative<double>(*constraint.right_literal))) {
        auto predicate_id = find_predicate_id(
            impl.literal_predicate_ids,
            numeric_literal_key(constraint.op, *constraint.right_literal));
        if (!predicate_id) {
            return std::nullopt;
        }
        return MirRuntimePredicateRef{MirRuntimePredicateKind::NumericLiteral, *predicate_id};
    }

    if (supported_compare_constraint(
            constraint,
            left_field_info,
            left_field_type,
            field_types,
            binding_fact_types)) {
        auto predicate_id = numeric_compare_op_index(constraint.op);
        if (!predicate_id) {
            return std::nullopt;
        }
        return MirRuntimePredicateRef{MirRuntimePredicateKind::Compare, *predicate_id};
    }

    return std::nullopt;
}

template <typename ImplT>
void collect_rule_graph_pattern(ImplT const& impl,
                                ParsedPattern const& pattern,
                                std::size_t condition_group_index,
                                std::size_t pattern_index,
                                std::size_t depth,
                                std::optional<std::size_t> parent_node_index,
                                FieldTypeByFact const& field_types,
                                BindingFactTypes& binding_fact_types,
                                MirRuleGraph& graph) {
    if (!pattern.binding.empty() && !pattern.fact_type.empty()) {
        binding_fact_types[pattern.binding] = pattern.fact_type;
    }
    collect_graph_inline_bindings(pattern.constraint_root.get(), pattern.fact_type, binding_fact_types);

    MirRuleGraphNode node;
    node.condition_group_index = condition_group_index;
    node.pattern_index = pattern_index;
    node.depth = depth;
    node.parent_node_index = parent_node_index;
    node.kind = rule_graph_node_kind(pattern);
    node.source_kind = rule_graph_source_kind(pattern);
    node.binding = pattern.binding;
    node.fact_type = pattern.fact_type;

    std::vector<ParsedConstraint const*> constraints;
    collect_query_graph_constraints(pattern.constraint_root.get(), constraints);
    node.constraint_count = constraints.size();
    node.predicates.reserve(constraints.size() + (pattern.type == PatternType::EVAL ? 1 : 0));
    for (auto const* constraint : constraints) {
        auto predicate = query_graph_runtime_predicate_ref(
            impl,
            *constraint,
            pattern.fact_type,
            field_types,
            binding_fact_types);
        if (predicate) {
            node.predicates.push_back(MirRuleGraphPredicate{
                rule_graph_constraint_is_join(*constraint) ? MirRulePredicateRole::Join : MirRulePredicateRole::Alpha,
                *predicate});
        }
    }

    if (pattern.type == PatternType::EVAL && pattern.eval_expression) {
        auto predicate_id = find_predicate_id(impl.eval_expression_predicate_ids, *pattern.eval_expression);
        if (predicate_id) {
            node.predicates.push_back(MirRuleGraphPredicate{
                MirRulePredicateRole::Eval,
                MirRuntimePredicateRef{MirRuntimePredicateKind::EvalExpression, *predicate_id}});
        } else if (auto external_call = parse_external_eval_call(*pattern.eval_expression);
                   external_call && is_external_eval_expression(*pattern.eval_expression)) {
            node.predicates.push_back(MirRuleGraphPredicate{
                MirRulePredicateRole::Eval,
                RuntimePredicateRef{
                    RuntimePredicateBackend::Native,
                    MirRuntimePredicateKind::External,
                    0,
                    CompareOp::None,
                    TemporalOp::None,
                    0,
                    external_call->predicate_name}});
        }
    }

    std::size_t const current_node_index = graph.nodes.size();
    graph.nodes.push_back(std::move(node));

    if (auto const* accumulate = std::get_if<ParsedAccumulate>(&pattern.source)) {
        if (accumulate->source_pattern) {
            BindingFactTypes nested_binding_fact_types = binding_fact_types;
            collect_rule_graph_pattern(impl,
                                       *accumulate->source_pattern,
                                       condition_group_index,
                                       0,
                                       depth + 1,
                                       current_node_index,
                                       field_types,
                                       nested_binding_fact_types,
                                       graph);
        }
    }

    if (pattern.forall_info) {
        for (std::size_t nested_index = 0; nested_index < pattern.forall_info->patterns.size(); ++nested_index) {
            BindingFactTypes nested_binding_fact_types = binding_fact_types;
            collect_rule_graph_pattern(impl,
                                       pattern.forall_info->patterns[nested_index],
                                       condition_group_index,
                                       nested_index,
                                       depth + 1,
                                       current_node_index,
                                       field_types,
                                       nested_binding_fact_types,
                                       graph);
        }
    }
    for (std::size_t nested_index = 0; nested_index < pattern.nested_patterns.size(); ++nested_index) {
        BindingFactTypes nested_binding_fact_types = binding_fact_types;
        collect_rule_graph_pattern(impl,
                                   pattern.nested_patterns[nested_index],
                                   condition_group_index,
                                   nested_index,
                                   depth + 1,
                                   current_node_index,
                                   field_types,
                                   nested_binding_fact_types,
                                   graph);
    }
}

template <typename ImplT>
std::vector<MirRuleGraph> collect_rule_graphs(ImplT const& impl,
                                              std::vector<ParsedRule> const& rules,
                                              std::vector<ParsedDeclaration> const& declarations) {
    auto const field_types = build_field_type_index(declarations);
    std::vector<MirRuleGraph> graphs;
    for (std::size_t rule_index = 0; rule_index < rules.size(); ++rule_index) {
        auto const& rule = rules[rule_index];
        for (std::size_t group_index = 0; group_index < rule.condition_groups.size(); ++group_index) {
            MirRuleGraph graph;
            graph.rule_index = rule_index;
            graph.rule_name = rule.name;
            graph.condition_group_index = group_index;
            BindingFactTypes binding_fact_types;
            auto const& group = rule.condition_groups[group_index];
            graph.nodes.reserve(group.size());
            for (std::size_t pattern_index = 0; pattern_index < group.size(); ++pattern_index) {
                collect_rule_graph_pattern(impl,
                                           group[pattern_index],
                                           group_index,
                                           pattern_index,
                                           0,
                                           std::nullopt,
                                           field_types,
                                           binding_fact_types,
                                           graph);
            }
            graphs.push_back(std::move(graph));
        }
    }
    return graphs;
}

template <typename ImplT>
std::vector<MirQueryGraph> collect_query_graphs(ImplT const& impl,
                                                std::vector<ParsedQuery> const& queries,
                                                std::vector<ParsedDeclaration> const& declarations,
                                                std::string* error_out) {
    auto const field_types = build_field_type_index(declarations);
    std::vector<MirQueryGraph> graphs;
    graphs.reserve(queries.size());
    for (std::size_t query_index = 0; query_index < queries.size(); ++query_index) {
        auto const& query = queries[query_index];
        MirQueryGraph graph;
        graph.query_index = query_index;
        graph.query_name = query.name;
        graph.nodes.reserve(query.patterns.size());

        BindingFactTypes binding_fact_types;
        for (std::size_t pattern_index = 0; pattern_index < query.patterns.size(); ++pattern_index) {
            auto const& pattern = query.patterns[pattern_index];
            if (!pattern.binding.empty() && !pattern.fact_type.empty()) {
                binding_fact_types[pattern.binding] = pattern.fact_type;
            }
            collect_graph_inline_bindings(pattern.constraint_root.get(), pattern.fact_type, binding_fact_types);

            MirQueryGraphNode node;
            node.pattern_index = pattern_index;
            node.binding = pattern.binding;
            node.fact_type = pattern.fact_type;

            std::vector<ParsedConstraint const*> constraints;
            collect_query_graph_constraints(pattern.constraint_root.get(), constraints);
            node.constraint_count = constraints.size();
            node.predicates.reserve(constraints.size());
            for (auto const* constraint : constraints) {
                auto predicate = query_graph_runtime_predicate_ref(
                    impl,
                    *constraint,
                    pattern.fact_type,
                    field_types,
                    binding_fact_types);
                if (predicate) {
                    node.predicates.push_back(*predicate);
                }
            }
            graph.nodes.push_back(std::move(node));
        }
        graphs.push_back(std::move(graph));
    }
    return graphs;
}

MIR_item_t emit_constant_i64_function(MIR_context_t ctx, char const* name, std::int64_t value) {
    MIR_type_t result_type = MIR_T_I64;
    MIR_item_t func = MIR_new_func_arr(ctx, name, 1, &result_type, 0, nullptr);
    MIR_append_insn(ctx, func, MIR_new_ret_insn(ctx, 1, MIR_new_int_op(ctx, value)));
    MIR_finish_func(ctx);
    return func;
}

MIR_item_t emit_indexed_i64_function(MIR_context_t ctx,
                                     char const* name,
                                     std::vector<ParsedRule> const& rules,
                                     std::function<std::int64_t(ParsedRule const&)> value_of,
                                     std::int64_t default_value) {
    MIR_type_t result_type = MIR_T_I64;
    MIR_var_t args[] = {{MIR_T_I64, "rule_index", 0}};
    MIR_item_t func = MIR_new_func_arr(ctx, name, 1, &result_type, 1, args);
    MIR_reg_t index_reg = MIR_reg(ctx, "rule_index", func->u.func);
    std::vector<MIR_label_t> labels;
    labels.reserve(rules.size());

    for (std::size_t index = 0; index < rules.size(); ++index) {
        MIR_label_t label = MIR_new_label(ctx);
        labels.push_back(label);
        MIR_append_insn(ctx,
                        func,
                        MIR_new_insn(ctx,
                                     MIR_BEQ,
                                     MIR_new_label_op(ctx, label),
                                     MIR_new_reg_op(ctx, index_reg),
                                     MIR_new_int_op(ctx, static_cast<std::int64_t>(index))));
    }

    MIR_append_insn(ctx, func, MIR_new_ret_insn(ctx, 1, MIR_new_int_op(ctx, default_value)));

    for (std::size_t index = 0; index < rules.size(); ++index) {
        MIR_append_insn(ctx, func, labels[index]);
        MIR_append_insn(ctx, func, MIR_new_ret_insn(ctx, 1, MIR_new_int_op(ctx, value_of(rules[index]))));
    }

    MIR_finish_func(ctx);
    return func;
}

void append_compare_handler(MIR_context_t ctx,
                            MIR_item_t func,
                            MIR_reg_t lhs_reg,
                            MIR_reg_t rhs_reg,
                            MIR_label_t handler_label,
                            MIR_insn_code_t branch_code) {
    MIR_label_t true_label = MIR_new_label(ctx);
    MIR_append_insn(ctx, func, handler_label);
    MIR_append_insn(ctx,
                    func,
                    MIR_new_insn(ctx,
                                 branch_code,
                                 MIR_new_label_op(ctx, true_label),
                                 MIR_new_reg_op(ctx, lhs_reg),
                                 MIR_new_reg_op(ctx, rhs_reg)));
    MIR_append_insn(ctx, func, MIR_new_ret_insn(ctx, 1, MIR_new_int_op(ctx, 0)));
    MIR_append_insn(ctx, func, true_label);
    MIR_append_insn(ctx, func, MIR_new_ret_insn(ctx, 1, MIR_new_int_op(ctx, 1)));
}

MIR_insn_code_t i64_branch_code_for(CompareOp op) {
    switch (op) {
        case CompareOp::EQ: return MIR_BEQ;
        case CompareOp::NE: return MIR_BNE;
        case CompareOp::GT: return MIR_BGT;
        case CompareOp::LT: return MIR_BLT;
        case CompareOp::GE: return MIR_BGE;
        case CompareOp::LE: return MIR_BLE;
        default: return MIR_INVALID_INSN;
    }
}

MIR_insn_code_t double_branch_code_for(CompareOp op) {
    switch (op) {
        case CompareOp::EQ: return MIR_DBEQ;
        case CompareOp::NE: return MIR_DBNE;
        case CompareOp::GT: return MIR_DBGT;
        case CompareOp::LT: return MIR_DBLT;
        case CompareOp::GE: return MIR_DBGE;
        case CompareOp::LE: return MIR_DBLE;
        default: return MIR_INVALID_INSN;
    }
}

MIR_item_t emit_double_value_list_predicate(MIR_context_t ctx,
                                            char const* name,
                                            ValueListPredicateSpec const& spec) {
    MIR_type_t result_type = MIR_T_I64;
    MIR_var_t args[] = {{MIR_T_D, "lhs", 0}};
    MIR_item_t func = MIR_new_func_arr(ctx, name, 1, &result_type, 1, args);
    MIR_reg_t lhs_reg = MIR_reg(ctx, "lhs", func->u.func);
    MIR_label_t found_label = MIR_new_label(ctx);
    for (auto const& value : spec.values) {
        double const numeric_value = std::holds_alternative<int64_t>(value)
            ? static_cast<double>(std::get<int64_t>(value))
            : std::get<double>(value);
        MIR_append_insn(ctx,
                        func,
                        MIR_new_insn(ctx,
                                     MIR_DBEQ,
                                     MIR_new_label_op(ctx, found_label),
                                     MIR_new_reg_op(ctx, lhs_reg),
                                     MIR_new_double_op(ctx, numeric_value)));
    }
    MIR_append_insn(ctx,
                    func,
                    MIR_new_ret_insn(ctx,
                                     1,
                                     MIR_new_int_op(ctx, spec.op == CompareOp::In ? 0 : 1)));
    MIR_append_insn(ctx, func, found_label);
    MIR_append_insn(ctx,
                    func,
                    MIR_new_ret_insn(ctx,
                                     1,
                                     MIR_new_int_op(ctx, spec.op == CompareOp::In ? 1 : 0)));
    MIR_finish_func(ctx);
    return func;
}

MIR_item_t emit_string_value_list_predicate(MIR_context_t ctx,
                                            char const* name,
                                            ValueListPredicateSpec const& spec,
                                            std::vector<std::string> const& literals,
                                            MIR_item_t string_compare_proto,
                                            MIR_item_t string_compare_import) {
    MIR_type_t result_type = MIR_T_I64;
    MIR_var_t args[] = {{MIR_T_P, "lhs", 0}};
    MIR_item_t func = MIR_new_func_arr(ctx, name, 1, &result_type, 1, args);
    MIR_reg_t lhs_reg = MIR_reg(ctx, "lhs", func->u.func);
    MIR_label_t found_label = MIR_new_label(ctx);

    for (std::size_t index = 0; index < literals.size(); ++index) {
        auto const& text = literals[index];
        std::string cmp_name = "value_list_cmp_" + std::to_string(index);
        MIR_reg_t cmp_reg = MIR_new_func_reg(ctx, func->u.func, MIR_T_I64, cmp_name.c_str());
        MIR_str_t literal{static_cast<std::size_t>(text.size() + 1), text.c_str()};
        MIR_append_insn(ctx,
                        func,
                        MIR_new_call_insn(ctx,
                                          5,
                                          MIR_new_ref_op(ctx, string_compare_proto),
                                          MIR_new_ref_op(ctx, string_compare_import),
                                          MIR_new_reg_op(ctx, cmp_reg),
                                          MIR_new_reg_op(ctx, lhs_reg),
                                          MIR_new_str_op(ctx, literal)));
        MIR_append_insn(ctx,
                        func,
                        MIR_new_insn(ctx,
                                     MIR_BEQ,
                                     MIR_new_label_op(ctx, found_label),
                                     MIR_new_reg_op(ctx, cmp_reg),
                                     MIR_new_int_op(ctx, 0)));
    }

    MIR_append_insn(ctx,
                    func,
                    MIR_new_ret_insn(ctx,
                                     1,
                                     MIR_new_int_op(ctx, spec.op == CompareOp::In ? 0 : 1)));
    MIR_append_insn(ctx, func, found_label);
    MIR_append_insn(ctx,
                    func,
                    MIR_new_ret_insn(ctx,
                                     1,
                                     MIR_new_int_op(ctx, spec.op == CompareOp::In ? 1 : 0)));
    MIR_finish_func(ctx);
    return func;
}

MIR_reg_t emit_numeric_expression_value(MIR_context_t ctx,
                                        MIR_item_t func,
                                        NumericExpressionNode const& node,
                                        std::vector<MIR_reg_t> const& variable_regs,
                                        std::size_t& temp_index,
                                        MIR_item_t math_unary_proto,
                                        MIR_item_t math_unary_import,
                                        MIR_item_t math_binary_proto,
                                        MIR_item_t math_binary_import) {
    switch (node.kind) {
        case NumericExpressionNode::Kind::Constant: {
            std::string temp_name = "expr_const_" + std::to_string(temp_index++);
            MIR_reg_t reg = MIR_new_func_reg(ctx, func->u.func, MIR_T_D, temp_name.c_str());
            MIR_append_insn(ctx,
                            func,
                            MIR_new_insn(ctx,
                                         MIR_DMOV,
                                         MIR_new_reg_op(ctx, reg),
                                         MIR_new_double_op(ctx, node.constant)));
            return reg;
        }
        case NumericExpressionNode::Kind::Variable:
            return variable_regs[node.variable_index];
        case NumericExpressionNode::Kind::Function: {
            MIR_reg_t lhs = emit_numeric_expression_value(
                ctx,
                func,
                *node.left,
                variable_regs,
                temp_index,
                math_unary_proto,
                math_unary_import,
                math_binary_proto,
                math_binary_import);
            std::string temp_name = "expr_fn_" + std::to_string(temp_index++);
            MIR_reg_t result = MIR_new_func_reg(ctx, func->u.func, MIR_T_D, temp_name.c_str());
            if (is_binary_numeric_expression_function(node.function)) {
                MIR_reg_t rhs = emit_numeric_expression_value(
                    ctx,
                    func,
                    *node.right,
                    variable_regs,
                    temp_index,
                    math_unary_proto,
                    math_unary_import,
                    math_binary_proto,
                    math_binary_import);
                MIR_append_insn(ctx,
                                func,
                                MIR_new_call_insn(ctx,
                                                  6,
                                                  MIR_new_ref_op(ctx, math_binary_proto),
                                                  MIR_new_ref_op(ctx, math_binary_import),
                                                  MIR_new_reg_op(ctx, result),
                                                  MIR_new_int_op(ctx, static_cast<std::int64_t>(node.function)),
                                                  MIR_new_reg_op(ctx, lhs),
                                                  MIR_new_reg_op(ctx, rhs)));
            } else {
                MIR_append_insn(ctx,
                                func,
                                MIR_new_call_insn(ctx,
                                                  5,
                                                  MIR_new_ref_op(ctx, math_unary_proto),
                                                  MIR_new_ref_op(ctx, math_unary_import),
                                                  MIR_new_reg_op(ctx, result),
                                                  MIR_new_int_op(ctx, static_cast<std::int64_t>(node.function)),
                                                  MIR_new_reg_op(ctx, lhs)));
            }
            return result;
        }
        case NumericExpressionNode::Kind::Binary:
            break;
    }

    MIR_reg_t lhs = emit_numeric_expression_value(
        ctx,
        func,
        *node.left,
        variable_regs,
        temp_index,
        math_unary_proto,
        math_unary_import,
        math_binary_proto,
        math_binary_import);
    MIR_reg_t rhs = emit_numeric_expression_value(
        ctx,
        func,
        *node.right,
        variable_regs,
        temp_index,
        math_unary_proto,
        math_unary_import,
        math_binary_proto,
        math_binary_import);
    std::string temp_name = "expr_tmp_" + std::to_string(temp_index++);
    MIR_reg_t result = MIR_new_func_reg(ctx, func->u.func, MIR_T_D, temp_name.c_str());
    MIR_insn_code_t code = MIR_INVALID_INSN;
    switch (node.binary_op) {
        case '+': code = MIR_DADD; break;
        case '-': code = MIR_DSUB; break;
        case '*': code = MIR_DMUL; break;
        case '/': code = MIR_DDIV; break;
        default: code = MIR_INVALID_INSN; break;
    }
    MIR_append_insn(ctx,
                    func,
                    MIR_new_insn(ctx,
                                 code,
                                 MIR_new_reg_op(ctx, result),
                                 MIR_new_reg_op(ctx, lhs),
                                 MIR_new_reg_op(ctx, rhs)));
    return result;
}

void append_expression_arity_guard(MIR_context_t ctx,
                                   MIR_item_t func,
                                   MIR_reg_t arg_count_reg,
                                   MIR_reg_t args_reg,
                                   std::size_t expected_count,
                                   bool returns_double = false) {
    MIR_label_t arity_ok_label = MIR_new_label(ctx);
    MIR_append_insn(ctx,
                    func,
                    MIR_new_insn(ctx,
                                 MIR_BEQ,
                                 MIR_new_label_op(ctx, arity_ok_label),
                                 MIR_new_reg_op(ctx, arg_count_reg),
                                 MIR_new_int_op(ctx, static_cast<std::int64_t>(expected_count))));
    MIR_append_insn(ctx,
                    func,
                    MIR_new_ret_insn(ctx,
                                     1,
                                     returns_double ? MIR_new_double_op(ctx, 0.0) : MIR_new_int_op(ctx, 0)));
    MIR_append_insn(ctx, func, arity_ok_label);

    if (expected_count == 0) {
        return;
    }

    MIR_label_t args_ok_label = MIR_new_label(ctx);
    MIR_append_insn(ctx,
                    func,
                    MIR_new_insn(ctx,
                                 MIR_BNE,
                                 MIR_new_label_op(ctx, args_ok_label),
                                 MIR_new_reg_op(ctx, args_reg),
                                 MIR_new_int_op(ctx, 0)));
    MIR_append_insn(ctx,
                    func,
                    MIR_new_ret_insn(ctx,
                                     1,
                                     returns_double ? MIR_new_double_op(ctx, 0.0) : MIR_new_int_op(ctx, 0)));
    MIR_append_insn(ctx, func, args_ok_label);
}

std::vector<MIR_reg_t> emit_expression_variable_loads(MIR_context_t ctx,
                                                      MIR_item_t func,
                                                      MIR_reg_t args_reg,
                                                      std::size_t variable_count,
                                                      std::size_t& temp_index) {
    std::vector<MIR_reg_t> variable_regs;
    variable_regs.reserve(variable_count);
    for (std::size_t index = 0; index < variable_count; ++index) {
        std::string name = "expr_arg_" + std::to_string(index);
        MIR_reg_t value_reg = MIR_new_func_reg(ctx, func->u.func, MIR_T_D, name.c_str());
        MIR_append_insn(ctx,
                        func,
                        MIR_new_insn(ctx,
                                     MIR_DMOV,
                                     MIR_new_reg_op(ctx, value_reg),
                                     MIR_new_mem_op(ctx,
                                                    MIR_T_D,
                                                    static_cast<MIR_disp_t>(index * sizeof(double)),
                                                    args_reg,
                                                    0,
                                                    1)));
        variable_regs.push_back(value_reg);
        ++temp_index;
    }
    return variable_regs;
}

MIR_item_t emit_numeric_expression_predicate(MIR_context_t ctx,
                                             char const* name,
                                             NumericExpressionSpec const& spec,
                                             MIR_item_t math_unary_proto,
                                             MIR_item_t math_unary_import,
                                             MIR_item_t math_binary_proto,
                                             MIR_item_t math_binary_import) {
    MIR_type_t result_type = MIR_T_I64;
    MIR_var_t args[] = {
        {MIR_T_D, "lhs", 0},
        {MIR_T_I64, "arg_count", 0},
        {MIR_T_P, "args", 0},
    };
    MIR_item_t func = MIR_new_func_arr(ctx, name, 1, &result_type, 3, args);
    MIR_reg_t lhs_reg = MIR_reg(ctx, "lhs", func->u.func);
    MIR_reg_t arg_count_reg = MIR_reg(ctx, "arg_count", func->u.func);
    MIR_reg_t args_reg = MIR_reg(ctx, "args", func->u.func);
    std::size_t temp_index = 0;
    append_expression_arity_guard(ctx, func, arg_count_reg, args_reg, spec.variables.size());
    std::vector<MIR_reg_t> variable_regs =
        emit_expression_variable_loads(ctx, func, args_reg, spec.variables.size(), temp_index);
    MIR_reg_t rhs_reg = emit_numeric_expression_value(
        ctx,
        func,
        *spec.root,
        variable_regs,
        temp_index,
        math_unary_proto,
        math_unary_import,
        math_binary_proto,
        math_binary_import);
    append_compare_handler(ctx, func, lhs_reg, rhs_reg, MIR_new_label(ctx), double_branch_code_for(spec.op));
    MIR_finish_func(ctx);
    return func;
}

MIR_item_t emit_numeric_value_expression(MIR_context_t ctx,
                                         char const* name,
                                         NumericExpressionSpec const& spec,
                                         MIR_item_t math_unary_proto,
                                         MIR_item_t math_unary_import,
                                         MIR_item_t math_binary_proto,
                                         MIR_item_t math_binary_import) {
    MIR_type_t result_type = MIR_T_D;
    MIR_var_t args[] = {
        {MIR_T_I64, "arg_count", 0},
        {MIR_T_P, "args", 0},
    };
    MIR_item_t func = MIR_new_func_arr(ctx, name, 1, &result_type, 2, args);
    MIR_reg_t arg_count_reg = MIR_reg(ctx, "arg_count", func->u.func);
    MIR_reg_t args_reg = MIR_reg(ctx, "args", func->u.func);
    std::size_t temp_index = 0;
    append_expression_arity_guard(ctx, func, arg_count_reg, args_reg, spec.variables.size(), true);
    std::vector<MIR_reg_t> variable_regs =
        emit_expression_variable_loads(ctx, func, args_reg, spec.variables.size(), temp_index);
    MIR_reg_t result_reg = emit_numeric_expression_value(
        ctx,
        func,
        *spec.root,
        variable_regs,
        temp_index,
        math_unary_proto,
        math_unary_import,
        math_binary_proto,
        math_binary_import);
    MIR_append_insn(ctx, func, MIR_new_ret_insn(ctx, 1, MIR_new_reg_op(ctx, result_reg)));
    MIR_finish_func(ctx);
    return func;
}

void emit_eval_expression_branch(MIR_context_t ctx,
                                 MIR_item_t func,
                                 EvalExpressionSpec::Node const& node,
                                 std::vector<MIR_reg_t> const& variable_regs,
                                 MIR_label_t true_label,
                                 MIR_label_t false_label,
                                 std::size_t& temp_index,
                                 MIR_item_t math_unary_proto,
                                 MIR_item_t math_unary_import,
                                 MIR_item_t math_binary_proto,
                                 MIR_item_t math_binary_import) {
    switch (node.kind) {
        case EvalExpressionSpec::Node::Kind::Compare: {
            MIR_reg_t lhs_reg = emit_numeric_expression_value(
                ctx,
                func,
                *node.lhs,
                variable_regs,
                temp_index,
                math_unary_proto,
                math_unary_import,
                math_binary_proto,
                math_binary_import);
            MIR_reg_t rhs_reg = emit_numeric_expression_value(
                ctx,
                func,
                *node.rhs,
                variable_regs,
                temp_index,
                math_unary_proto,
                math_unary_import,
                math_binary_proto,
                math_binary_import);
            MIR_append_insn(ctx,
                            func,
                            MIR_new_insn(ctx,
                                         double_branch_code_for(node.op),
                                         MIR_new_label_op(ctx, true_label),
                                         MIR_new_reg_op(ctx, lhs_reg),
                                         MIR_new_reg_op(ctx, rhs_reg)));
            MIR_append_insn(ctx,
                            func,
                            MIR_new_insn(ctx,
                                         MIR_JMP,
                                         MIR_new_label_op(ctx, false_label)));
            return;
        }
        case EvalExpressionSpec::Node::Kind::And: {
            MIR_label_t rhs_label = MIR_new_label(ctx);
            emit_eval_expression_branch(ctx,
                                        func,
                                        *node.left,
                                        variable_regs,
                                        rhs_label,
                                        false_label,
                                        temp_index,
                                        math_unary_proto,
                                        math_unary_import,
                                        math_binary_proto,
                                        math_binary_import);
            MIR_append_insn(ctx, func, rhs_label);
            emit_eval_expression_branch(ctx,
                                        func,
                                        *node.right,
                                        variable_regs,
                                        true_label,
                                        false_label,
                                        temp_index,
                                        math_unary_proto,
                                        math_unary_import,
                                        math_binary_proto,
                                        math_binary_import);
            return;
        }
        case EvalExpressionSpec::Node::Kind::Or: {
            MIR_label_t rhs_label = MIR_new_label(ctx);
            emit_eval_expression_branch(ctx,
                                        func,
                                        *node.left,
                                        variable_regs,
                                        true_label,
                                        rhs_label,
                                        temp_index,
                                        math_unary_proto,
                                        math_unary_import,
                                        math_binary_proto,
                                        math_binary_import);
            MIR_append_insn(ctx, func, rhs_label);
            emit_eval_expression_branch(ctx,
                                        func,
                                        *node.right,
                                        variable_regs,
                                        true_label,
                                        false_label,
                                        temp_index,
                                        math_unary_proto,
                                        math_unary_import,
                                        math_binary_proto,
                                        math_binary_import);
            return;
        }
    }
}

MIR_reg_t emit_eval_expression_value(MIR_context_t ctx,
                                     MIR_item_t func,
                                     EvalExpressionSpec::Node const& node,
                                     std::vector<MIR_reg_t> const& variable_regs,
                                     std::size_t& temp_index,
                                     MIR_item_t math_unary_proto,
                                     MIR_item_t math_unary_import,
                                     MIR_item_t math_binary_proto,
                                     MIR_item_t math_binary_import) {
    switch (node.kind) {
        case EvalExpressionSpec::Node::Kind::Compare: {
            MIR_reg_t lhs_reg = emit_numeric_expression_value(
                ctx,
                func,
                *node.lhs,
                variable_regs,
                temp_index,
                math_unary_proto,
                math_unary_import,
                math_binary_proto,
                math_binary_import);
            MIR_reg_t rhs_reg = emit_numeric_expression_value(
                ctx,
                func,
                *node.rhs,
                variable_regs,
                temp_index,
                math_unary_proto,
                math_unary_import,
                math_binary_proto,
                math_binary_import);
            std::string result_name = "eval_cmp_" + std::to_string(temp_index++);
            MIR_reg_t result_reg = MIR_new_func_reg(ctx, func->u.func, MIR_T_I64, result_name.c_str());
            MIR_label_t true_label = MIR_new_label(ctx);
            MIR_label_t end_label = MIR_new_label(ctx);
            MIR_append_insn(ctx,
                            func,
                            MIR_new_insn(ctx,
                                         MIR_MOV,
                                         MIR_new_reg_op(ctx, result_reg),
                                         MIR_new_int_op(ctx, 0)));
            MIR_append_insn(ctx,
                            func,
                            MIR_new_insn(ctx,
                                         double_branch_code_for(node.op),
                                         MIR_new_label_op(ctx, true_label),
                                         MIR_new_reg_op(ctx, lhs_reg),
                                         MIR_new_reg_op(ctx, rhs_reg)));
            MIR_append_insn(ctx,
                            func,
                            MIR_new_insn(ctx,
                                         MIR_JMP,
                                         MIR_new_label_op(ctx, end_label)));
            MIR_append_insn(ctx, func, true_label);
            MIR_append_insn(ctx,
                            func,
                            MIR_new_insn(ctx,
                                         MIR_MOV,
                                         MIR_new_reg_op(ctx, result_reg),
                                         MIR_new_int_op(ctx, 1)));
            MIR_append_insn(ctx, func, end_label);
            return result_reg;
        }
        case EvalExpressionSpec::Node::Kind::And:
        case EvalExpressionSpec::Node::Kind::Or: {
            MIR_reg_t lhs_reg = emit_eval_expression_value(
                ctx,
                func,
                *node.left,
                variable_regs,
                temp_index,
                math_unary_proto,
                math_unary_import,
                math_binary_proto,
                math_binary_import);
            MIR_reg_t rhs_reg = emit_eval_expression_value(
                ctx,
                func,
                *node.right,
                variable_regs,
                temp_index,
                math_unary_proto,
                math_unary_import,
                math_binary_proto,
                math_binary_import);
            std::string result_name = "eval_bool_" + std::to_string(temp_index++);
            MIR_reg_t result_reg = MIR_new_func_reg(ctx, func->u.func, MIR_T_I64, result_name.c_str());
            MIR_append_insn(ctx,
                            func,
                            MIR_new_insn(ctx,
                                         node.kind == EvalExpressionSpec::Node::Kind::And ? MIR_AND : MIR_OR,
                                         MIR_new_reg_op(ctx, result_reg),
                                         MIR_new_reg_op(ctx, lhs_reg),
                                         MIR_new_reg_op(ctx, rhs_reg)));
            return result_reg;
        }
    }
    return 0;
}

MIR_item_t emit_eval_expression_predicate(MIR_context_t ctx,
                                          char const* name,
                                          EvalExpressionSpec const& spec,
                                          MIR_item_t math_unary_proto,
                                          MIR_item_t math_unary_import,
                                          MIR_item_t math_binary_proto,
                                          MIR_item_t math_binary_import) {
    MIR_type_t result_type = MIR_T_I64;
    MIR_var_t args[] = {
        {MIR_T_I64, "arg_count", 0},
        {MIR_T_P, "args", 0},
    };
    MIR_item_t func = MIR_new_func_arr(ctx, name, 1, &result_type, 2, args);
    MIR_reg_t arg_count_reg = MIR_reg(ctx, "arg_count", func->u.func);
    MIR_reg_t args_reg = MIR_reg(ctx, "args", func->u.func);
    std::size_t temp_index = 0;
    append_expression_arity_guard(ctx, func, arg_count_reg, args_reg, spec.variables.size());
    std::vector<MIR_reg_t> variable_regs =
        emit_expression_variable_loads(ctx, func, args_reg, spec.variables.size(), temp_index);
    MIR_reg_t result_reg = emit_eval_expression_value(
        ctx,
        func,
        *spec.root,
        variable_regs,
        temp_index,
        math_unary_proto,
        math_unary_import,
        math_binary_proto,
        math_binary_import);
    MIR_append_insn(ctx, func, MIR_new_ret_insn(ctx, 1, MIR_new_reg_op(ctx, result_reg)));
    MIR_finish_func(ctx);
    return func;
}

MIR_item_t emit_i64_binary_compare(MIR_context_t ctx, char const* name, CompareOp op) {
    MIR_type_t result_type = MIR_T_I64;
    MIR_var_t args[] = {
        {MIR_T_I64, "lhs", 0},
        {MIR_T_I64, "rhs", 0},
    };
    MIR_item_t func = MIR_new_func_arr(ctx, name, 1, &result_type, 2, args);
    MIR_reg_t lhs_reg = MIR_reg(ctx, "lhs", func->u.func);
    MIR_reg_t rhs_reg = MIR_reg(ctx, "rhs", func->u.func);
    append_compare_handler(ctx, func, lhs_reg, rhs_reg, MIR_new_label(ctx), i64_branch_code_for(op));
    MIR_finish_func(ctx);
    return func;
}

MIR_item_t emit_double_binary_compare(MIR_context_t ctx, char const* name, CompareOp op) {
    MIR_type_t result_type = MIR_T_I64;
    MIR_var_t args[] = {
        {MIR_T_D, "lhs", 0},
        {MIR_T_D, "rhs", 0},
    };
    MIR_item_t func = MIR_new_func_arr(ctx, name, 1, &result_type, 2, args);
    MIR_reg_t lhs_reg = MIR_reg(ctx, "lhs", func->u.func);
    MIR_reg_t rhs_reg = MIR_reg(ctx, "rhs", func->u.func);
    append_compare_handler(ctx, func, lhs_reg, rhs_reg, MIR_new_label(ctx), double_branch_code_for(op));
    MIR_finish_func(ctx);
    return func;
}

MIR_item_t emit_compare_i64_function(MIR_context_t ctx) {
    MIR_type_t result_type = MIR_T_I64;
    MIR_var_t args[] = {
        {MIR_T_I64, "op", 0},
        {MIR_T_I64, "lhs", 0},
        {MIR_T_I64, "rhs", 0},
    };
    MIR_item_t func = MIR_new_func_arr(ctx, "rulesforge_mir_compare_i64", 1, &result_type, 3, args);
    MIR_reg_t op_reg = MIR_reg(ctx, "op", func->u.func);
    MIR_reg_t lhs_reg = MIR_reg(ctx, "lhs", func->u.func);
    MIR_reg_t rhs_reg = MIR_reg(ctx, "rhs", func->u.func);

    struct BranchCase {
        CompareOp op;
        MIR_insn_code_t branch_code;
        MIR_label_t handler_label;
    };
    BranchCase cases[] = {
        {CompareOp::EQ, MIR_BEQ, MIR_new_label(ctx)},
        {CompareOp::NE, MIR_BNE, MIR_new_label(ctx)},
        {CompareOp::GT, MIR_BGT, MIR_new_label(ctx)},
        {CompareOp::LT, MIR_BLT, MIR_new_label(ctx)},
        {CompareOp::GE, MIR_BGE, MIR_new_label(ctx)},
        {CompareOp::LE, MIR_BLE, MIR_new_label(ctx)},
    };

    for (auto const& branch_case : cases) {
        MIR_append_insn(ctx,
                        func,
                        MIR_new_insn(ctx,
                                     MIR_BEQ,
                                     MIR_new_label_op(ctx, branch_case.handler_label),
                                     MIR_new_reg_op(ctx, op_reg),
                                     MIR_new_int_op(ctx, static_cast<std::int64_t>(branch_case.op))));
    }
    MIR_append_insn(ctx, func, MIR_new_ret_insn(ctx, 1, MIR_new_int_op(ctx, 0)));

    for (auto const& branch_case : cases) {
        append_compare_handler(ctx, func, lhs_reg, rhs_reg, branch_case.handler_label, branch_case.branch_code);
    }

    MIR_finish_func(ctx);
    return func;
}

MIR_item_t emit_compare_double_function(MIR_context_t ctx) {
    MIR_type_t result_type = MIR_T_I64;
    MIR_var_t args[] = {
        {MIR_T_I64, "op", 0},
        {MIR_T_D, "lhs", 0},
        {MIR_T_D, "rhs", 0},
    };
    MIR_item_t func = MIR_new_func_arr(ctx, "rulesforge_mir_compare_double", 1, &result_type, 3, args);
    MIR_reg_t op_reg = MIR_reg(ctx, "op", func->u.func);
    MIR_reg_t lhs_reg = MIR_reg(ctx, "lhs", func->u.func);
    MIR_reg_t rhs_reg = MIR_reg(ctx, "rhs", func->u.func);

    struct BranchCase {
        CompareOp op;
        MIR_insn_code_t branch_code;
        MIR_label_t handler_label;
    };
    BranchCase cases[] = {
        {CompareOp::EQ, MIR_DBEQ, MIR_new_label(ctx)},
        {CompareOp::NE, MIR_DBNE, MIR_new_label(ctx)},
        {CompareOp::GT, MIR_DBGT, MIR_new_label(ctx)},
        {CompareOp::LT, MIR_DBLT, MIR_new_label(ctx)},
        {CompareOp::GE, MIR_DBGE, MIR_new_label(ctx)},
        {CompareOp::LE, MIR_DBLE, MIR_new_label(ctx)},
    };

    for (auto const& branch_case : cases) {
        MIR_append_insn(ctx,
                        func,
                        MIR_new_insn(ctx,
                                     MIR_BEQ,
                                     MIR_new_label_op(ctx, branch_case.handler_label),
                                     MIR_new_reg_op(ctx, op_reg),
                                     MIR_new_int_op(ctx, static_cast<std::int64_t>(branch_case.op))));
    }
    MIR_append_insn(ctx, func, MIR_new_ret_insn(ctx, 1, MIR_new_int_op(ctx, 0)));

    for (auto const& branch_case : cases) {
        append_compare_handler(ctx, func, lhs_reg, rhs_reg, branch_case.handler_label, branch_case.branch_code);
    }

    MIR_finish_func(ctx);
    return func;
}

void append_temporal_compare_handler(MIR_context_t ctx,
                                     MIR_item_t func,
                                     MIR_label_t handler_label,
                                     MIR_reg_t lhs_reg,
                                     MIR_reg_t rhs_reg,
                                     MIR_insn_code_t branch_code) {
    MIR_label_t true_label = MIR_new_label(ctx);
    MIR_append_insn(ctx, func, handler_label);
    MIR_append_insn(ctx,
                    func,
                    MIR_new_insn(ctx,
                                 branch_code,
                                 MIR_new_label_op(ctx, true_label),
                                 MIR_new_reg_op(ctx, lhs_reg),
                                 MIR_new_reg_op(ctx, rhs_reg)));
    MIR_append_insn(ctx, func, MIR_new_ret_insn(ctx, 1, MIR_new_int_op(ctx, 0)));
    MIR_append_insn(ctx, func, true_label);
    MIR_append_insn(ctx, func, MIR_new_ret_insn(ctx, 1, MIR_new_int_op(ctx, 1)));
}

void append_temporal_within_handler(MIR_context_t ctx,
                                    MIR_item_t func,
                                    MIR_label_t handler_label,
                                    MIR_reg_t lhs_reg,
                                    MIR_reg_t rhs_reg,
                                    MIR_reg_t window_reg) {
    MIR_reg_t lower_reg = MIR_new_func_reg(ctx, func->u.func, MIR_T_I64, "within_lower");
    MIR_reg_t upper_reg = MIR_new_func_reg(ctx, func->u.func, MIR_T_I64, "within_upper");
    MIR_label_t false_label = MIR_new_label(ctx);

    MIR_append_insn(ctx, func, handler_label);
    MIR_append_insn(ctx,
                    func,
                    MIR_new_insn(ctx,
                                 MIR_SUB,
                                 MIR_new_reg_op(ctx, lower_reg),
                                 MIR_new_reg_op(ctx, rhs_reg),
                                 MIR_new_reg_op(ctx, window_reg)));
    MIR_append_insn(ctx,
                    func,
                    MIR_new_insn(ctx,
                                 MIR_ADD,
                                 MIR_new_reg_op(ctx, upper_reg),
                                 MIR_new_reg_op(ctx, rhs_reg),
                                 MIR_new_reg_op(ctx, window_reg)));
    MIR_append_insn(ctx,
                    func,
                    MIR_new_insn(ctx,
                                 MIR_BLT,
                                 MIR_new_label_op(ctx, false_label),
                                 MIR_new_reg_op(ctx, lhs_reg),
                                 MIR_new_reg_op(ctx, lower_reg)));
    MIR_append_insn(ctx,
                    func,
                    MIR_new_insn(ctx,
                                 MIR_BGT,
                                 MIR_new_label_op(ctx, false_label),
                                 MIR_new_reg_op(ctx, lhs_reg),
                                 MIR_new_reg_op(ctx, upper_reg)));
    MIR_append_insn(ctx, func, MIR_new_ret_insn(ctx, 1, MIR_new_int_op(ctx, 1)));
    MIR_append_insn(ctx, func, false_label);
    MIR_append_insn(ctx, func, MIR_new_ret_insn(ctx, 1, MIR_new_int_op(ctx, 0)));
}

MIR_item_t emit_temporal_predicate_function(MIR_context_t ctx) {
    MIR_type_t result_type = MIR_T_I64;
    MIR_var_t args[] = {
        {MIR_T_I64, "op", 0},
        {MIR_T_I64, "lhs", 0},
        {MIR_T_I64, "rhs", 0},
        {MIR_T_I64, "window", 0},
    };
    MIR_item_t func = MIR_new_func_arr(ctx, "rulesforge_mir_temporal_predicate", 1, &result_type, 4, args);
    MIR_reg_t op_reg = MIR_reg(ctx, "op", func->u.func);
    MIR_reg_t lhs_reg = MIR_reg(ctx, "lhs", func->u.func);
    MIR_reg_t rhs_reg = MIR_reg(ctx, "rhs", func->u.func);
    MIR_reg_t window_reg = MIR_reg(ctx, "window", func->u.func);

    MIR_label_t after_label = MIR_new_label(ctx);
    MIR_label_t before_label = MIR_new_label(ctx);
    MIR_label_t within_label = MIR_new_label(ctx);
    MIR_label_t coincides_label = MIR_new_label(ctx);
    MIR_label_t during_label = MIR_new_label(ctx);

    struct TemporalCase {
        TemporalOp op;
        MIR_label_t label;
    };
    TemporalCase cases[] = {
        {TemporalOp::After, after_label},
        {TemporalOp::Before, before_label},
        {TemporalOp::Within, within_label},
        {TemporalOp::Coincides, coincides_label},
        {TemporalOp::During, during_label},
    };
    for (auto const& temporal_case : cases) {
        MIR_append_insn(ctx,
                        func,
                        MIR_new_insn(ctx,
                                     MIR_BEQ,
                                     MIR_new_label_op(ctx, temporal_case.label),
                                     MIR_new_reg_op(ctx, op_reg),
                                     MIR_new_int_op(ctx, static_cast<std::int64_t>(temporal_case.op))));
    }
    MIR_append_insn(ctx, func, MIR_new_ret_insn(ctx, 1, MIR_new_int_op(ctx, 0)));

    append_temporal_compare_handler(ctx, func, after_label, lhs_reg, rhs_reg, MIR_BGT);
    append_temporal_compare_handler(ctx, func, before_label, lhs_reg, rhs_reg, MIR_BLT);
    append_temporal_within_handler(ctx, func, within_label, lhs_reg, rhs_reg, window_reg);
    append_temporal_compare_handler(ctx, func, coincides_label, lhs_reg, rhs_reg, MIR_BEQ);
    append_temporal_compare_handler(ctx, func, during_label, lhs_reg, rhs_reg, MIR_BGT);

    MIR_finish_func(ctx);
    return func;
}

MIR_item_t emit_string_binary_compare(MIR_context_t ctx,
                                      char const* name,
                                      CompareOp op,
                                      MIR_item_t string_compare_proto,
                                      MIR_item_t string_compare_import) {
    MIR_type_t result_type = MIR_T_I64;
    MIR_var_t args[] = {
        {MIR_T_P, "lhs", 0},
        {MIR_T_P, "rhs", 0},
    };
    MIR_item_t func = MIR_new_func_arr(ctx, name, 1, &result_type, 2, args);
    MIR_reg_t lhs_reg = MIR_reg(ctx, "lhs", func->u.func);
    MIR_reg_t rhs_reg = MIR_reg(ctx, "rhs", func->u.func);
    MIR_reg_t cmp_reg = MIR_new_func_reg(ctx, func->u.func, MIR_T_I64, "cmp");

    MIR_append_insn(ctx,
                    func,
                    MIR_new_call_insn(ctx,
                                      5,
                                      MIR_new_ref_op(ctx, string_compare_proto),
                                      MIR_new_ref_op(ctx, string_compare_import),
                                      MIR_new_reg_op(ctx, cmp_reg),
                                      MIR_new_reg_op(ctx, lhs_reg),
                                      MIR_new_reg_op(ctx, rhs_reg)));

    MIR_label_t true_label = MIR_new_label(ctx);
    MIR_insn_code_t const branch_code = i64_branch_code_for(op);
    MIR_append_insn(ctx,
                    func,
                    MIR_new_insn(ctx,
                                 branch_code,
                                 MIR_new_label_op(ctx, true_label),
                                 MIR_new_reg_op(ctx, cmp_reg),
                                 MIR_new_int_op(ctx, 0)));
    MIR_append_insn(ctx, func, MIR_new_ret_insn(ctx, 1, MIR_new_int_op(ctx, 0)));
    MIR_append_insn(ctx, func, true_label);
    MIR_append_insn(ctx, func, MIR_new_ret_insn(ctx, 1, MIR_new_int_op(ctx, 1)));
    MIR_finish_func(ctx);
    return func;
}

MIR_item_t emit_string_external_predicate(MIR_context_t ctx,
                                          char const* name,
                                          MIR_item_t predicate_proto,
                                          MIR_item_t predicate_import) {
    MIR_type_t result_type = MIR_T_I64;
    MIR_var_t args[] = {
        {MIR_T_P, "lhs", 0},
        {MIR_T_P, "rhs", 0},
    };
    MIR_item_t func = MIR_new_func_arr(ctx, name, 1, &result_type, 2, args);
    MIR_reg_t lhs_reg = MIR_reg(ctx, "lhs", func->u.func);
    MIR_reg_t rhs_reg = MIR_reg(ctx, "rhs", func->u.func);
    MIR_reg_t result_reg = MIR_new_func_reg(ctx, func->u.func, MIR_T_I64, "contains");

    MIR_append_insn(ctx,
                    func,
                                      MIR_new_call_insn(ctx,
                                      5,
                                      MIR_new_ref_op(ctx, predicate_proto),
                                      MIR_new_ref_op(ctx, predicate_import),
                                      MIR_new_reg_op(ctx, result_reg),
                                      MIR_new_reg_op(ctx, lhs_reg),
                                      MIR_new_reg_op(ctx, rhs_reg)));
    MIR_append_insn(ctx, func, MIR_new_ret_insn(ctx, 1, MIR_new_reg_op(ctx, result_reg)));
    MIR_finish_func(ctx);
    return func;
}

MIR_item_t emit_string_length_is_function(MIR_context_t ctx,
                                          MIR_item_t predicate_proto,
                                          MIR_item_t predicate_import) {
    MIR_type_t result_type = MIR_T_I64;
    MIR_var_t args[] = {
        {MIR_T_P, "lhs", 0},
        {MIR_T_I64, "rhs", 0},
    };
    MIR_item_t func = MIR_new_func_arr(ctx, "rulesforge_mir_string_length_is_predicate", 1, &result_type, 2, args);
    MIR_reg_t lhs_reg = MIR_reg(ctx, "lhs", func->u.func);
    MIR_reg_t rhs_reg = MIR_reg(ctx, "rhs", func->u.func);
    MIR_reg_t result_reg = MIR_new_func_reg(ctx, func->u.func, MIR_T_I64, "length_is");

    MIR_append_insn(ctx,
                    func,
                    MIR_new_call_insn(ctx,
                                      5,
                                      MIR_new_ref_op(ctx, predicate_proto),
                                      MIR_new_ref_op(ctx, predicate_import),
                                      MIR_new_reg_op(ctx, result_reg),
                                      MIR_new_reg_op(ctx, lhs_reg),
                                      MIR_new_reg_op(ctx, rhs_reg)));
    MIR_append_insn(ctx, func, MIR_new_ret_insn(ctx, 1, MIR_new_reg_op(ctx, result_reg)));
    MIR_finish_func(ctx);
    return func;
}

MIR_item_t emit_map_contains_key_function(MIR_context_t ctx,
                                          MIR_item_t predicate_proto,
                                          MIR_item_t predicate_import) {
    MIR_type_t result_type = MIR_T_I64;
    MIR_var_t args[] = {
        {MIR_T_I64, "op", 0},
        {MIR_T_P, "lhs", 0},
        {MIR_T_P, "rhs", 0},
    };
    MIR_item_t func = MIR_new_func_arr(ctx, "rulesforge_mir_map_contains_key_predicate", 1, &result_type, 3, args);
    MIR_reg_t op_reg = MIR_reg(ctx, "op", func->u.func);
    MIR_reg_t lhs_reg = MIR_reg(ctx, "lhs", func->u.func);
    MIR_reg_t rhs_reg = MIR_reg(ctx, "rhs", func->u.func);
    MIR_reg_t result_reg = MIR_new_func_reg(ctx, func->u.func, MIR_T_I64, "map_contains_key");

    MIR_append_insn(ctx,
                    func,
                    MIR_new_call_insn(ctx,
                                      6,
                                      MIR_new_ref_op(ctx, predicate_proto),
                                      MIR_new_ref_op(ctx, predicate_import),
                                      MIR_new_reg_op(ctx, result_reg),
                                      MIR_new_reg_op(ctx, op_reg),
                                      MIR_new_reg_op(ctx, lhs_reg),
                                      MIR_new_reg_op(ctx, rhs_reg)));
    MIR_append_insn(ctx, func, MIR_new_ret_insn(ctx, 1, MIR_new_reg_op(ctx, result_reg)));
    MIR_finish_func(ctx);
    return func;
}

MIR_item_t emit_collection_contains_function(MIR_context_t ctx,
                                             MIR_item_t predicate_proto,
                                             MIR_item_t predicate_import) {
    MIR_type_t result_type = MIR_T_I64;
    MIR_var_t args[] = {
        {MIR_T_I64, "op", 0},
        {MIR_T_P, "lhs", 0},
        {MIR_T_P, "rhs", 0},
    };
    MIR_item_t func = MIR_new_func_arr(ctx, "rulesforge_mir_collection_contains_predicate", 1, &result_type, 3, args);
    MIR_reg_t op_reg = MIR_reg(ctx, "op", func->u.func);
    MIR_reg_t lhs_reg = MIR_reg(ctx, "lhs", func->u.func);
    MIR_reg_t rhs_reg = MIR_reg(ctx, "rhs", func->u.func);
    MIR_reg_t result_reg = MIR_new_func_reg(ctx, func->u.func, MIR_T_I64, "collection_contains");

    MIR_append_insn(ctx,
                    func,
                    MIR_new_call_insn(ctx,
                                      6,
                                      MIR_new_ref_op(ctx, predicate_proto),
                                      MIR_new_ref_op(ctx, predicate_import),
                                      MIR_new_reg_op(ctx, result_reg),
                                      MIR_new_reg_op(ctx, op_reg),
                                      MIR_new_reg_op(ctx, lhs_reg),
                                      MIR_new_reg_op(ctx, rhs_reg)));
    MIR_append_insn(ctx, func, MIR_new_ret_insn(ctx, 1, MIR_new_reg_op(ctx, result_reg)));
    MIR_finish_func(ctx);
    return func;
}

MIR_item_t emit_aggregate_count_function(MIR_context_t ctx,
                                         MIR_item_t aggregate_proto,
                                         MIR_item_t aggregate_import) {
    MIR_type_t result_type = MIR_T_I64;
    MIR_var_t args[] = {
        {MIR_T_I64, "current", 0},
        {MIR_T_I64, "direction", 0},
    };
    MIR_item_t func = MIR_new_func_arr(ctx, "rulesforge_mir_aggregate_count_kernel", 1, &result_type, 2, args);
    MIR_reg_t current_reg = MIR_reg(ctx, "current", func->u.func);
    MIR_reg_t direction_reg = MIR_reg(ctx, "direction", func->u.func);
    MIR_reg_t result_reg = MIR_new_func_reg(ctx, func->u.func, MIR_T_I64, "aggregate_count");

    MIR_append_insn(ctx,
                    func,
                    MIR_new_call_insn(ctx,
                                      5,
                                      MIR_new_ref_op(ctx, aggregate_proto),
                                      MIR_new_ref_op(ctx, aggregate_import),
                                      MIR_new_reg_op(ctx, result_reg),
                                      MIR_new_reg_op(ctx, current_reg),
                                      MIR_new_reg_op(ctx, direction_reg)));
    MIR_append_insn(ctx, func, MIR_new_ret_insn(ctx, 1, MIR_new_reg_op(ctx, result_reg)));
    MIR_finish_func(ctx);
    return func;
}

MIR_item_t emit_aggregate_sum_function(MIR_context_t ctx,
                                       MIR_item_t aggregate_proto,
                                       MIR_item_t aggregate_import) {
    MIR_type_t result_type = MIR_T_D;
    MIR_var_t args[] = {
        {MIR_T_D, "current", 0},
        {MIR_T_D, "value", 0},
        {MIR_T_I64, "direction", 0},
    };
    MIR_item_t func = MIR_new_func_arr(ctx, "rulesforge_mir_aggregate_sum_kernel", 1, &result_type, 3, args);
    MIR_reg_t current_reg = MIR_reg(ctx, "current", func->u.func);
    MIR_reg_t value_reg = MIR_reg(ctx, "value", func->u.func);
    MIR_reg_t direction_reg = MIR_reg(ctx, "direction", func->u.func);
    MIR_reg_t result_reg = MIR_new_func_reg(ctx, func->u.func, MIR_T_D, "aggregate_sum");

    MIR_append_insn(ctx,
                    func,
                    MIR_new_call_insn(ctx,
                                      6,
                                      MIR_new_ref_op(ctx, aggregate_proto),
                                      MIR_new_ref_op(ctx, aggregate_import),
                                      MIR_new_reg_op(ctx, result_reg),
                                      MIR_new_reg_op(ctx, current_reg),
                                      MIR_new_reg_op(ctx, value_reg),
                                      MIR_new_reg_op(ctx, direction_reg)));
    MIR_append_insn(ctx, func, MIR_new_ret_insn(ctx, 1, MIR_new_reg_op(ctx, result_reg)));
    MIR_finish_func(ctx);
    return func;
}

MIR_item_t emit_aggregate_extreme_function(MIR_context_t ctx,
                                           char const* name,
                                           MIR_item_t aggregate_proto,
                                           MIR_item_t aggregate_import) {
    MIR_type_t result_type = MIR_T_D;
    MIR_var_t args[] = {
        {MIR_T_D, "current", 0},
        {MIR_T_D, "value", 0},
    };
    MIR_item_t func = MIR_new_func_arr(ctx, name, 1, &result_type, 2, args);
    MIR_reg_t current_reg = MIR_reg(ctx, "current", func->u.func);
    MIR_reg_t value_reg = MIR_reg(ctx, "value", func->u.func);
    MIR_reg_t result_reg = MIR_new_func_reg(ctx, func->u.func, MIR_T_D, "aggregate_extreme");

    MIR_append_insn(ctx,
                    func,
                    MIR_new_call_insn(ctx,
                                      5,
                                      MIR_new_ref_op(ctx, aggregate_proto),
                                      MIR_new_ref_op(ctx, aggregate_import),
                                      MIR_new_reg_op(ctx, result_reg),
                                      MIR_new_reg_op(ctx, current_reg),
                                      MIR_new_reg_op(ctx, value_reg)));
    MIR_append_insn(ctx, func, MIR_new_ret_insn(ctx, 1, MIR_new_reg_op(ctx, result_reg)));
    MIR_finish_func(ctx);
    return func;
}

MIR_item_t emit_collect_update_function(MIR_context_t ctx,
                                        char const* name,
                                        MIR_item_t aggregate_proto,
                                        MIR_item_t aggregate_import) {
    MIR_type_t result_type = MIR_T_I64;
    MIR_var_t args[] = {
        {MIR_T_P, "facts", 0},
        {MIR_T_P, "fact", 0},
        {MIR_T_I64, "direction", 0},
    };
    MIR_item_t func = MIR_new_func_arr(ctx, name, 1, &result_type, 3, args);
    MIR_reg_t facts_reg = MIR_reg(ctx, "facts", func->u.func);
    MIR_reg_t fact_reg = MIR_reg(ctx, "fact", func->u.func);
    MIR_reg_t direction_reg = MIR_reg(ctx, "direction", func->u.func);
    MIR_reg_t result_reg = MIR_new_func_reg(ctx, func->u.func, MIR_T_I64, "collect_update");

    MIR_append_insn(ctx,
                    func,
                    MIR_new_call_insn(ctx,
                                      6,
                                      MIR_new_ref_op(ctx, aggregate_proto),
                                      MIR_new_ref_op(ctx, aggregate_import),
                                      MIR_new_reg_op(ctx, result_reg),
                                      MIR_new_reg_op(ctx, facts_reg),
                                      MIR_new_reg_op(ctx, fact_reg),
                                      MIR_new_reg_op(ctx, direction_reg)));
    MIR_append_insn(ctx, func, MIR_new_ret_insn(ctx, 1, MIR_new_reg_op(ctx, result_reg)));
    MIR_finish_func(ctx);
    return func;
}

MIR_item_t emit_collect_result_function(MIR_context_t ctx,
                                        char const* name,
                                        MIR_item_t aggregate_proto,
                                        MIR_item_t aggregate_import) {
    MIR_type_t result_type = MIR_T_I64;
    MIR_var_t args[] = {
        {MIR_T_P, "facts", 0},
        {MIR_T_P, "result", 0},
    };
    MIR_item_t func = MIR_new_func_arr(ctx, name, 1, &result_type, 2, args);
    MIR_reg_t facts_reg = MIR_reg(ctx, "facts", func->u.func);
    MIR_reg_t result_ptr_reg = MIR_reg(ctx, "result", func->u.func);
    MIR_reg_t result_reg = MIR_new_func_reg(ctx, func->u.func, MIR_T_I64, "collect_result");

    MIR_append_insn(ctx,
                    func,
                    MIR_new_call_insn(ctx,
                                      5,
                                      MIR_new_ref_op(ctx, aggregate_proto),
                                      MIR_new_ref_op(ctx, aggregate_import),
                                      MIR_new_reg_op(ctx, result_reg),
                                      MIR_new_reg_op(ctx, facts_reg),
                                      MIR_new_reg_op(ctx, result_ptr_reg)));
    MIR_append_insn(ctx, func, MIR_new_ret_insn(ctx, 1, MIR_new_reg_op(ctx, result_reg)));
    MIR_finish_func(ctx);
    return func;
}

} // namespace

MirExecutionPlan::MirExecutionPlan(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

MirExecutionPlan::~MirExecutionPlan() = default;

std::unique_ptr<MirExecutionPlan> MirExecutionPlan::compile(std::vector<ParsedRule> const& rules,
                                                            std::vector<ParsedQuery> const& queries,
                                                            std::vector<ParsedDeclaration> const& declarations,
                                                            std::string* error_out) {
    if (rules.size() > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
        return mir_compile_failure(error_out, "Too many rules for MIR execution plan");
    }

    auto impl = std::make_unique<Impl>();
    impl->ctx = MIR_init();
    if (impl->ctx == nullptr) {
        return mir_compile_failure(error_out, "MIR_init failed");
    }

    MIR_module_t module = MIR_new_module(impl->ctx, "rulesforge_execution_plan");
    if (module == nullptr) {
        return mir_compile_failure(error_out, "MIR_new_module failed");
    }
    MIR_item_t string_compare_import = MIR_new_import(impl->ctx, "rulesforge_mir_string_compare");
    if (string_compare_import == nullptr) {
        return mir_compile_failure(error_out, "MIR_new_import failed for rulesforge_mir_string_compare");
    }
    MIR_item_t string_contains_import = MIR_new_import(impl->ctx, "rulesforge_mir_string_contains");
    if (string_contains_import == nullptr) {
        return mir_compile_failure(error_out, "MIR_new_import failed for rulesforge_mir_string_contains");
    }
    MIR_item_t string_regex_match_import = MIR_new_import(impl->ctx, "rulesforge_mir_string_regex_match");
    if (string_regex_match_import == nullptr) {
        return mir_compile_failure(error_out, "MIR_new_import failed for rulesforge_mir_string_regex_match");
    }
    MIR_item_t string_starts_with_import = MIR_new_import(impl->ctx, "rulesforge_mir_string_starts_with");
    if (string_starts_with_import == nullptr) {
        return mir_compile_failure(error_out, "MIR_new_import failed for rulesforge_mir_string_starts_with");
    }
    MIR_item_t string_ends_with_import = MIR_new_import(impl->ctx, "rulesforge_mir_string_ends_with");
    if (string_ends_with_import == nullptr) {
        return mir_compile_failure(error_out, "MIR_new_import failed for rulesforge_mir_string_ends_with");
    }
    MIR_item_t string_length_is_import = MIR_new_import(impl->ctx, "rulesforge_mir_string_length_is");
    if (string_length_is_import == nullptr) {
        return mir_compile_failure(error_out, "MIR_new_import failed for rulesforge_mir_string_length_is");
    }
    MIR_item_t map_contains_key_import = MIR_new_import(impl->ctx, "rulesforge_mir_map_contains_key");
    if (map_contains_key_import == nullptr) {
        return mir_compile_failure(error_out, "MIR_new_import failed for rulesforge_mir_map_contains_key");
    }
    MIR_item_t collection_contains_import = MIR_new_import(impl->ctx, "rulesforge_mir_collection_contains");
    if (collection_contains_import == nullptr) {
        return mir_compile_failure(error_out, "MIR_new_import failed for rulesforge_mir_collection_contains");
    }
    MIR_item_t math_unary_import = MIR_new_import(impl->ctx, "rulesforge_mir_math_unary");
    if (math_unary_import == nullptr) {
        return mir_compile_failure(error_out, "MIR_new_import failed for rulesforge_mir_math_unary");
    }
    MIR_item_t math_binary_import = MIR_new_import(impl->ctx, "rulesforge_mir_math_binary");
    if (math_binary_import == nullptr) {
        return mir_compile_failure(error_out, "MIR_new_import failed for rulesforge_mir_math_binary");
    }
    MIR_item_t aggregate_count_import = MIR_new_import(impl->ctx, "rulesforge_mir_aggregate_count");
    if (aggregate_count_import == nullptr) {
        return mir_compile_failure(error_out, "MIR_new_import failed for rulesforge_mir_aggregate_count");
    }
    MIR_item_t aggregate_sum_import = MIR_new_import(impl->ctx, "rulesforge_mir_aggregate_sum");
    if (aggregate_sum_import == nullptr) {
        return mir_compile_failure(error_out, "MIR_new_import failed for rulesforge_mir_aggregate_sum");
    }
    MIR_item_t aggregate_min_import = MIR_new_import(impl->ctx, "rulesforge_mir_aggregate_min");
    if (aggregate_min_import == nullptr) {
        return mir_compile_failure(error_out, "MIR_new_import failed for rulesforge_mir_aggregate_min");
    }
    MIR_item_t aggregate_max_import = MIR_new_import(impl->ctx, "rulesforge_mir_aggregate_max");
    if (aggregate_max_import == nullptr) {
        return mir_compile_failure(error_out, "MIR_new_import failed for rulesforge_mir_aggregate_max");
    }
    MIR_item_t collect_list_update_import = MIR_new_import(impl->ctx, "rulesforge_mir_collect_list_update");
    if (collect_list_update_import == nullptr) {
        return mir_compile_failure(error_out, "MIR_new_import failed for rulesforge_mir_collect_list_update");
    }
    MIR_item_t collect_set_update_import = MIR_new_import(impl->ctx, "rulesforge_mir_collect_set_update");
    if (collect_set_update_import == nullptr) {
        return mir_compile_failure(error_out, "MIR_new_import failed for rulesforge_mir_collect_set_update");
    }
    MIR_item_t collect_list_result_import = MIR_new_import(impl->ctx, "rulesforge_mir_collect_list_result");
    if (collect_list_result_import == nullptr) {
        return mir_compile_failure(error_out, "MIR_new_import failed for rulesforge_mir_collect_list_result");
    }
    MIR_item_t collect_set_result_import = MIR_new_import(impl->ctx, "rulesforge_mir_collect_set_result");
    if (collect_set_result_import == nullptr) {
        return mir_compile_failure(error_out, "MIR_new_import failed for rulesforge_mir_collect_set_result");
    }
    MIR_type_t string_compare_result_type = MIR_T_I64;
    MIR_item_t string_compare_proto = MIR_new_proto(impl->ctx,
                                                    "rulesforge_mir_string_compare_proto",
                                                    1,
                                                    &string_compare_result_type,
                                                    2,
                                                    MIR_T_P,
                                                    "lhs",
                                                    MIR_T_P,
                                                    "rhs");
    if (string_compare_proto == nullptr) {
        return mir_compile_failure(error_out, "MIR_new_proto failed for rulesforge_mir_string_compare");
    }
    MIR_type_t string_contains_result_type = MIR_T_I64;
    MIR_item_t string_contains_proto = MIR_new_proto(impl->ctx,
                                                     "rulesforge_mir_string_contains_proto",
                                                     1,
                                                     &string_contains_result_type,
                                                     2,
                                                     MIR_T_P,
                                                     "lhs",
                                                     MIR_T_P,
                                                     "rhs");
    if (string_contains_proto == nullptr) {
        return mir_compile_failure(error_out, "MIR_new_proto failed for rulesforge_mir_string_contains");
    }
    MIR_type_t string_regex_match_result_type = MIR_T_I64;
    MIR_item_t string_regex_match_proto = MIR_new_proto(impl->ctx,
                                                        "rulesforge_mir_string_regex_match_proto",
                                                        1,
                                                        &string_regex_match_result_type,
                                                        2,
                                                        MIR_T_P,
                                                        "lhs",
                                                        MIR_T_P,
                                                        "rhs");
    if (string_regex_match_proto == nullptr) {
        return mir_compile_failure(error_out, "MIR_new_proto failed for rulesforge_mir_string_regex_match");
    }
    MIR_type_t string_starts_with_result_type = MIR_T_I64;
    MIR_item_t string_starts_with_proto = MIR_new_proto(impl->ctx,
                                                        "rulesforge_mir_string_starts_with_proto",
                                                        1,
                                                        &string_starts_with_result_type,
                                                        2,
                                                        MIR_T_P,
                                                        "lhs",
                                                        MIR_T_P,
                                                        "rhs");
    if (string_starts_with_proto == nullptr) {
        return mir_compile_failure(error_out, "MIR_new_proto failed for rulesforge_mir_string_starts_with");
    }
    MIR_type_t string_ends_with_result_type = MIR_T_I64;
    MIR_item_t string_ends_with_proto = MIR_new_proto(impl->ctx,
                                                      "rulesforge_mir_string_ends_with_proto",
                                                      1,
                                                      &string_ends_with_result_type,
                                                      2,
                                                      MIR_T_P,
                                                      "lhs",
                                                      MIR_T_P,
                                                      "rhs");
    if (string_ends_with_proto == nullptr) {
        return mir_compile_failure(error_out, "MIR_new_proto failed for rulesforge_mir_string_ends_with");
    }
    MIR_type_t string_length_is_result_type = MIR_T_I64;
    MIR_item_t string_length_is_proto = MIR_new_proto(impl->ctx,
                                                      "rulesforge_mir_string_length_is_proto",
                                                      1,
                                                      &string_length_is_result_type,
                                                      2,
                                                      MIR_T_P,
                                                      "lhs",
                                                      MIR_T_I64,
                                                      "rhs");
    if (string_length_is_proto == nullptr) {
        return mir_compile_failure(error_out, "MIR_new_proto failed for rulesforge_mir_string_length_is");
    }
    MIR_type_t map_contains_key_result_type = MIR_T_I64;
    MIR_item_t map_contains_key_proto = MIR_new_proto(impl->ctx,
                                                      "rulesforge_mir_map_contains_key_proto",
                                                      1,
                                                      &map_contains_key_result_type,
                                                      3,
                                                      MIR_T_I64,
                                                      "op",
                                                      MIR_T_P,
                                                      "lhs",
                                                      MIR_T_P,
                                                      "rhs");
    if (map_contains_key_proto == nullptr) {
        return mir_compile_failure(error_out, "MIR_new_proto failed for rulesforge_mir_map_contains_key");
    }
    MIR_type_t collection_contains_result_type = MIR_T_I64;
    MIR_item_t collection_contains_proto = MIR_new_proto(impl->ctx,
                                                         "rulesforge_mir_collection_contains_proto",
                                                         1,
                                                         &collection_contains_result_type,
                                                         3,
                                                         MIR_T_I64,
                                                         "op",
                                                         MIR_T_P,
                                                         "lhs",
                                                         MIR_T_P,
                                                         "rhs");
    if (collection_contains_proto == nullptr) {
        return mir_compile_failure(error_out, "MIR_new_proto failed for rulesforge_mir_collection_contains");
    }
    MIR_type_t math_unary_result_type = MIR_T_D;
    MIR_item_t math_unary_proto = MIR_new_proto(impl->ctx,
                                                "rulesforge_mir_math_unary_proto",
                                                1,
                                                &math_unary_result_type,
                                                2,
                                                MIR_T_I64,
                                                "op",
                                                MIR_T_D,
                                                "value");
    if (math_unary_proto == nullptr) {
        return mir_compile_failure(error_out, "MIR_new_proto failed for rulesforge_mir_math_unary");
    }
    MIR_type_t math_binary_result_type = MIR_T_D;
    MIR_item_t math_binary_proto = MIR_new_proto(impl->ctx,
                                                 "rulesforge_mir_math_binary_proto",
                                                 1,
                                                 &math_binary_result_type,
                                                 3,
                                                 MIR_T_I64,
                                                 "op",
                                                 MIR_T_D,
                                                 "lhs",
                                                 MIR_T_D,
                                                 "rhs");
    if (math_binary_proto == nullptr) {
        return mir_compile_failure(error_out, "MIR_new_proto failed for rulesforge_mir_math_binary");
    }
    MIR_type_t aggregate_count_result_type = MIR_T_I64;
    MIR_item_t aggregate_count_proto = MIR_new_proto(impl->ctx,
                                                     "rulesforge_mir_aggregate_count_proto",
                                                     1,
                                                     &aggregate_count_result_type,
                                                     2,
                                                     MIR_T_I64,
                                                     "current",
                                                     MIR_T_I64,
                                                     "direction");
    if (aggregate_count_proto == nullptr) {
        return mir_compile_failure(error_out, "MIR_new_proto failed for rulesforge_mir_aggregate_count");
    }
    MIR_type_t aggregate_sum_result_type = MIR_T_D;
    MIR_item_t aggregate_sum_proto = MIR_new_proto(impl->ctx,
                                                   "rulesforge_mir_aggregate_sum_proto",
                                                   1,
                                                   &aggregate_sum_result_type,
                                                   3,
                                                   MIR_T_D,
                                                   "current",
                                                   MIR_T_D,
                                                   "value",
                                                   MIR_T_I64,
                                                   "direction");
    if (aggregate_sum_proto == nullptr) {
        return mir_compile_failure(error_out, "MIR_new_proto failed for rulesforge_mir_aggregate_sum");
    }
    MIR_type_t aggregate_extreme_result_type = MIR_T_D;
    MIR_item_t aggregate_extreme_proto = MIR_new_proto(impl->ctx,
                                                       "rulesforge_mir_aggregate_extreme_proto",
                                                       1,
                                                       &aggregate_extreme_result_type,
                                                       2,
                                                       MIR_T_D,
                                                       "current",
                                                       MIR_T_D,
                                                       "value");
    if (aggregate_extreme_proto == nullptr) {
        return mir_compile_failure(error_out, "MIR_new_proto failed for aggregate extreme helpers");
    }
    MIR_type_t collect_update_result_type = MIR_T_I64;
    MIR_item_t collect_update_proto = MIR_new_proto(impl->ctx,
                                                    "rulesforge_mir_collect_update_proto",
                                                    1,
                                                    &collect_update_result_type,
                                                    3,
                                                    MIR_T_P,
                                                    "facts",
                                                    MIR_T_P,
                                                    "fact",
                                                    MIR_T_I64,
                                                    "direction");
    if (collect_update_proto == nullptr) {
        return mir_compile_failure(error_out, "MIR_new_proto failed for collect update helpers");
    }
    MIR_type_t collect_result_result_type = MIR_T_I64;
    MIR_item_t collect_result_proto = MIR_new_proto(impl->ctx,
                                                    "rulesforge_mir_collect_result_proto",
                                                    1,
                                                    &collect_result_result_type,
                                                    2,
                                                    MIR_T_P,
                                                    "facts",
                                                    MIR_T_P,
                                                    "result");
    if (collect_result_proto == nullptr) {
        return mir_compile_failure(error_out, "MIR_new_proto failed for collect result helpers");
    }
    MIR_item_t rule_count_func =
        emit_constant_i64_function(impl->ctx, "rulesforge_mir_rule_count", static_cast<std::int64_t>(rules.size()));
    MIR_item_t rule_salience_func = emit_indexed_i64_function(
        impl->ctx,
        "rulesforge_mir_rule_salience",
        rules,
        [](ParsedRule const& rule) { return static_cast<std::int64_t>(rule.salience); },
        0);
    MIR_item_t rule_enabled_func = emit_indexed_i64_function(
        impl->ctx,
        "rulesforge_mir_rule_enabled",
        rules,
        [](ParsedRule const& rule) { return rule.enabled ? 1 : 0; },
        0);
    MIR_item_t compare_i64_func = emit_compare_i64_function(impl->ctx);
    MIR_item_t compare_double_func = emit_compare_double_function(impl->ctx);
    MIR_item_t temporal_predicate_func = emit_temporal_predicate_function(impl->ctx);
    MIR_item_t string_contains_func = emit_string_external_predicate(
        impl->ctx,
        "rulesforge_mir_string_contains_predicate",
        string_contains_proto,
        string_contains_import);
    MIR_item_t string_regex_match_func = emit_string_external_predicate(
        impl->ctx,
        "rulesforge_mir_string_regex_match_predicate",
        string_regex_match_proto,
        string_regex_match_import);
    MIR_item_t string_starts_with_func = emit_string_external_predicate(
        impl->ctx,
        "rulesforge_mir_string_starts_with_predicate",
        string_starts_with_proto,
        string_starts_with_import);
    MIR_item_t string_ends_with_func = emit_string_external_predicate(
        impl->ctx,
        "rulesforge_mir_string_ends_with_predicate",
        string_ends_with_proto,
        string_ends_with_import);
    MIR_item_t string_length_is_func = emit_string_length_is_function(
        impl->ctx,
        string_length_is_proto,
        string_length_is_import);
    MIR_item_t map_contains_key_func = emit_map_contains_key_function(
        impl->ctx,
        map_contains_key_proto,
        map_contains_key_import);
    MIR_item_t collection_contains_func = emit_collection_contains_function(
        impl->ctx,
        collection_contains_proto,
        collection_contains_import);
    MIR_item_t aggregate_count_func = emit_aggregate_count_function(
        impl->ctx,
        aggregate_count_proto,
        aggregate_count_import);
    MIR_item_t aggregate_sum_func = emit_aggregate_sum_function(
        impl->ctx,
        aggregate_sum_proto,
        aggregate_sum_import);
    MIR_item_t aggregate_min_func = emit_aggregate_extreme_function(
        impl->ctx,
        "rulesforge_mir_aggregate_min_kernel",
        aggregate_extreme_proto,
        aggregate_min_import);
    MIR_item_t aggregate_max_func = emit_aggregate_extreme_function(
        impl->ctx,
        "rulesforge_mir_aggregate_max_kernel",
        aggregate_extreme_proto,
        aggregate_max_import);
    MIR_item_t collect_list_update_func = emit_collect_update_function(
        impl->ctx,
        "rulesforge_mir_collect_list_update_kernel",
        collect_update_proto,
        collect_list_update_import);
    MIR_item_t collect_set_update_func = emit_collect_update_function(
        impl->ctx,
        "rulesforge_mir_collect_set_update_kernel",
        collect_update_proto,
        collect_set_update_import);
    MIR_item_t collect_list_result_func = emit_collect_result_function(
        impl->ctx,
        "rulesforge_mir_collect_list_result_kernel",
        collect_result_proto,
        collect_list_result_import);
    MIR_item_t collect_set_result_func = emit_collect_result_function(
        impl->ctx,
        "rulesforge_mir_collect_set_result_kernel",
        collect_result_proto,
        collect_set_result_import);
    if (rule_count_func == nullptr || rule_salience_func == nullptr || rule_enabled_func == nullptr
        || compare_i64_func == nullptr || compare_double_func == nullptr || temporal_predicate_func == nullptr
        || string_contains_func == nullptr || string_regex_match_func == nullptr || string_starts_with_func == nullptr
        || string_ends_with_func == nullptr || string_length_is_func == nullptr || map_contains_key_func == nullptr
        || collection_contains_func == nullptr || aggregate_count_func == nullptr || aggregate_sum_func == nullptr
        || aggregate_min_func == nullptr || aggregate_max_func == nullptr || collect_list_update_func == nullptr
        || collect_set_update_func == nullptr || collect_list_result_func == nullptr
        || collect_set_result_func == nullptr) {
        return mir_compile_failure(error_out, "MIR function emission failed for one or more core functions");
    }
    std::array<CompareOp, 6> const numeric_ops = {
        CompareOp::EQ,
        CompareOp::NE,
        CompareOp::GT,
        CompareOp::LT,
        CompareOp::GE,
        CompareOp::LE,
    };
    std::array<MIR_item_t, 6> fixed_compare_i64_items{};
    std::array<MIR_item_t, 6> fixed_compare_double_items{};
    std::array<MIR_item_t, 6> fixed_compare_string_items{};
    for (std::size_t index = 0; index < numeric_ops.size(); ++index) {
        std::string i64_name = "rulesforge_mir_compare_i64_fixed_" + std::to_string(index);
        std::string double_name = "rulesforge_mir_compare_double_fixed_" + std::to_string(index);
        fixed_compare_i64_items[index] = emit_i64_binary_compare(impl->ctx, i64_name.c_str(), numeric_ops[index]);
        fixed_compare_double_items[index] = emit_double_binary_compare(impl->ctx, double_name.c_str(), numeric_ops[index]);
    }
    std::array<CompareOp, 6> const string_ops = {
        CompareOp::EQ,
        CompareOp::NE,
        CompareOp::GT,
        CompareOp::LT,
        CompareOp::GE,
        CompareOp::LE,
    };
    for (std::size_t index = 0; index < string_ops.size(); ++index) {
        std::string string_name = "rulesforge_mir_compare_string_fixed_" + std::to_string(index);
        fixed_compare_string_items[index] = emit_string_binary_compare(
            impl->ctx,
            string_name.c_str(),
            string_ops[index],
            string_compare_proto,
            string_compare_import);
    }
    auto literal_predicate_specs = collect_numeric_literal_predicates(rules, queries);
    auto numeric_expression_predicate_specs = collect_numeric_expression_predicates(rules, queries);
    auto numeric_value_expression_specs = collect_numeric_value_expressions(rules);
    auto value_list_predicate_specs = collect_value_list_predicates(rules, queries);
    auto eval_expression_predicate_specs = collect_eval_expression_predicates(rules, queries);
    struct PendingNumericExpressionPredicate {
        std::string key;
        std::vector<std::string> variables;
        MIR_item_t item = nullptr;
    };
    struct PendingNumericValueExpression {
        std::string key;
        std::vector<std::string> variables;
        MIR_item_t item = nullptr;
    };
    struct PendingValueListPredicate {
        std::string key;
        ValueListPredicateKind kind = ValueListPredicateKind::Numeric;
        MIR_item_t item = nullptr;
    };
    struct PendingEvalExpressionPredicate {
        std::string key;
        std::vector<std::string> variables;
        MIR_item_t item = nullptr;
    };
    std::vector<PendingNumericExpressionPredicate> pending_numeric_expression_predicates;
    pending_numeric_expression_predicates.reserve(numeric_expression_predicate_specs.size());
    for (std::size_t index = 0; index < numeric_expression_predicate_specs.size(); ++index) {
        auto const& spec = numeric_expression_predicate_specs[index];
        std::string name = "rulesforge_mir_numeric_expression_predicate_" + std::to_string(index);
        pending_numeric_expression_predicates.push_back(PendingNumericExpressionPredicate{
            spec.key,
            spec.variables,
            emit_numeric_expression_predicate(impl->ctx,
                                              name.c_str(),
                                              spec,
                                              math_unary_proto,
                                              math_unary_import,
                                              math_binary_proto,
                                              math_binary_import)});
    }
    std::vector<PendingNumericValueExpression> pending_numeric_value_expressions;
    pending_numeric_value_expressions.reserve(numeric_value_expression_specs.size());
    for (std::size_t index = 0; index < numeric_value_expression_specs.size(); ++index) {
        auto const& spec = numeric_value_expression_specs[index];
        std::string name = "rulesforge_mir_numeric_value_expression_" + std::to_string(index);
        pending_numeric_value_expressions.push_back(PendingNumericValueExpression{
            spec.key,
            spec.variables,
            emit_numeric_value_expression(impl->ctx,
                                          name.c_str(),
                                          spec,
                                          math_unary_proto,
                                          math_unary_import,
                                          math_binary_proto,
                                          math_binary_import)});
    }
    std::vector<PendingValueListPredicate> pending_value_list_predicates;
    pending_value_list_predicates.reserve(value_list_predicate_specs.size());
    for (std::size_t index = 0; index < value_list_predicate_specs.size(); ++index) {
        auto const& spec = value_list_predicate_specs[index];
        std::string name = "rulesforge_mir_value_list_predicate_" + std::to_string(index);
        MIR_item_t item = nullptr;
        if (spec.kind == ValueListPredicateKind::Numeric) {
            item = emit_double_value_list_predicate(impl->ctx, name.c_str(), spec);
        } else {
            std::vector<std::string> literals;
            literals.reserve(spec.values.size());
            for (auto const& value : spec.values) {
                literals.push_back(std::get<std::string>(value));
            }
            impl->value_list_string_storage.push_back(std::move(literals));
            item = emit_string_value_list_predicate(
                impl->ctx,
                name.c_str(),
                spec,
                impl->value_list_string_storage.back(),
                string_compare_proto,
                string_compare_import);
        }
        pending_value_list_predicates.push_back(PendingValueListPredicate{
            spec.key,
            spec.kind,
            item});
    }
    std::vector<PendingEvalExpressionPredicate> pending_eval_expression_predicates;
    pending_eval_expression_predicates.reserve(eval_expression_predicate_specs.size());
    for (std::size_t index = 0; index < eval_expression_predicate_specs.size(); ++index) {
        auto const& spec = eval_expression_predicate_specs[index];
        std::string name = "rulesforge_mir_eval_expression_predicate_" + std::to_string(index);
        pending_eval_expression_predicates.push_back(PendingEvalExpressionPredicate{
            spec.key,
            spec.variables,
            emit_eval_expression_predicate(impl->ctx,
                                           name.c_str(),
                                           spec,
                                           math_unary_proto,
                                           math_unary_import,
                                           math_binary_proto,
                                           math_binary_import)});
    }
    MIR_finish_module(impl->ctx);
    MIR_load_module(impl->ctx, module);
    MIR_load_external(impl->ctx,
                      "rulesforge_mir_string_compare",
                      reinterpret_cast<void*>(&rulesforge_mir_string_compare));
    MIR_load_external(impl->ctx,
                      "rulesforge_mir_string_contains",
                      reinterpret_cast<void*>(&rulesforge_mir_string_contains));
    MIR_load_external(impl->ctx,
                      "rulesforge_mir_string_regex_match",
                      reinterpret_cast<void*>(&rulesforge_mir_string_regex_match));
    MIR_load_external(impl->ctx,
                      "rulesforge_mir_string_starts_with",
                      reinterpret_cast<void*>(&rulesforge_mir_string_starts_with));
    MIR_load_external(impl->ctx,
                      "rulesforge_mir_string_ends_with",
                      reinterpret_cast<void*>(&rulesforge_mir_string_ends_with));
    MIR_load_external(impl->ctx,
                      "rulesforge_mir_string_length_is",
                      reinterpret_cast<void*>(&rulesforge_mir_string_length_is));
    MIR_load_external(impl->ctx,
                      "rulesforge_mir_map_contains_key",
                      reinterpret_cast<void*>(&rulesforge_mir_map_contains_key));
    MIR_load_external(impl->ctx,
                      "rulesforge_mir_collection_contains",
                      reinterpret_cast<void*>(&rulesforge_mir_collection_contains));
    MIR_load_external(impl->ctx,
                      "rulesforge_mir_math_unary",
                      reinterpret_cast<void*>(&rulesforge_mir_math_unary));
    MIR_load_external(impl->ctx,
                      "rulesforge_mir_math_binary",
                      reinterpret_cast<void*>(&rulesforge_mir_math_binary));
    MIR_load_external(impl->ctx,
                      "rulesforge_mir_aggregate_count",
                      reinterpret_cast<void*>(&rulesforge_mir_aggregate_count));
    MIR_load_external(impl->ctx,
                      "rulesforge_mir_aggregate_sum",
                      reinterpret_cast<void*>(&rulesforge_mir_aggregate_sum));
    MIR_load_external(impl->ctx,
                      "rulesforge_mir_aggregate_min",
                      reinterpret_cast<void*>(&rulesforge_mir_aggregate_min));
    MIR_load_external(impl->ctx,
                      "rulesforge_mir_aggregate_max",
                      reinterpret_cast<void*>(&rulesforge_mir_aggregate_max));
    MIR_load_external(impl->ctx,
                      "rulesforge_mir_collect_list_update",
                      reinterpret_cast<void*>(&rulesforge_mir_collect_list_update));
    MIR_load_external(impl->ctx,
                      "rulesforge_mir_collect_set_update",
                      reinterpret_cast<void*>(&rulesforge_mir_collect_set_update));
    MIR_load_external(impl->ctx,
                      "rulesforge_mir_collect_list_result",
                      reinterpret_cast<void*>(&rulesforge_mir_collect_list_result));
    MIR_load_external(impl->ctx,
                      "rulesforge_mir_collect_set_result",
                      reinterpret_cast<void*>(&rulesforge_mir_collect_set_result));
    MIR_gen_init(impl->ctx);
    impl->gen_initialized = true;
    MIR_link(impl->ctx, MIR_set_gen_interface, nullptr);

    impl->rule_count_fn = reinterpret_cast<Impl::RuleCountFn>(rule_count_func->addr);
    impl->rule_salience_fn = reinterpret_cast<Impl::RuleLookupFn>(rule_salience_func->addr);
    impl->rule_enabled_fn = reinterpret_cast<Impl::RuleLookupFn>(rule_enabled_func->addr);
    impl->compare_i64_fn = reinterpret_cast<Impl::CompareI64Fn>(compare_i64_func->addr);
    impl->compare_double_fn = reinterpret_cast<Impl::CompareDoubleFn>(compare_double_func->addr);
    impl->temporal_predicate_fn = reinterpret_cast<Impl::TemporalPredicateFn>(temporal_predicate_func->addr);
    impl->string_contains_fn = reinterpret_cast<Impl::StringContainsFn>(string_contains_func->addr);
    impl->string_regex_match_fn = reinterpret_cast<Impl::StringRegexMatchFn>(string_regex_match_func->addr);
    impl->string_starts_with_fn = reinterpret_cast<Impl::StringAffixFn>(string_starts_with_func->addr);
    impl->string_ends_with_fn = reinterpret_cast<Impl::StringAffixFn>(string_ends_with_func->addr);
    impl->string_length_is_fn = reinterpret_cast<Impl::StringLengthIsFn>(string_length_is_func->addr);
    impl->map_contains_key_fn = reinterpret_cast<Impl::MapContainsKeyFn>(map_contains_key_func->addr);
    impl->collection_contains_fn = reinterpret_cast<Impl::CollectionContainsFn>(collection_contains_func->addr);
    impl->aggregate_count_fn = reinterpret_cast<Impl::AggregateCountFn>(aggregate_count_func->addr);
    impl->aggregate_sum_fn = reinterpret_cast<Impl::AggregateSumFn>(aggregate_sum_func->addr);
    impl->aggregate_min_fn = reinterpret_cast<Impl::AggregateExtremeFn>(aggregate_min_func->addr);
    impl->aggregate_max_fn = reinterpret_cast<Impl::AggregateExtremeFn>(aggregate_max_func->addr);
    impl->collect_list_update_fn = reinterpret_cast<Impl::AggregateCollectUpdateFn>(collect_list_update_func->addr);
    impl->collect_set_update_fn = reinterpret_cast<Impl::AggregateCollectUpdateFn>(collect_set_update_func->addr);
    impl->collect_list_result_fn = reinterpret_cast<Impl::AggregateCollectResultFn>(collect_list_result_func->addr);
    impl->collect_set_result_fn = reinterpret_cast<Impl::AggregateCollectResultFn>(collect_set_result_func->addr);
    for (std::size_t index = 0; index < numeric_ops.size(); ++index) {
        if (fixed_compare_i64_items[index] == nullptr || fixed_compare_i64_items[index]->addr == nullptr
            || fixed_compare_double_items[index] == nullptr || fixed_compare_double_items[index]->addr == nullptr) {
            if (error_out != nullptr) {
                *error_out = "MIR did not produce one or more fixed numeric compare functions";
            }
            return nullptr;
        }
        impl->fixed_compare_i64_fns[index] =
            reinterpret_cast<Impl::FixedCompareI64Fn>(fixed_compare_i64_items[index]->addr);
        impl->fixed_compare_double_fns[index] =
            reinterpret_cast<Impl::FixedCompareDoubleFn>(fixed_compare_double_items[index]->addr);
    }
    for (std::size_t index = 0; index < string_ops.size(); ++index) {
        if (fixed_compare_string_items[index] == nullptr || fixed_compare_string_items[index]->addr == nullptr) {
            if (error_out != nullptr) {
                *error_out = "MIR did not produce one or more fixed string compare functions";
            }
            return nullptr;
        }
        impl->fixed_compare_string_fns[index] =
            reinterpret_cast<Impl::FixedCompareStringFn>(fixed_compare_string_items[index]->addr);
    }
    for (auto const& spec : literal_predicate_specs) {
        std::size_t const predicate_id = impl->literal_predicates.size();
        impl->literal_predicate_ids[spec.key] = predicate_id;
        impl->literal_predicates.push_back(Impl::LiteralPredicateEntry{spec.op, spec.literal});
    }
    for (auto const& pending : pending_numeric_expression_predicates) {
        if (pending.item == nullptr || pending.item->addr == nullptr) {
            if (error_out != nullptr) {
                *error_out = "MIR did not produce one or more numeric expression predicates";
            }
            return nullptr;
        }
        std::size_t const predicate_id = impl->numeric_expression_predicates.size();
        impl->numeric_expression_predicate_ids[pending.key] = predicate_id;
        impl->numeric_expression_predicates.push_back(Impl::NumericExpressionPredicateEntry{
            pending.item->addr,
            pending.variables});
    }
    for (auto const& pending : pending_numeric_value_expressions) {
        if (pending.item == nullptr || pending.item->addr == nullptr) {
            if (error_out != nullptr) {
                *error_out = "MIR did not produce one or more numeric value expressions";
            }
            return nullptr;
        }
        std::size_t const expression_id = impl->numeric_value_expressions.size();
        impl->numeric_value_expression_ids[pending.key] = expression_id;
        impl->numeric_value_expressions.push_back(Impl::NumericValueExpressionEntry{
            reinterpret_cast<Impl::NumericValueExpressionFn>(pending.item->addr),
            pending.variables});
    }
    for (auto const& pending : pending_value_list_predicates) {
        if (pending.item == nullptr || pending.item->addr == nullptr) {
            if (error_out != nullptr) {
                *error_out = "MIR did not produce one or more value-list predicates";
            }
            return nullptr;
        }
        std::size_t const predicate_id = impl->value_list_predicates.size();
        impl->value_list_predicate_ids[pending.key] = predicate_id;
        if (pending.kind == ValueListPredicateKind::String) {
            impl->value_list_predicates.push_back(Impl::ValueListPredicateEntry{
                true,
                nullptr,
                reinterpret_cast<Impl::ValueListStringPredicateFn>(pending.item->addr)});
        } else {
            impl->value_list_predicates.push_back(Impl::ValueListPredicateEntry{
                false,
                reinterpret_cast<Impl::ValueListDoublePredicateFn>(pending.item->addr),
                nullptr});
        }
    }
    for (auto const& pending : pending_eval_expression_predicates) {
        if (pending.item == nullptr || pending.item->addr == nullptr) {
            if (error_out != nullptr) {
                *error_out = "MIR did not produce one or more eval expression predicates";
            }
            return nullptr;
        }
        std::size_t const predicate_id = impl->eval_expression_predicates.size();
        impl->eval_expression_predicate_ids[pending.key] = predicate_id;
        impl->eval_expression_predicates.push_back(Impl::EvalExpressionPredicateEntry{
            pending.item->addr,
            pending.variables});
    }
    if (impl->rule_count_fn == nullptr || impl->rule_salience_fn == nullptr || impl->rule_enabled_fn == nullptr
        || impl->compare_i64_fn == nullptr || impl->compare_double_fn == nullptr
        || impl->temporal_predicate_fn == nullptr
        || impl->string_contains_fn == nullptr || impl->string_regex_match_fn == nullptr
        || impl->string_starts_with_fn == nullptr || impl->string_ends_with_fn == nullptr
        || impl->string_length_is_fn == nullptr || impl->map_contains_key_fn == nullptr
        || impl->collection_contains_fn == nullptr) {
        if (error_out != nullptr) {
            *error_out = "MIR did not produce one or more rulesforge execution functions";
        }
        return nullptr;
    }

    auto plan = std::unique_ptr<MirExecutionPlan>(new MirExecutionPlan(std::move(impl)));
    plan->rule_count_ = rules.size();
    plan->query_count_ = queries.size();
    plan->rule_coverages_ = collect_rule_coverages(rules, declarations);
    plan->query_coverages_ = collect_query_coverages(queries, declarations);
    plan->rule_graphs_ = collect_rule_graphs(*plan->impl_, rules, declarations);
    plan->query_graphs_ = collect_query_graphs(*plan->impl_, queries, declarations, error_out);
    if (plan->query_graphs_.size() != queries.size()) {
        if (error_out != nullptr && error_out->empty()) {
            *error_out = "MIR query graph lowering failed";
        }
        return nullptr;
    }
    return plan;
}

std::int64_t MirExecutionPlan::run_rule_count() const {
    if (impl_ == nullptr || impl_->rule_count_fn == nullptr) {
        return -1;
    }
    return impl_->rule_count_fn();
}

std::int64_t MirExecutionPlan::run_rule_salience(std::size_t rule_index) const {
    if (impl_ == nullptr || impl_->rule_salience_fn == nullptr
        || rule_index > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
        return 0;
    }
    return impl_->rule_salience_fn(static_cast<std::int64_t>(rule_index));
}

bool MirExecutionPlan::run_rule_enabled(std::size_t rule_index) const {
    if (impl_ == nullptr || impl_->rule_enabled_fn == nullptr
        || rule_index > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
        return false;
    }
    return impl_->rule_enabled_fn(static_cast<std::int64_t>(rule_index)) != 0;
}

std::optional<bool> MirExecutionPlan::run_temporal_predicate(TemporalOp op,
                                                             std::int64_t lhs,
                                                             std::int64_t rhs,
                                                             std::int64_t window_ms) const {
    if (impl_ == nullptr || impl_->temporal_predicate_fn == nullptr
        || !supported_temporal_predicate_op(op, window_ms)) {
        return std::nullopt;
    }
    return impl_->temporal_predicate_fn(static_cast<std::int64_t>(op), lhs, rhs, window_ms) != 0;
}

std::optional<bool> MirExecutionPlan::run_string_contains(CompareOp op,
                                                          std::string const& lhs,
                                                          std::string const& rhs) const {
    if (impl_ == nullptr || impl_->string_contains_fn == nullptr) {
        return std::nullopt;
    }
    if (op != CompareOp::Contains && op != CompareOp::NotContains) {
        return std::nullopt;
    }
    bool const contains = impl_->string_contains_fn(lhs.c_str(), rhs.c_str()) != 0;
    return op == CompareOp::Contains ? contains : !contains;
}

std::optional<bool> MirExecutionPlan::run_string_matches(CompareOp op,
                                                         std::string const& lhs,
                                                         std::string const& rhs) const {
    if (impl_ == nullptr || impl_->string_regex_match_fn == nullptr) {
        return std::nullopt;
    }
    if (op != CompareOp::Matches && op != CompareOp::NotMatches) {
        return std::nullopt;
    }
    std::int64_t const result = impl_->string_regex_match_fn(lhs.c_str(), rhs.c_str());
    if (result < 0) {
        return std::nullopt;
    }
    bool const matches = result != 0;
    return op == CompareOp::Matches ? matches : !matches;
}

std::optional<bool> MirExecutionPlan::run_string_affix(CompareOp op,
                                                       std::string const& lhs,
                                                       std::string const& rhs) const {
    if (impl_ == nullptr) {
        return std::nullopt;
    }
    if (op == CompareOp::StartsWith) {
        if (impl_->string_starts_with_fn == nullptr) {
            return std::nullopt;
        }
        return impl_->string_starts_with_fn(lhs.c_str(), rhs.c_str()) != 0;
    }
    if (op == CompareOp::EndsWith) {
        if (impl_->string_ends_with_fn == nullptr) {
            return std::nullopt;
        }
        return impl_->string_ends_with_fn(lhs.c_str(), rhs.c_str()) != 0;
    }
    return std::nullopt;
}

std::optional<bool> MirExecutionPlan::run_string_length_is(std::string const& lhs, std::int64_t rhs) const {
    if (impl_ == nullptr || impl_->string_length_is_fn == nullptr) {
        return std::nullopt;
    }
    return impl_->string_length_is_fn(lhs.c_str(), rhs) != 0;
}

std::optional<bool> MirExecutionPlan::run_map_contains_key(CompareOp op,
                                                           ConstraintValue const& lhs,
                                                           ConstraintValue const& rhs) const {
    if (impl_ == nullptr || impl_->map_contains_key_fn == nullptr
        || (op != CompareOp::ContainsKey && op != CompareOp::NotContainsKey)) {
        return std::nullopt;
    }
    std::int64_t const result = impl_->map_contains_key_fn(
        static_cast<std::int64_t>(op),
        &lhs,
        &rhs);
    if (result < 0) {
        return std::nullopt;
    }
    return result != 0;
}

std::optional<bool> MirExecutionPlan::run_collection_contains(CompareOp op,
                                                              ConstraintValue const& lhs,
                                                              ConstraintValue const& rhs) const {
    if (impl_ == nullptr || impl_->collection_contains_fn == nullptr
        || (op != CompareOp::Contains && op != CompareOp::NotContains
            && op != CompareOp::MemberOf && op != CompareOp::NotMemberOf)) {
        return std::nullopt;
    }
    std::int64_t const result = impl_->collection_contains_fn(
        static_cast<std::int64_t>(op),
        &lhs,
        &rhs);
    if (result < 0) {
        return std::nullopt;
    }
    return result != 0;
}

std::optional<std::size_t> MirExecutionPlan::compare_predicate_id(CompareOp op) const {
    if (impl_ == nullptr) {
        return std::nullopt;
    }
    return numeric_compare_op_index(op);
}

std::optional<bool> MirExecutionPlan::run_compare_predicate(std::size_t predicate_id,
                                                            ConstraintValue const& lhs,
                                                            ConstraintValue const& rhs) const {
    if (impl_ == nullptr || predicate_id >= impl_->fixed_compare_i64_fns.size()) {
        return std::nullopt;
    }

    bool const lhs_nil = std::holds_alternative<NilValue>(lhs);
    bool const rhs_nil = std::holds_alternative<NilValue>(rhs);
    if (lhs_nil || rhs_nil) {
        if (predicate_id == *numeric_compare_op_index(CompareOp::EQ)) {
            return lhs_nil && rhs_nil;
        }
        if (predicate_id == *numeric_compare_op_index(CompareOp::NE)) {
            return lhs_nil != rhs_nil;
        }
        return std::nullopt;
    }

    if (std::holds_alternative<int64_t>(lhs) && std::holds_alternative<int64_t>(rhs)) {
        auto fixed_fn = impl_->fixed_compare_i64_fns[predicate_id];
        if (fixed_fn == nullptr) {
            return std::nullopt;
        }
        return fixed_fn(std::get<int64_t>(lhs), std::get<int64_t>(rhs)) != 0;
    }

    bool const lhs_arith = std::holds_alternative<int64_t>(lhs) || std::holds_alternative<double>(lhs);
    bool const rhs_arith = std::holds_alternative<int64_t>(rhs) || std::holds_alternative<double>(rhs);
    if (lhs_arith && rhs_arith) {
        auto fixed_fn = impl_->fixed_compare_double_fns[predicate_id];
        if (fixed_fn == nullptr) {
            return std::nullopt;
        }
        double const lhs_double = std::holds_alternative<int64_t>(lhs)
            ? static_cast<double>(std::get<int64_t>(lhs))
            : std::get<double>(lhs);
        double const rhs_double = std::holds_alternative<int64_t>(rhs)
            ? static_cast<double>(std::get<int64_t>(rhs))
            : std::get<double>(rhs);
        return fixed_fn(lhs_double, rhs_double) != 0;
    }

    if (std::holds_alternative<std::string>(lhs) && std::holds_alternative<std::string>(rhs)) {
        auto fixed_fn = impl_->fixed_compare_string_fns[predicate_id];
        if (fixed_fn == nullptr) {
            return std::nullopt;
        }
        return fixed_fn(std::get<std::string>(lhs).c_str(), std::get<std::string>(rhs).c_str()) != 0;
    }

    auto const eq_predicate_id = numeric_compare_op_index(CompareOp::EQ);
    auto const ne_predicate_id = numeric_compare_op_index(CompareOp::NE);
    if (eq_predicate_id && predicate_id == *eq_predicate_id) {
        ConstraintValueCompare compare;
        return !compare(lhs, rhs) && !compare(rhs, lhs);
    }
    if (ne_predicate_id && predicate_id == *ne_predicate_id) {
        ConstraintValueCompare compare;
        return compare(lhs, rhs) || compare(rhs, lhs);
    }

    return std::nullopt;
}

std::optional<std::size_t> MirExecutionPlan::numeric_compare_predicate_id(CompareOp op) const {
    return compare_predicate_id(op);
}

std::optional<std::size_t> MirExecutionPlan::numeric_literal_predicate_id(CompareOp op,
                                                                          ConstraintValue const& literal) const {
    if (impl_ == nullptr || !supported_numeric_compare_op(op)) {
        return std::nullopt;
    }
    std::string const key = numeric_literal_key(op, literal);
    if (key.empty()) {
        return std::nullopt;
    }
    auto it = impl_->literal_predicate_ids.find(key);
    if (it == impl_->literal_predicate_ids.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<bool> MirExecutionPlan::run_numeric_literal_predicate(std::size_t predicate_id,
                                                                    ConstraintValue const& lhs) const {
    if (impl_ == nullptr || predicate_id >= impl_->literal_predicates.size()) {
        return std::nullopt;
    }
    auto const& predicate = impl_->literal_predicates[predicate_id];
    auto predicate_index = numeric_compare_op_index(predicate.op);
    if (!predicate_index) {
        return std::nullopt;
    }
    return run_compare_predicate(*predicate_index, lhs, predicate.literal);
}

std::optional<std::size_t> MirExecutionPlan::numeric_expression_predicate_id(CompareOp op,
                                                                             std::string const& expression) const {
    if (impl_ == nullptr || !supported_numeric_compare_op(op)) {
        return std::nullopt;
    }
    auto it = impl_->numeric_expression_predicate_ids.find(numeric_expression_key(op, expression));
    if (it == impl_->numeric_expression_predicate_ids.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<std::string> const* MirExecutionPlan::numeric_expression_predicate_variables(
    std::size_t predicate_id) const {
    if (impl_ == nullptr || predicate_id >= impl_->numeric_expression_predicates.size()) {
        return nullptr;
    }
    return &impl_->numeric_expression_predicates[predicate_id].variables;
}

std::optional<std::size_t> MirExecutionPlan::numeric_value_expression_id(
    std::string const& expression) const {
    if (impl_ == nullptr) {
        return std::nullopt;
    }
    auto it = impl_->numeric_value_expression_ids.find(expression);
    if (it != impl_->numeric_value_expression_ids.end()) {
        return it->second;
    }
    auto normalized = normalize_expression_whitespace(expression);
    if (normalized != expression) {
        it = impl_->numeric_value_expression_ids.find(normalized);
        if (it != impl_->numeric_value_expression_ids.end()) {
            return it->second;
        }
    }
    return std::nullopt;
}

std::vector<std::string> const* MirExecutionPlan::numeric_value_expression_variables(
    std::size_t expression_id) const {
    if (impl_ == nullptr || expression_id >= impl_->numeric_value_expressions.size()) {
        return nullptr;
    }
    return &impl_->numeric_value_expressions[expression_id].variables;
}

std::optional<bool> MirExecutionPlan::run_numeric_expression_predicate(
    std::size_t predicate_id,
    ConstraintValue const& lhs,
    std::vector<ConstraintValue> const& args) const {
    if (impl_ == nullptr || predicate_id >= impl_->numeric_expression_predicates.size()) {
        return std::nullopt;
    }
    auto const& predicate = impl_->numeric_expression_predicates[predicate_id];
    if (predicate.fn == nullptr || predicate.variables.size() != args.size()
        || (!std::holds_alternative<int64_t>(lhs) && !std::holds_alternative<double>(lhs))) {
        return std::nullopt;
    }

    double lhs_double = std::holds_alternative<int64_t>(lhs)
        ? static_cast<double>(std::get<int64_t>(lhs))
        : std::get<double>(lhs);
    std::vector<double> numeric_args(args.size());
    for (std::size_t index = 0; index < args.size(); ++index) {
        if (std::holds_alternative<int64_t>(args[index])) {
            numeric_args[index] = static_cast<double>(std::get<int64_t>(args[index]));
        } else if (std::holds_alternative<double>(args[index])) {
            numeric_args[index] = std::get<double>(args[index]);
        } else {
            return std::nullopt;
        }
    }

    return reinterpret_cast<Impl::NumericExpressionFn>(predicate.fn)(
        lhs_double,
        static_cast<std::int64_t>(numeric_args.size()),
        numeric_args.empty() ? nullptr : numeric_args.data()) != 0;
}

std::optional<double> MirExecutionPlan::run_numeric_value_expression(
    std::size_t expression_id,
    std::vector<ConstraintValue> const& args) const {
    if (impl_ == nullptr || expression_id >= impl_->numeric_value_expressions.size()) {
        return std::nullopt;
    }
    auto const& expression = impl_->numeric_value_expressions[expression_id];
    if (expression.fn == nullptr || expression.variables.size() != args.size()) {
        return std::nullopt;
    }

    std::vector<double> numeric_args(args.size());
    for (std::size_t index = 0; index < args.size(); ++index) {
        if (std::holds_alternative<int64_t>(args[index])) {
            numeric_args[index] = static_cast<double>(std::get<int64_t>(args[index]));
        } else if (std::holds_alternative<double>(args[index])) {
            numeric_args[index] = std::get<double>(args[index]);
        } else {
            return std::nullopt;
        }
    }

    return expression.fn(static_cast<std::int64_t>(numeric_args.size()),
                         numeric_args.empty() ? nullptr : numeric_args.data());
}

std::optional<double> MirExecutionPlan::run_runtime_numeric_value_expression(
    std::size_t expression_id,
    std::vector<ConstraintValue> const& args) const {
    return run_numeric_value_expression(expression_id, args);
}

std::optional<std::size_t> MirExecutionPlan::value_list_predicate_id(
    CompareOp op,
    std::vector<ConstraintValue> const& values) const {
    if (impl_ == nullptr || (op != CompareOp::In && op != CompareOp::NotIn)) {
        return std::nullopt;
    }
    std::string const key = value_list_key(op, values);
    if (key.empty()) {
        return std::nullopt;
    }
    auto it = impl_->value_list_predicate_ids.find(key);
    if (it == impl_->value_list_predicate_ids.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<bool> MirExecutionPlan::run_value_list_predicate(std::size_t predicate_id,
                                                               ConstraintValue const& lhs) const {
    if (impl_ == nullptr || predicate_id >= impl_->value_list_predicates.size()) {
        return std::nullopt;
    }
    auto const& predicate = impl_->value_list_predicates[predicate_id];
    if (predicate.is_string) {
        if (predicate.string_fn == nullptr || !std::holds_alternative<std::string>(lhs)) {
            return std::nullopt;
        }
        return predicate.string_fn(std::get<std::string>(lhs).c_str()) != 0;
    }
    if (predicate.double_fn == nullptr
        || (!std::holds_alternative<int64_t>(lhs) && !std::holds_alternative<double>(lhs))) {
        return std::nullopt;
    }
    double const lhs_double = std::holds_alternative<int64_t>(lhs)
        ? static_cast<double>(std::get<int64_t>(lhs))
        : std::get<double>(lhs);
    return predicate.double_fn(lhs_double) != 0;
}

std::optional<std::size_t> MirExecutionPlan::eval_expression_predicate_id(
    std::string const& expression) const {
    if (impl_ == nullptr) {
        return std::nullopt;
    }
    auto it = impl_->eval_expression_predicate_ids.find(expression);
    if (it == impl_->eval_expression_predicate_ids.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<std::string> const* MirExecutionPlan::eval_expression_predicate_variables(
    std::size_t predicate_id) const {
    if (impl_ == nullptr || predicate_id >= impl_->eval_expression_predicates.size()) {
        return nullptr;
    }
    return &impl_->eval_expression_predicates[predicate_id].variables;
}

std::optional<bool> MirExecutionPlan::run_eval_expression_predicate(
    std::size_t predicate_id,
    std::vector<ConstraintValue> const& args) const {
    if (impl_ == nullptr || predicate_id >= impl_->eval_expression_predicates.size()) {
        return std::nullopt;
    }
    auto const& predicate = impl_->eval_expression_predicates[predicate_id];
    if (predicate.fn == nullptr || predicate.variables.size() != args.size()) {
        return std::nullopt;
    }

    std::vector<double> numeric_args(args.size());
    for (std::size_t index = 0; index < args.size(); ++index) {
        if (std::holds_alternative<int64_t>(args[index])) {
            numeric_args[index] = static_cast<double>(std::get<int64_t>(args[index]));
        } else if (std::holds_alternative<double>(args[index])) {
            numeric_args[index] = std::get<double>(args[index]);
        } else {
            return std::nullopt;
        }
    }

    return reinterpret_cast<Impl::EvalExpressionFn>(predicate.fn)(
        static_cast<std::int64_t>(numeric_args.size()),
        numeric_args.empty() ? nullptr : numeric_args.data()) != 0;
}

std::optional<std::int64_t> MirExecutionPlan::run_count_aggregate(
    std::int64_t current,
    std::int64_t direction) const {
    if (impl_ == nullptr || impl_->aggregate_count_fn == nullptr) {
        return std::nullopt;
    }
    return impl_->aggregate_count_fn(current, direction);
}

std::optional<double> MirExecutionPlan::run_sum_aggregate(
    double current,
    ConstraintValue const& value,
    std::int64_t direction) const {
    if (impl_ == nullptr || impl_->aggregate_sum_fn == nullptr) {
        return std::nullopt;
    }
    double numeric_value = 0.0;
    if (std::holds_alternative<int64_t>(value)) {
        numeric_value = static_cast<double>(std::get<int64_t>(value));
    } else if (std::holds_alternative<double>(value)) {
        numeric_value = std::get<double>(value);
    } else {
        return std::nullopt;
    }
    return impl_->aggregate_sum_fn(current, numeric_value, direction);
}

std::optional<double> MirExecutionPlan::run_min_aggregate(
    double current,
    ConstraintValue const& value) const {
    if (impl_ == nullptr || impl_->aggregate_min_fn == nullptr) {
        return std::nullopt;
    }
    if (std::holds_alternative<int64_t>(value)) {
        return impl_->aggregate_min_fn(current, static_cast<double>(std::get<int64_t>(value)));
    }
    if (std::holds_alternative<double>(value)) {
        return impl_->aggregate_min_fn(current, std::get<double>(value));
    }
    return std::nullopt;
}

std::optional<double> MirExecutionPlan::run_max_aggregate(
    double current,
    ConstraintValue const& value) const {
    if (impl_ == nullptr || impl_->aggregate_max_fn == nullptr) {
        return std::nullopt;
    }
    if (std::holds_alternative<int64_t>(value)) {
        return impl_->aggregate_max_fn(current, static_cast<double>(std::get<int64_t>(value)));
    }
    if (std::holds_alternative<double>(value)) {
        return impl_->aggregate_max_fn(current, std::get<double>(value));
    }
    return std::nullopt;
}

std::optional<std::int64_t> MirExecutionPlan::run_runtime_count_aggregate(
    std::int64_t current,
    std::int64_t direction) const {
    return run_count_aggregate(current, direction);
}

std::optional<double> MirExecutionPlan::run_runtime_sum_aggregate(
    double current,
    ConstraintValue const& value,
    std::int64_t direction) const {
    return run_sum_aggregate(current, value, direction);
}

std::optional<double> MirExecutionPlan::run_runtime_min_aggregate(
    double current,
    ConstraintValue const& value) const {
    return run_min_aggregate(current, value);
}

std::optional<double> MirExecutionPlan::run_runtime_max_aggregate(
    double current,
    ConstraintValue const& value) const {
    return run_max_aggregate(current, value);
}

std::optional<bool> MirExecutionPlan::run_runtime_collect_list_update(
    std::vector<Fact*>& facts,
    Fact* fact,
    std::int64_t direction) const {
    if (impl_ == nullptr || impl_->collect_list_update_fn == nullptr) {
        return std::nullopt;
    }
    std::int64_t const result = impl_->collect_list_update_fn(&facts, fact, direction);
    if (result < 0) {
        return std::nullopt;
    }
    return result != 0;
}

std::optional<bool> MirExecutionPlan::run_runtime_collect_set_update(
    std::unordered_set<Fact*>& facts,
    Fact* fact,
    std::int64_t direction) const {
    if (impl_ == nullptr || impl_->collect_set_update_fn == nullptr) {
        return std::nullopt;
    }
    std::int64_t const result = impl_->collect_set_update_fn(&facts, fact, direction);
    if (result < 0) {
        return std::nullopt;
    }
    return result != 0;
}

std::optional<FactList> MirExecutionPlan::run_runtime_collect_list_result(
    std::vector<Fact*> const& facts) const {
    if (impl_ == nullptr || impl_->collect_list_result_fn == nullptr) {
        return std::nullopt;
    }
    FactList result;
    if (impl_->collect_list_result_fn(&facts, &result) < 0) {
        return std::nullopt;
    }
    return result;
}

std::optional<FactList> MirExecutionPlan::run_runtime_collect_set_result(
    std::unordered_set<Fact*> const& facts) const {
    if (impl_ == nullptr || impl_->collect_set_result_fn == nullptr) {
        return std::nullopt;
    }
    FactList result;
    if (impl_->collect_set_result_fn(&facts, &result) < 0) {
        return std::nullopt;
    }
    return result;
}

std::optional<bool> MirExecutionPlan::run_runtime_predicate(
    RuntimePredicateRef predicate,
    std::vector<ConstraintValue> const& args) const {
    if (predicate.backend != RuntimePredicateBackend::MirJit) {
        return std::nullopt;
    }

    auto numeric_i64 = [](ConstraintValue const& value) -> std::optional<std::int64_t> {
        if (std::holds_alternative<int64_t>(value)) {
            return std::get<int64_t>(value);
        }
        if (std::holds_alternative<double>(value)) {
            return static_cast<std::int64_t>(std::get<double>(value));
        }
        return std::nullopt;
    };

    switch (predicate.kind) {
        case MirRuntimePredicateKind::Compare:
            if (args.size() != 2) {
                return std::nullopt;
            }
            return run_compare_predicate(predicate.predicate_id, args[0], args[1]);
        case MirRuntimePredicateKind::Temporal: {
            if (args.size() != 2) {
                return std::nullopt;
            }
            auto lhs = numeric_i64(args[0]);
            auto rhs = numeric_i64(args[1]);
            if (!lhs || !rhs) {
                return std::nullopt;
            }
            return run_temporal_predicate(predicate.temporal_op, *lhs, *rhs, predicate.window_ms);
        }
        case MirRuntimePredicateKind::StringContains:
            if (args.size() != 2
                || !std::holds_alternative<std::string>(args[0])
                || !std::holds_alternative<std::string>(args[1])) {
                return std::nullopt;
            }
            return run_string_contains(
                predicate.compare_op, std::get<std::string>(args[0]), std::get<std::string>(args[1]));
        case MirRuntimePredicateKind::StringMatches:
            if (args.size() != 2
                || !std::holds_alternative<std::string>(args[0])
                || !std::holds_alternative<std::string>(args[1])) {
                return std::nullopt;
            }
            return run_string_matches(
                predicate.compare_op, std::get<std::string>(args[0]), std::get<std::string>(args[1]));
        case MirRuntimePredicateKind::StringAffix:
            if (args.size() != 2
                || !std::holds_alternative<std::string>(args[0])
                || !std::holds_alternative<std::string>(args[1])) {
                return std::nullopt;
            }
            return run_string_affix(
                predicate.compare_op, std::get<std::string>(args[0]), std::get<std::string>(args[1]));
        case MirRuntimePredicateKind::StringLengthIs: {
            if (args.size() != 2 || !std::holds_alternative<std::string>(args[0])) {
                return std::nullopt;
            }
            auto expected_length = numeric_i64(args[1]);
            if (!expected_length) {
                return std::nullopt;
            }
            return run_string_length_is(std::get<std::string>(args[0]), *expected_length);
        }
        case MirRuntimePredicateKind::MapContainsKey:
            if (args.size() != 2) {
                return std::nullopt;
            }
            return run_map_contains_key(predicate.compare_op, args[0], args[1]);
        case MirRuntimePredicateKind::CollectionContains:
            if (args.size() != 2) {
                return std::nullopt;
            }
            return run_collection_contains(predicate.compare_op, args[0], args[1]);
        case MirRuntimePredicateKind::NumericLiteral:
            if (args.size() != 1) {
                return std::nullopt;
            }
            return run_numeric_literal_predicate(predicate.predicate_id, args[0]);
        case MirRuntimePredicateKind::NumericExpression: {
            if (args.empty()) {
                return std::nullopt;
            }
            std::vector<ConstraintValue> expression_args(args.begin() + 1, args.end());
            return run_numeric_expression_predicate(predicate.predicate_id, args[0], expression_args);
        }
        case MirRuntimePredicateKind::ValueList:
            if (args.size() != 1) {
                return std::nullopt;
            }
            return run_value_list_predicate(predicate.predicate_id, args[0]);
        case MirRuntimePredicateKind::EvalExpression:
            return run_eval_expression_predicate(predicate.predicate_id, args);
        case MirRuntimePredicateKind::External:
            return std::nullopt;
    }

    return std::nullopt;
}

MirExecutionPlanSummary MirExecutionPlan::summary() const {
    MirExecutionPlanSummary result;
    result.available = impl_ != nullptr;
    result.rule_count = rule_count_;
    result.query_count = query_count_;
    if (impl_ == nullptr) {
        return result;
    }

    result.fixed_i64_compare_count = static_cast<std::size_t>(
        std::count_if(impl_->fixed_compare_i64_fns.begin(),
                      impl_->fixed_compare_i64_fns.end(),
                      [](auto fn) { return fn != nullptr; }));
    result.fixed_double_compare_count = static_cast<std::size_t>(
        std::count_if(impl_->fixed_compare_double_fns.begin(),
                      impl_->fixed_compare_double_fns.end(),
                      [](auto fn) { return fn != nullptr; }));
    result.fixed_string_compare_count = static_cast<std::size_t>(
        std::count_if(impl_->fixed_compare_string_fns.begin(),
                      impl_->fixed_compare_string_fns.end(),
                      [](auto fn) { return fn != nullptr; }));
    result.compare_predicate_count = std::min({
        result.fixed_i64_compare_count,
        result.fixed_double_compare_count,
        result.fixed_string_compare_count,
    });
    result.numeric_literal_predicate_count = impl_->literal_predicates.size();
    result.numeric_expression_predicate_count = impl_->numeric_expression_predicates.size();
    result.numeric_value_expression_count = impl_->numeric_value_expressions.size();
    result.value_list_predicate_count = impl_->value_list_predicates.size();
    result.eval_expression_predicate_count = impl_->eval_expression_predicates.size();
    result.has_generic_i64_compare = impl_->compare_i64_fn != nullptr;
    result.has_generic_double_compare = impl_->compare_double_fn != nullptr;
    result.supports_numeric_compare_predicates =
        result.fixed_i64_compare_count == impl_->fixed_compare_i64_fns.size()
        && result.fixed_double_compare_count == impl_->fixed_compare_double_fns.size();
    result.supports_string_compare_predicates =
        result.fixed_string_compare_count == impl_->fixed_compare_string_fns.size();
    result.supports_string_compare = result.fixed_string_compare_count == impl_->fixed_compare_string_fns.size();
    result.supports_string_contains = impl_->string_contains_fn != nullptr;
    result.supports_string_matches = impl_->string_regex_match_fn != nullptr;
    result.supports_string_affix = impl_->string_starts_with_fn != nullptr && impl_->string_ends_with_fn != nullptr;
    result.supports_string_length_is = impl_->string_length_is_fn != nullptr;
    result.supports_numeric_literal_predicates = !impl_->literal_predicates.empty();
    result.rule_coverage_count = rule_coverages_.size();
    result.query_coverage_count = query_coverages_.size();
    result.rule_graph_count = rule_graphs_.size();
    for (auto const& graph : rule_graphs_) {
        for (auto const& node : graph.nodes) {
            result.rule_graph_predicate_count += node.predicates.size();
        }
    }
    result.query_graph_count = query_graphs_.size();
    for (auto const& graph : query_graphs_) {
        for (auto const& node : graph.nodes) {
            result.query_graph_predicate_count += node.predicates.size();
        }
    }
    result.lowering_errors = {
        "unsupported_operator",
        "unsupported_type",
        "expression_unlowered",
        "collection_helper_unlowered",
        "map_helper_unlowered",
        "dynamic_regex_unlowered",
        "regex_literal_invalid",
        "string_helper_unlowered",
        "forall_pattern_unlowered",
        "eval_pattern_unlowered",
        "accumulate_unlowered",
        "temporal_unlowered",
    };
    return result;
}

std::string MirExecutionPlan::format_summary() const {
    auto const s = summary();
    std::ostringstream out;
    out << "MIR summary: available=" << (s.available ? "true" : "false")
        << ", rules=" << s.rule_count
        << ", queries=" << s.query_count
        << ", fixed_i64_compare=" << s.fixed_i64_compare_count
        << ", fixed_double_compare=" << s.fixed_double_compare_count
        << ", fixed_string_compare=" << s.fixed_string_compare_count
        << ", compare_predicates=" << s.compare_predicate_count
        << ", numeric_literal_predicates=" << s.numeric_literal_predicate_count
        << ", numeric_expression_predicates=" << s.numeric_expression_predicate_count
        << ", numeric_value_expressions=" << s.numeric_value_expression_count
        << ", value_list_predicates=" << s.value_list_predicate_count
        << ", eval_expression_predicates=" << s.eval_expression_predicate_count
        << ", generic_i64_compare=" << (s.has_generic_i64_compare ? "true" : "false")
        << ", generic_double_compare=" << (s.has_generic_double_compare ? "true" : "false")
        << ", numeric_compare_predicates=" << (s.supports_numeric_compare_predicates ? "true" : "false")
        << ", string_compare_predicates=" << (s.supports_string_compare_predicates ? "true" : "false")
        << ", string_compare_support=" << (s.supports_string_compare ? "true" : "false")
        << ", string_contains_support=" << (s.supports_string_contains ? "true" : "false")
        << ", string_matches_support=" << (s.supports_string_matches ? "true" : "false")
        << ", string_affix_support=" << (s.supports_string_affix ? "true" : "false")
        << ", string_length_is_support=" << (s.supports_string_length_is ? "true" : "false")
        << ", numeric_literal_support=" << (s.supports_numeric_literal_predicates ? "true" : "false")
        << ", rule_coverage=" << s.rule_coverage_count
        << ", query_coverage=" << s.query_coverage_count
        << ", rule_graphs=" << s.rule_graph_count
        << ", rule_graph_predicates=" << s.rule_graph_predicate_count
        << ", query_graphs=" << s.query_graph_count
        << ", query_graph_predicates=" << s.query_graph_predicate_count
        << ", lowering_errors=";
    if (s.lowering_errors.empty()) {
        out << "none";
    } else {
        for (std::size_t index = 0; index < s.lowering_errors.size(); ++index) {
            if (index != 0) {
                out << "|";
            }
            out << s.lowering_errors[index];
        }
    }
    return out.str();
}

std::string MirExecutionPlan::format_debug_dump() const {
    std::ostringstream out;
    out << format_summary() << "\n";
    out << "MIR rule coverage:\n";
    if (rule_coverages_.empty()) {
        out << "  none\n";
    } else {
        for (auto const& coverage : rule_coverages_) {
            out << "  rule[" << coverage.rule_index << "] \"" << coverage.rule_name << "\""
                << ": constraints=" << coverage.constraint_count
                << ", compare_predicates=" << coverage.compare_predicate_count
                << ", literal_predicates=" << coverage.literal_predicate_count
                << ", lowering_error_constraints=" << coverage.lowering_error_constraint_count
                << ", lowering_errors=";
            if (coverage.lowering_errors.empty()) {
                out << "none";
            } else {
                for (std::size_t index = 0; index < coverage.lowering_errors.size(); ++index) {
                    if (index != 0) {
                        out << "|";
                    }
                    out << coverage.lowering_errors[index];
                }
            }
            out << "\n";
        }
    }
    out << "MIR rule graphs:\n";
    if (rule_graphs_.empty()) {
        out << "  none\n";
    } else {
        for (auto const& graph : rule_graphs_) {
            std::size_t predicate_count = 0;
            for (auto const& node : graph.nodes) {
                predicate_count += node.predicates.size();
            }
            out << "  rule_graph[" << graph.rule_index << ":" << graph.condition_group_index << "] \""
                << graph.rule_name << "\""
                << ": patterns=" << graph.nodes.size()
                << ", predicates=" << predicate_count
                << "\n";
            for (auto const& node : graph.nodes) {
                out << "    pattern[" << node.pattern_index << "]"
                    << " depth=" << node.depth
                    << ", parent=" << (node.parent_node_index ? std::to_string(*node.parent_node_index) : "-")
                    << ", kind=" << rule_graph_node_kind_name(node.kind)
                    << ", source=" << (node.source_kind.empty() ? "-" : node.source_kind)
                    << ", binding=" << (node.binding.empty() ? "-" : node.binding)
                    << ", fact_type=" << (node.fact_type.empty() ? "-" : node.fact_type)
                    << ", constraints=" << node.constraint_count
                    << ", predicates=" << node.predicates.size()
                    << ", abi=current_fact:" << (node.input_abi.reads_current_fact ? "true" : "false")
                    << "|token_facts:" << (node.input_abi.reads_token_facts ? "true" : "false")
                    << "|field_accessor:" << (node.input_abi.uses_field_accessor ? "true" : "false")
                    << "|missing_non_match:" << (node.input_abi.missing_field_is_non_match ? "true" : "false")
                    << "|explicit_nil_value:" << (node.input_abi.explicit_nil_is_value ? "true" : "false")
                    << "\n";
                for (std::size_t predicate_index = 0; predicate_index < node.predicates.size(); ++predicate_index) {
                    auto const& predicate = node.predicates[predicate_index];
                    out << "      predicate[" << predicate_index << "] role="
                        << rule_graph_predicate_role_name(predicate.role)
                        << ", backend=" << runtime_predicate_backend_name(predicate.ref.backend)
                        << ", kind=" << static_cast<int>(predicate.ref.kind)
                        << ", id=" << predicate.ref.predicate_id
                        << ", external=" << (predicate.ref.external_name.empty() ? "-" : predicate.ref.external_name)
                        << "\n";
                }
            }
        }
    }
    out << "MIR query coverage:\n";
    if (query_coverages_.empty()) {
        out << "  none\n";
    } else {
        for (auto const& coverage : query_coverages_) {
            out << "  query[" << coverage.query_index << "] \"" << coverage.query_name << "\""
                << ": constraints=" << coverage.constraint_count
                << ", compare_predicates=" << coverage.compare_predicate_count
                << ", literal_predicates=" << coverage.literal_predicate_count
                << ", lowering_error_constraints=" << coverage.lowering_error_constraint_count
                << ", lowering_errors=";
            if (coverage.lowering_errors.empty()) {
                out << "none";
            } else {
                for (std::size_t index = 0; index < coverage.lowering_errors.size(); ++index) {
                    if (index != 0) {
                        out << "|";
                    }
                    out << coverage.lowering_errors[index];
                }
            }
            out << "\n";
        }
    }
    out << "MIR query graphs:\n";
    if (query_graphs_.empty()) {
        out << "  none\n";
    } else {
        for (auto const& graph : query_graphs_) {
            std::size_t predicate_count = 0;
            for (auto const& node : graph.nodes) {
                predicate_count += node.predicates.size();
            }
            out << "  query_graph[" << graph.query_index << "] \"" << graph.query_name << "\""
                << ": patterns=" << graph.nodes.size()
                << ", predicates=" << predicate_count
                << "\n";
            for (auto const& node : graph.nodes) {
                out << "    pattern[" << node.pattern_index << "]"
                    << " binding=" << (node.binding.empty() ? "-" : node.binding)
                    << ", fact_type=" << (node.fact_type.empty() ? "-" : node.fact_type)
                    << ", constraints=" << node.constraint_count
                    << ", predicates=" << node.predicates.size()
                    << "\n";
                for (std::size_t predicate_index = 0; predicate_index < node.predicates.size(); ++predicate_index) {
                    auto const& predicate = node.predicates[predicate_index];
                    out << "      predicate[" << predicate_index << "]"
                        << " backend=" << runtime_predicate_backend_name(predicate.backend)
                        << ", kind=" << static_cast<int>(predicate.kind)
                        << ", id=" << predicate.predicate_id
                        << ", external=" << (predicate.external_name.empty() ? "-" : predicate.external_name)
                        << "\n";
                }
            }
        }
    }
    return out.str();
}

} // namespace rulesforge
