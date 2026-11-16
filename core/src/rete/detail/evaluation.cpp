#include "diagnostics/logging_control.hpp"

#include "runtime/knowledge_base.hpp"
#include "runtime/stateful_session.hpp"
#include "expression_descriptor.hpp"
#include "text/scalar_text.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iosfwd>
#include <chrono>

#include <sstream>
#include <span>
#include <stdexcept>
#include <typeinfo>
#include <utility>

using namespace rulesforge;

#include "rete/detail/evaluation.hpp"
#include "diagnostics/rete_counters.hpp"

namespace rulesforge::rete_detail {

bool changed_fields_contains(rulesforge::ModifiedFieldsHint const* changed_fields,
                             std::string_view field) {
    if (!changed_fields) return true;
    return changed_fields->contains(field);
}


std::optional<RuntimeNumeric> comparison_numeric(ConstraintValue const& value) {
    if (auto const* i = std::get_if<int64_t>(&value)) {
        return *i;
    }
    if (auto const* u = std::get_if<uint64_t>(&value)) {
        return *u;
    }
    if (auto const* d = std::get_if<double>(&value)) {
        return *d;
    }
    if (auto const* duration = std::get_if<DurationValue>(&value)) {
        return duration->milliseconds;
    }
    if (auto const* enumeration = std::get_if<EnumValue>(&value)) {
        return std::visit([](auto numeric) -> RuntimeNumeric { return numeric; }, enumeration->value);
    }
    return std::nullopt;
}

std::optional<double> numeric_value(ConstraintValue const& value) {
    auto numeric = comparison_numeric(value);
    if (!numeric) return std::nullopt;
    return std::visit([](auto number) { return static_cast<double>(number); }, *numeric);
}

int compare_numeric(RuntimeNumeric const& lhs, RuntimeNumeric const& rhs) {
    if (auto const* lhs_double = std::get_if<double>(&lhs)) {
        double const rhs_double = std::visit([](auto value) { return static_cast<double>(value); }, rhs);
        return *lhs_double < rhs_double ? -1 : (*lhs_double > rhs_double ? 1 : 0);
    }
    if (auto const* rhs_double = std::get_if<double>(&rhs)) {
        double const lhs_double = std::visit([](auto value) { return static_cast<double>(value); }, lhs);
        return lhs_double < *rhs_double ? -1 : (lhs_double > *rhs_double ? 1 : 0);
    }
    if (auto const* lhs_signed = std::get_if<int64_t>(&lhs)) {
        if (auto const* rhs_signed = std::get_if<int64_t>(&rhs)) {
            return *lhs_signed < *rhs_signed ? -1 : (*lhs_signed > *rhs_signed ? 1 : 0);
        }
        if (*lhs_signed < 0) return -1;
        auto const lhs_unsigned = static_cast<uint64_t>(*lhs_signed);
        auto const rhs_unsigned = std::get<uint64_t>(rhs);
        return lhs_unsigned < rhs_unsigned ? -1 : (lhs_unsigned > rhs_unsigned ? 1 : 0);
    }
    auto const lhs_unsigned = std::get<uint64_t>(lhs);
    if (auto const* rhs_signed = std::get_if<int64_t>(&rhs)) {
        if (*rhs_signed < 0) return 1;
        auto const rhs_unsigned = static_cast<uint64_t>(*rhs_signed);
        return lhs_unsigned < rhs_unsigned ? -1 : (lhs_unsigned > rhs_unsigned ? 1 : 0);
    }
    auto const rhs_unsigned = std::get<uint64_t>(rhs);
    return lhs_unsigned < rhs_unsigned ? -1 : (lhs_unsigned > rhs_unsigned ? 1 : 0);
}

bool evaluate_ordered_compare(CompareOp op, int comparison) {
    switch (op) {
        case CompareOp::EQ: return comparison == 0;
        case CompareOp::NE: return comparison != 0;
        case CompareOp::GT: return comparison > 0;
        case CompareOp::LT: return comparison < 0;
        case CompareOp::GE: return comparison >= 0;
        case CompareOp::LE: return comparison <= 0;
        default: return false;
    }
}

std::optional<uint64_t> bit_mask_value(ConstraintValue const& value) {
    if (auto const* unsigned_value = std::get_if<uint64_t>(&value)) {
        return *unsigned_value;
    }
    if (auto const* signed_value = std::get_if<int64_t>(&value); signed_value && *signed_value >= 0) {
        return static_cast<uint64_t>(*signed_value);
    }
    return std::nullopt;
}

bool evaluate_compare(CompareOp op, ConstraintValue const& lhs, ConstraintValue const& rhs) {
    if (op == CompareOp::HasFlag) {
        auto const lhs_bits = bit_mask_value(lhs);
        auto const mask = bit_mask_value(rhs);
        return lhs_bits && mask && *mask != 0 && (*lhs_bits & *mask) == *mask;
    }
    if (auto lhs_num = comparison_numeric(lhs)) {
        if (auto rhs_num = comparison_numeric(rhs)) {
            return evaluate_ordered_compare(op, compare_numeric(*lhs_num, *rhs_num));
        }
    }

    auto const* raw_lhs_str = std::get_if<std::string>(&lhs);
    if (raw_lhs_str && op == CompareOp::LengthIs) {
        if (auto rhs_num = comparison_numeric(rhs)) {
            return compare_numeric(RuntimeNumeric{static_cast<uint64_t>(raw_lhs_str->size())},
                                   *rhs_num) == 0;
        }
        return false;
    }
    ScalarTextView const lhs_str(lhs);
    ScalarTextView const rhs_str(rhs);
    if (lhs_str && rhs_str) {
        switch (op) {
            case CompareOp::EQ: return *lhs_str == *rhs_str;
            case CompareOp::NE: return *lhs_str != *rhs_str;
            case CompareOp::GT: return *lhs_str > *rhs_str;
            case CompareOp::LT: return *lhs_str < *rhs_str;
            case CompareOp::GE: return *lhs_str >= *rhs_str;
            case CompareOp::LE: return *lhs_str <= *rhs_str;
            case CompareOp::Contains: return lhs_str->find(*rhs_str) != std::string::npos;
            case CompareOp::NotContains: return lhs_str->find(*rhs_str) == std::string::npos;
            case CompareOp::StartsWith: return lhs_str->rfind(*rhs_str, 0) == 0;
            case CompareOp::EndsWith:
                return lhs_str->size() >= rhs_str->size()
                    && lhs_str->compare(lhs_str->size() - rhs_str->size(), rhs_str->size(), *rhs_str) == 0;
            default: return false;
        }
    }

    if (op == CompareOp::EQ || op == CompareOp::NE) {
        bool const equal = ConstraintValueEquals{}(lhs, rhs);
        return op == CompareOp::EQ ? equal : !equal;
    }
    return false;
}

bool evaluate_literal_membership(ParsedConstraint const& constraint, ConstraintValue const& value) {
    if (!constraint.right_value_list)
        throw std::logic_error("IN predicate is missing its literal list");
    bool found = std::any_of(constraint.right_value_list->begin(), constraint.right_value_list->end(),
        [&](ConstraintValue const& candidate) { return evaluate_compare(CompareOp::EQ, value, candidate); });
    return constraint.op == CompareOp::In ? found : !found;
}

bool evaluate_temporal(TemporalOp op, std::int64_t lhs, std::int64_t rhs, std::int64_t window_ms) {
    switch (op) {
        case TemporalOp::After:
            return lhs > rhs;
        case TemporalOp::Before:
            return lhs < rhs;
        case TemporalOp::Within:
            return window_ms >= 0 && std::llabs(lhs - rhs) <= window_ms;
        case TemporalOp::Coincides:
            return lhs == rhs;
        case TemporalOp::During:
            return lhs >= rhs;
        case TemporalOp::None:
            return false;
    }
    return false;
}

std::string trim_ascii_copy(std::string value) {
    auto const not_space = [](unsigned char ch) { return std::isspace(ch) == 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::optional<ConstraintValue> evaluate_runtime_expression(
    std::shared_ptr<rulesforge::ExpressionDescriptor> const& program,
    Fact const& fact, Token const* token, std::map<std::string, int> const* bindings,
    std::map<std::string, std::string> const* scalar_fields) {
    if (!program) throw std::logic_error("Scalar expression reached RETE without MIR lowering");
    struct Context {
        Fact const& fact;
        Token const* token;
        std::map<std::string, int> const* bindings;
        std::map<std::string, std::string> const* scalar_fields;
    } context{fact, token, bindings, scalar_fields};
    return program->evaluate([](void* opaque, rulesforge::ExpressionDescriptor::Variable const& access)
        -> std::optional<ConstraintValue> {
        auto& c = *static_cast<Context*>(opaque);
        auto const* source = &c.fact;
        auto const* path = &access.path;
        bool direct = access.field == "this";
        std::string const* scalar_field = nullptr;
        if (direct && c.scalar_fields) {
            auto found = c.scalar_fields->find(access.binding);
            if (found != c.scalar_fields->end()) { scalar_field = &found->second; direct = false; }
        }
        if (c.bindings) {
            auto binding = c.bindings->find(access.binding);
            if (binding != c.bindings->end() && c.token) {
                if (auto* bound = c.token->get_fact_at_depth(binding->second)) source = bound;
                else if (binding->second < c.token->get_depth()) return std::nullopt;
            } else if (binding == c.bindings->end()) {
                if (!direct || scalar_field) return std::nullopt;
                path = &access.unqualified_path; direct = false;
            }
        } else if (direct) { path = &access.unqualified_path; direct = false; }
        if (direct) return static_cast<int64_t>(source->id);
        return scalar_field ? source->get_field(*scalar_field) : source->get_field(*path);
    }, &context);
}

bool alpha_constraint_affected(ParsedConstraint const& c,
                               rulesforge::ModifiedFieldsHint const* changed_fields) {
    if (!changed_fields) return true;
    if (c.right_arith_expr) return true;  // conservative: expression dependencies are not tracked
    if (c.left_field == "this" || c.left_field.empty()) return true;

    if (!c.cached_left_field_path.empty() && !c.cached_left_field_path[0].name.empty()) {
        return changed_fields_contains(changed_fields, c.cached_left_field_path[0].name);
    }

    size_t end = c.left_field.find_first_of(".[");
    std::string_view root = (end == std::string::npos)
        ? std::string_view(c.left_field)
        : std::string_view(c.left_field.data(), end);
    if (root.empty()) return true;
    return changed_fields_contains(changed_fields, root);
}

[[noreturn]] void throw_lhs_runtime_not_lowered(char const* feature) {
    std::ostringstream message;
    message << feature << " reached RETE runtime without native lowering";
    throw std::runtime_error(message.str());
}

[[noreturn]] void throw_lhs_runtime_predicate_failed(
    rulesforge::RuntimePredicateRef const& predicate,
    char const* feature) {
    if (predicate.backend == rulesforge::RuntimePredicateBackend::Native) {
        std::ostringstream message;
        message << feature << " native";
        if (!predicate.external_name.empty()) {
            message << " '" << predicate.external_name << "'";
        }
        message << " predicate failed at runtime";
        throw std::runtime_error(message.str());
    }
    throw_lhs_runtime_not_lowered(feature);
}

void print_join_constraints_with_runtime_predicates(
    std::ostream& os,
    std::vector<ParsedConstraint> const& joins,
    std::vector<std::optional<rulesforge::RuntimePredicateRef>> const& runtime_predicates) {
    for (std::size_t index = 0; index < joins.size(); ++index) {
        os << "\\n" << constraint_to_string(joins[index]);
        if (index < runtime_predicates.size() && runtime_predicates[index]) {
            auto const& predicate = *runtime_predicates[index];
            os << " [mir#" << static_cast<int>(predicate.kind) << ":" << predicate.predicate_id << "]";
        }
    }
}

bool check_all_join_conditions(StatefulSession& session, Token const& token, Fact const& fact,
                               std::vector<ParsedConstraint> const& joins,
                               std::map<std::string, int> const& bindings,
                               std::map<std::string, std::string> const& scalar_binding_fields,
                               std::vector<std::optional<rulesforge::RuntimePredicateRef>> const* runtime_predicates) {
    g_join_checks.fetch_add(1, std::memory_order_relaxed);
    if (joins.empty()) { return true; }

    for (std::size_t join_index = 0; join_index < joins.size(); ++join_index) {
        auto const& join = joins[join_index];
        auto const* runtime_predicate = (runtime_predicates != nullptr
            && join_index < runtime_predicates->size())
            ? &(*runtime_predicates)[join_index]
            : nullptr;
        if (join.temporal_constraint) {
            auto const& tc = *join.temporal_constraint;
            auto lhs_val_opt = fact.get_field(tc.lhs_field);
            g_field_lookups.fetch_add(1, std::memory_order_relaxed);
            auto it = bindings.find(tc.rhs_binding_and_field.first);
            if (it == bindings.end()) return false;

            auto bound_fact = token.get_fact_at_depth(it->second);   // Get fact directly from token
            if (!bound_fact) return false;
            auto rhs_val_opt = bound_fact->get_field(tc.rhs_binding_and_field.second);
            g_field_lookups.fetch_add(1, std::memory_order_relaxed);

            if (!lhs_val_opt || !rhs_val_opt) return false;
            auto lhs_ts = std::get_if<int64_t>(&*lhs_val_opt);
            auto rhs_ts = std::get_if<int64_t>(&*rhs_val_opt);
            if (!lhs_ts || !rhs_ts) return false;
            if (!evaluate_temporal(tc.op, *lhs_ts, *rhs_ts, tc.window_ms)) return false;
            continue;
        }
        std::optional<ConstraintValue> lhs_val_opt;
        std::optional<ConstraintValue> rhs_val_opt;

        if (join.left_binding) {
            auto it = bindings.find(*join.left_binding);
            if (it == bindings.end()) return false;

            auto bound_fact = token.get_fact_at_depth(it->second);
            if (!bound_fact) {
                // Current-pattern binding can legally point past current token depth.
                // In that case, resolve it against the currently tested fact.
                if (it->second >= token.get_depth()) {
                    bound_fact = const_cast<Fact*>(&fact);
                } else {
                    return false;
                }
            }

            // Use cached path if available
            if (!join.cached_left_field_path.empty()) {
                lhs_val_opt = bound_fact->get_field(join.cached_left_field_path);
            } else {
                lhs_val_opt = bound_fact->get_field(join.left_field);
            }
            g_field_lookups.fetch_add(1, std::memory_order_relaxed);

        } else {
            if (!join.cached_left_field_path.empty()) {
                lhs_val_opt = fact.get_field(join.cached_left_field_path);
            } else {
                lhs_val_opt = fact.get_field(join.left_field);
            }
            g_field_lookups.fetch_add(1, std::memory_order_relaxed);
        }

        if (join.op == CompareOp::In || join.op == CompareOp::NotIn) {
            if (!lhs_val_opt || !evaluate_literal_membership(join, *lhs_val_opt)) return false;
            continue;
        }
        if (join.right_bound_field) {
            auto it = bindings.find(join.right_bound_field->first);
            if (it == bindings.end()) return false;

            auto bound_fact = token.get_fact_at_depth(it->second);
            if (!bound_fact) {
                // Match LHS binding resolution: allow current-pattern binding on RHS.
                if (it->second >= token.get_depth()) {
                    bound_fact = const_cast<Fact*>(&fact);
                } else {
                    return false;
                }
            }

            // Use cached RHS path if available
            if (!join.cached_right_field_path.empty()) {
                rhs_val_opt = bound_fact->get_field(join.cached_right_field_path);
            } else {
                rhs_val_opt = bound_fact->get_field(join.right_bound_field->second);
            }
            g_field_lookups.fetch_add(1, std::memory_order_relaxed);

        } else if (join.right_arith_expr) {
            if (!lhs_val_opt) {
                return false;
            }
            auto rhs_expr_value = evaluate_runtime_expression(
                join.right_arith_program,
                fact,
                &token,
                &bindings,
                &scalar_binding_fields);
            if (!rhs_expr_value) {
                return false;
            }
            rhs_val_opt = std::move(rhs_expr_value);
        } else if (join.right_literal) {
            rhs_val_opt = join.right_literal;
        } else {
            continue;   // Should not happen for a join constraint
        }

        if (!lhs_val_opt || !rhs_val_opt) {
            return false;
        }
        ConstraintValue lhs = *lhs_val_opt;
        ConstraintValue rhs = *rhs_val_opt;
        (void)runtime_predicate;
        if (!evaluate_compare(join.op, lhs, rhs)) {
            return false;
        }
    }

    return true;
}

std::optional<ConstraintValue> resolve_constraint_field(
    ParsedConstraint const& constraint,
    bool left_side,
    Fact const& current_fact,
    Token const& token,
    std::map<std::string, int> const& bindings) {
    std::optional<std::string> binding;
    std::string field;
    std::vector<PathSegment> const* cached_path = nullptr;

    if (left_side) {
        binding = constraint.left_binding;
        field = constraint.left_field;
        cached_path = &constraint.cached_left_field_path;
    } else if (constraint.right_bound_field) {
        binding = constraint.right_bound_field->first;
        field = constraint.right_bound_field->second;
        cached_path = &constraint.cached_right_field_path;
    } else {
        return std::nullopt;
    }

    Fact const* source_fact = &current_fact;
    if (binding) {
        auto it = bindings.find(*binding);
        if (it == bindings.end()) {
            return std::nullopt;
        }
        auto bound_fact = token.get_fact_at_depth(it->second);
        if (bound_fact) {
            source_fact = bound_fact;
        } else if (it->second < token.get_depth()) {
            return std::nullopt;
        }
    }

    if (field == "this") {
        return ConstraintValue{static_cast<int64_t>(source_fact->id)};
    }
    if (cached_path != nullptr && !cached_path->empty()) {
        return source_fact->get_field(*cached_path);
    }
    return source_fact->get_field(field);
}

bool check_all_runtime_constraints(
    StatefulSession& session,
    Token const& token,
    Fact const& fact,
    std::span<ParsedConstraint const> constraints,
    std::map<std::string, int> const& bindings,
    std::span<std::optional<rulesforge::RuntimePredicateRef> const> runtime_predicates,
    std::map<std::string, std::string> const& scalar_binding_fields) {
    if (constraints.empty()) {
        return true;
    }

    auto kb = session.get_knowledge_base();

    for (std::size_t index = 0; index < constraints.size(); ++index) {
        auto const& constraint = constraints[index];
        if (constraint.op == CompareOp::None) {
            continue;
        }
        auto const* runtime_predicate = (index < runtime_predicates.size())
            ? &runtime_predicates[index]
            : nullptr;
        bool const can_use_runtime_predicate =
            runtime_predicate != nullptr && *runtime_predicate && kb && !(*runtime_predicate)->external_name.empty();

        if (constraint.temporal_constraint) {
            auto const& temporal = *constraint.temporal_constraint;
            auto lhs = fact.get_field(temporal.lhs_field);
            auto it = bindings.find(temporal.rhs_binding_and_field.first);
            if (!lhs || it == bindings.end()) {
                return false;
            }
            auto bound_fact = token.get_fact_at_depth(it->second);
            if (!bound_fact && it->second >= token.get_depth()) {
                bound_fact = const_cast<Fact*>(&fact);
            }
            if (!bound_fact) {
                return false;
            }
            auto rhs = bound_fact->get_field(temporal.rhs_binding_and_field.second);
            if (!rhs) {
                return false;
            }
            if (can_use_runtime_predicate) {
                auto result = kb->runtime_predicate(*(*runtime_predicate), {*lhs, *rhs});
                if (!result) {
                    throw_lhs_runtime_predicate_failed(*(*runtime_predicate), "unnest temporal predicate");
                }
                if (!*result) {
                    return false;
                }
                continue;
            }
            auto const* lhs_ts = std::get_if<std::int64_t>(&*lhs);
            auto const* rhs_ts = std::get_if<std::int64_t>(&*rhs);
            if (!lhs_ts || !rhs_ts) {
                return false;
            }
            if (!evaluate_temporal(temporal.op, *lhs_ts, *rhs_ts, temporal.window_ms)) return false;
            continue;
        }

        auto lhs = resolve_constraint_field(constraint, true, fact, token, bindings);
        if (!lhs) {
            return false;
        }

        if (constraint.op == CompareOp::In || constraint.op == CompareOp::NotIn) {
            if (!evaluate_literal_membership(constraint, *lhs)) return false;
            continue;
        }

        if (can_use_runtime_predicate) {
            switch ((*runtime_predicate)->kind) {
                case rulesforge::RuntimePredicateKind::NumericLiteral:
                case rulesforge::RuntimePredicateKind::ValueList: {
                    auto result = kb->runtime_predicate(*(*runtime_predicate), {*lhs});
                    if (!result) {
                        throw_lhs_runtime_predicate_failed(*(*runtime_predicate), "unnest unary predicate");
                    }
                    if (!*result) {
                        return false;
                    }
                    continue;
                }
                case rulesforge::RuntimePredicateKind::CollectionContains:
                    if (constraint.right_value_list) {
                        auto rhs_list = make_typed_list(*constraint.right_value_list);
                        auto result = kb->runtime_predicate(*(*runtime_predicate), {*lhs, rhs_list});
                        if (!result) {
                            throw_lhs_runtime_predicate_failed(
                                *(*runtime_predicate),
                                "unnest collection value-list predicate");
                        }
                        if (!*result) {
                            return false;
                        }
                        continue;
                    }
                    break;
                default:
                    break;
            }
        }

        std::optional<ConstraintValue> rhs;
        if (constraint.right_bound_field) {
            rhs = resolve_constraint_field(constraint, false, fact, token, bindings);
        } else if (constraint.right_arith_expr) {
            rhs = evaluate_runtime_expression(constraint.right_arith_program, fact, &token, &bindings,
                                              &scalar_binding_fields);
        } else if (constraint.right_literal) {
            rhs = constraint.right_literal;
        } else {
            rhs = ConstraintValue{NilValue{}};
        }
        if (!rhs) {
            return false;
        }

        if (can_use_runtime_predicate) {
            auto result = kb->runtime_predicate(*(*runtime_predicate), {*lhs, *rhs});
            if (!result) {
                throw_lhs_runtime_predicate_failed(*(*runtime_predicate), "unnest binary predicate");
            }
            if (!*result) {
                return false;
            }
            continue;
        }
        if (!evaluate_compare(constraint.op, *lhs, *rhs)) {
            return false;
        }
    }

    return true;
}

// Preserve the parsed AND/OR tree and short-circuit behavior while reusing
// the same field resolution and typed predicates as other derived facts.
// Inactive branches still advance the leaf index, without evaluating them.
bool check_accumulate_result(StatefulSession& session, Token const& token, Fact const& fact,
    ConstraintNode const* root, std::span<ParsedConstraint const> constraints,
    std::map<std::string, int> const& bindings,
    std::map<std::string, std::string> const& scalar_binding_fields,
    std::span<std::optional<rulesforge::RuntimePredicateRef> const> predicates,
    size_t& leaf, bool active) {
    if (!root) return true;
    if (root->type == NodeType::LEAF) {
        size_t index = leaf++;
        return active && check_all_runtime_constraints(session, token, fact,
            constraints.subspan(index, 1), bindings, predicates.subspan(index, 1), scalar_binding_fields);
    }
    bool result = root->type == NodeType::AND;
    for (auto const& child : root->children) {
        bool evaluate = active && (root->type == NodeType::AND ? result : !result);
        bool match = check_accumulate_result(session, token, fact, child.get(),
            constraints, bindings, scalar_binding_fields, predicates, leaf, evaluate);
        if (evaluate) result = match;
    }
    return active && result;
}


} // namespace rulesforge::rete_detail
