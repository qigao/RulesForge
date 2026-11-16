#pragma once

#include "model/constraint_types.hpp"
#include "model/token.hpp"
#include "execution/runtime_predicate.hpp"
#include <algorithm>
#include <iosfwd>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

class StatefulSession;
namespace rulesforge {
class ExpressionDescriptor;
struct ModifiedFieldsHint;
}

// Private node implementation contracts. Fact/session ownership stays with the
// existing owners; these functions borrow their arguments synchronously.
namespace rulesforge::rete_detail {
using RuntimeNumeric = std::variant<int64_t, uint64_t, double>;

bool changed_fields_contains(ModifiedFieldsHint const* fields, std::string_view field);
std::optional<RuntimeNumeric> comparison_numeric(ConstraintValue const& value);
std::optional<double> numeric_value(ConstraintValue const& value);
int compare_numeric(RuntimeNumeric const& lhs, RuntimeNumeric const& rhs);
bool evaluate_ordered_compare(CompareOp op, int comparison);
std::optional<uint64_t> bit_mask_value(ConstraintValue const& value);
bool evaluate_compare(CompareOp op, ConstraintValue const& lhs, ConstraintValue const& rhs);
bool evaluate_literal_membership(ParsedConstraint const& constraint, ConstraintValue const& value);
bool evaluate_temporal(TemporalOp op, int64_t lhs, int64_t rhs, int64_t window_ms);
std::string trim_ascii_copy(std::string value);

std::optional<ConstraintValue> evaluate_runtime_expression(
    std::shared_ptr<ExpressionDescriptor> const& program, Fact const& fact,
    Token const* token, std::map<std::string, int> const* bindings,
    std::map<std::string, std::string> const* scalar_fields = nullptr);

bool alpha_constraint_affected(ParsedConstraint const& constraint, ModifiedFieldsHint const* fields);
[[noreturn]] void throw_lhs_runtime_not_lowered(char const* feature);
[[noreturn]] void throw_lhs_runtime_predicate_failed(
    RuntimePredicateRef const& predicate, char const* feature);
void print_join_constraints_with_runtime_predicates(
    std::ostream& out, std::vector<ParsedConstraint> const& joins,
    std::vector<std::optional<RuntimePredicateRef>> const& predicates);

bool check_all_join_conditions(StatefulSession& session, Token const& token,
    Fact const& fact, std::vector<ParsedConstraint> const& joins,
    std::map<std::string, int> const& bindings,
    std::map<std::string, std::string> const& scalar_fields = {},
    std::vector<std::optional<RuntimePredicateRef>> const* predicates = nullptr);

std::optional<ConstraintValue> resolve_constraint_field(
    ParsedConstraint const& constraint, bool left_side, Fact const& fact,
    Token const& token, std::map<std::string, int> const& bindings);
bool check_all_runtime_constraints(StatefulSession& session, Token const& token,
    Fact const& fact, std::span<ParsedConstraint const> constraints,
    std::map<std::string, int> const& bindings,
    std::span<std::optional<RuntimePredicateRef> const> predicates,
    std::map<std::string, std::string> const& scalar_fields = {});
bool check_accumulate_result(StatefulSession& session, Token const& token,
    Fact const& fact, ConstraintNode const* tree,
    std::span<ParsedConstraint const> constraints,
    std::map<std::string, int> const& bindings,
    std::map<std::string, std::string> const& scalar_fields,
    std::span<std::optional<RuntimePredicateRef> const> predicates,
    size_t& leaf, bool active = true);

template<typename T>
void remove_from_vector(std::vector<T>& values, T const& item) {
    auto found = std::find(values.begin(), values.end(), item);
    if (found != values.end()) {
        *found = std::move(values.back());
        values.pop_back();
    }
}
} // namespace rulesforge::rete_detail
