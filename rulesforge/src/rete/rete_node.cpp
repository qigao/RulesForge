#include "core/logging_control.hpp"

#include "engine/knowledge_base.hpp"
#include "rete/rete_node.hpp"
#include "engine/stateful_session.hpp"
#include "turbo_parser.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iosfwd>
#include <chrono>

#include <sstream>
#include <stdexcept>
#include <typeinfo>
#include <utility>

using namespace rulesforge;

// --- Helper Functions ---
namespace {
    std::atomic<uint64_t> g_alpha_checks{0};
    std::atomic<uint64_t> g_join_checks{0};
    std::atomic<uint64_t> g_compare_calls{0};
    std::atomic<uint64_t> g_field_lookups{0};

    bool changed_fields_contains(rulesforge::ModifiedFieldsHint const* changed_fields,
                                 std::string_view field) {
        if (!changed_fields) return true;
        return changed_fields->contains(field);
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
        message << feature << " reached RETE runtime without MIR lowering";
        throw std::runtime_error(message.str());
    }

    [[noreturn]] void throw_lhs_runtime_predicate_failed(
        rulesforge::MirRuntimePredicateRef const& predicate,
        char const* feature) {
        auto kind_name = [](rulesforge::MirRuntimePredicateKind kind) {
            switch (kind) {
                case rulesforge::MirRuntimePredicateKind::Compare: return "compare";
                case rulesforge::MirRuntimePredicateKind::Temporal: return "temporal";
                case rulesforge::MirRuntimePredicateKind::StringContains: return "string contains";
                case rulesforge::MirRuntimePredicateKind::StringMatches: return "string regex";
                case rulesforge::MirRuntimePredicateKind::StringAffix: return "string affix";
                case rulesforge::MirRuntimePredicateKind::StringLengthIs: return "string lengthIs";
                case rulesforge::MirRuntimePredicateKind::MapContainsKey: return "map containsKey";
                case rulesforge::MirRuntimePredicateKind::CollectionContains: return "collection contains";
                case rulesforge::MirRuntimePredicateKind::NumericLiteral: return "numeric literal";
                case rulesforge::MirRuntimePredicateKind::NumericExpression: return "numeric expression";
                case rulesforge::MirRuntimePredicateKind::ValueList: return "value list";
                case rulesforge::MirRuntimePredicateKind::EvalExpression: return "eval expression";
                case rulesforge::MirRuntimePredicateKind::External: return "external";
            }
            return "unknown";
        };
        if (predicate.backend == rulesforge::RuntimePredicateBackend::MirJit) {
            std::ostringstream message;
            message << feature << " MIR " << kind_name(predicate.kind) << " predicate failed at runtime";
            throw std::runtime_error(message.str());
        }
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

    bool is_numeric_value(ConstraintValue const& value) {
        return std::holds_alternative<int64_t>(value) || std::holds_alternative<double>(value);
    }

    enum class MirExpressionArgsStatus {
        Resolved,
        MissingValue,
        TypeMismatch,
        PredicateMissing,
    };

    struct MirExpressionArgsResult {
        MirExpressionArgsStatus status = MirExpressionArgsStatus::PredicateMissing;
        std::vector<ConstraintValue> values;
    };

    std::string trim_runtime_name(std::string const& value) {
        auto begin = value.begin();
        while (begin != value.end() && std::isspace(static_cast<unsigned char>(*begin)) != 0) {
            ++begin;
        }
        auto end = value.end();
        while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1))) != 0) {
            --end;
        }
        return std::string(begin, end);
    }

    std::optional<ConstraintValue> resolve_mir_expression_variable(
        std::string const& variable,
        Fact const& current_fact,
        Token const* token,
        std::map<std::string, int> const* bindings) {
        std::string const normalized_variable = trim_runtime_name(variable);
        if (normalized_variable.empty() || normalized_variable.front() != '$') {
            return std::nullopt;
        }

        auto dot_pos = normalized_variable.find('.');
        if (dot_pos != std::string::npos) {
            std::string binding_name = trim_runtime_name(normalized_variable.substr(0, dot_pos));
            std::string field_name = trim_runtime_name(normalized_variable.substr(dot_pos + 1));
            if (bindings != nullptr && token != nullptr) {
                auto it = bindings->find(binding_name);
                if (it != bindings->end()) {
                    auto bound_fact = token->get_fact_at_depth(it->second);
                    if (!bound_fact) {
                        if (it->second >= token->get_depth()) {
                            bound_fact = const_cast<Fact*>(&current_fact);
                        } else {
                            return std::nullopt;
                        }
                    }
                    return bound_fact->get_field(field_name);
                }
            }
            return current_fact.get_field(field_name);
        }

        std::string field_name = normalized_variable.substr(1);
        if (bindings != nullptr && token != nullptr) {
            auto it = bindings->find(normalized_variable);
            if (it != bindings->end()) {
                auto bound_fact = token->get_fact_at_depth(it->second);
                if (!bound_fact) {
                    if (it->second >= token->get_depth()) {
                        bound_fact = const_cast<Fact*>(&current_fact);
                    } else {
                        return std::nullopt;
                    }
                }
                return bound_fact->get_field(field_name);
            }
        }
        return current_fact.get_field(field_name);
    }

    MirExpressionArgsResult resolve_mir_expression_arguments(
        KnowledgeBase const& kb,
        std::size_t predicate_id,
        Fact const& current_fact,
        Token const* token,
        std::map<std::string, int> const* bindings) {
        auto const* variables = kb.mir_numeric_expression_predicate_variables(predicate_id);
        if (variables == nullptr) {
            return {MirExpressionArgsStatus::PredicateMissing, {}};
        }

        std::vector<ConstraintValue> args;
        args.reserve(variables->size());
        for (auto const& variable : *variables) {
            auto value = resolve_mir_expression_variable(variable, current_fact, token, bindings);
            if (!value) {
                return {MirExpressionArgsStatus::MissingValue, {}};
            }
            if (!is_numeric_value(*value)) {
                return {MirExpressionArgsStatus::TypeMismatch, {}};
            }
            args.push_back(std::move(*value));
        }
        return {MirExpressionArgsStatus::Resolved, std::move(args)};
    }

    std::optional<std::vector<ConstraintValue>> resolve_mir_value_expression_arguments(
        KnowledgeBase const& kb,
        std::size_t expression_id,
        Fact const& current_fact,
        Token const* token,
        std::map<std::string, int> const* bindings) {
        auto const* variables = kb.mir_numeric_value_expression_variables(expression_id);
        if (variables == nullptr) {
            return std::nullopt;
        }

        std::vector<ConstraintValue> args;
        args.reserve(variables->size());
        for (auto const& variable : *variables) {
            auto value = resolve_mir_expression_variable(variable, current_fact, token, bindings);
            if (!value || !is_numeric_value(*value)) {
                return std::nullopt;
            }
            args.push_back(std::move(*value));
        }
        return args;
    }

    void print_join_constraints_with_mir_ids(
        std::ostream& os,
        std::vector<ParsedConstraint> const& joins,
        std::vector<std::optional<rulesforge::MirRuntimePredicateRef>> const& mir_runtime_predicates) {
        for (std::size_t index = 0; index < joins.size(); ++index) {
            os << "\\n" << constraint_to_string(joins[index]);
            if (index < mir_runtime_predicates.size() && mir_runtime_predicates[index]) {
                auto const& predicate = *mir_runtime_predicates[index];
                os << " [mir#" << static_cast<int>(predicate.kind) << ":" << predicate.predicate_id << "]";
            }
        }
    }

    bool check_all_join_conditions(StatefulSession& session, Token const& token, Fact const& fact,
                                   std::vector<ParsedConstraint> const& joins,
                                   std::map<std::string, int> const& bindings,
                                   std::vector<std::optional<rulesforge::MirRuntimePredicateRef>> const* mir_runtime_predicates = nullptr) {
        g_join_checks.fetch_add(1, std::memory_order_relaxed);
        if (joins.empty()) { return true; }

        for (std::size_t join_index = 0; join_index < joins.size(); ++join_index) {
            auto const& join = joins[join_index];
            auto kb = session.get_knowledge_base();
            auto const* runtime_predicate = (mir_runtime_predicates != nullptr
                && join_index < mir_runtime_predicates->size())
                ? &(*mir_runtime_predicates)[join_index]
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
                if (!kb || !kb->has_mir_execution_plan()) {
                    throw std::runtime_error("MIR execution plan is required for temporal predicate");
                }
                if (runtime_predicate == nullptr || !*runtime_predicate) {
                    throw_lhs_runtime_not_lowered("temporal predicate");
                }
                auto pass = kb->runtime_predicate(*(*runtime_predicate),
                    {ConstraintValue{*lhs_ts}, ConstraintValue{*rhs_ts}});
                if (!pass) {
                    throw_lhs_runtime_predicate_failed(*(*runtime_predicate), "temporal predicate");
                }
                if (!*pass) return false;
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
                if (runtime_predicate != nullptr && *runtime_predicate
                    && (*runtime_predicate)->kind == rulesforge::MirRuntimePredicateKind::NumericExpression
                    && kb && kb->has_mir_execution_plan()) {
                    auto const& predicate = *(*runtime_predicate);
                    auto args = resolve_mir_expression_arguments(
                        *kb,
                        predicate.predicate_id,
                        fact,
                        &token,
                        &bindings);
                    if (args.status == MirExpressionArgsStatus::MissingValue) {
                        return false;
                    }
                    if (args.status != MirExpressionArgsStatus::Resolved) {
                        throw_lhs_runtime_predicate_failed(predicate, "join expression predicate");
                    }
                    std::vector<ConstraintValue> runtime_args;
                    runtime_args.reserve(args.values.size() + 1);
                    runtime_args.push_back(*lhs_val_opt);
                    runtime_args.insert(runtime_args.end(), args.values.begin(), args.values.end());
                    auto mir_result = kb->runtime_predicate(predicate, runtime_args);
                    if (!mir_result) {
                        throw_lhs_runtime_predicate_failed(predicate, "join expression predicate");
                    }
                    if (!*mir_result) { return false; }
                    continue;
                }
                throw_lhs_runtime_not_lowered("join expression predicate");
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
            if (runtime_predicate != nullptr && *runtime_predicate && kb && kb->has_mir_execution_plan()) {
                if (auto mir_result = kb->runtime_predicate(*(*runtime_predicate), {lhs, rhs})) {
                    if (!*mir_result) { return false; }
                    continue;
                }
                throw_lhs_runtime_predicate_failed(*(*runtime_predicate), "join predicate");
            }
            throw_lhs_runtime_not_lowered("join predicate");
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

    bool check_all_mir_constraints(
        StatefulSession& session,
        Token const& token,
        Fact const& fact,
        std::vector<ParsedConstraint> const& constraints,
        std::map<std::string, int> const& bindings,
        std::vector<std::optional<rulesforge::MirRuntimePredicateRef>> const* mir_runtime_predicates) {
        if (constraints.empty()) {
            return true;
        }

        auto kb = session.get_knowledge_base();
        if (!kb || !kb->has_mir_execution_plan()) {
            throw std::runtime_error("MIR execution plan is required for unnest predicate");
        }

        for (std::size_t index = 0; index < constraints.size(); ++index) {
            auto const& constraint = constraints[index];
            if (constraint.op == CompareOp::None) {
                continue;
            }
            auto const* runtime_predicate = (mir_runtime_predicates != nullptr
                && index < mir_runtime_predicates->size())
                ? &(*mir_runtime_predicates)[index]
                : nullptr;
            if (runtime_predicate == nullptr || !*runtime_predicate) {
                throw_lhs_runtime_not_lowered("unnest predicate");
            }

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
                auto result = kb->runtime_predicate(*(*runtime_predicate), {*lhs, *rhs});
                if (!result) {
                    throw_lhs_runtime_predicate_failed(*(*runtime_predicate), "unnest temporal predicate");
                }
                if (!*result) {
                    return false;
                }
                continue;
            }

            auto lhs = resolve_constraint_field(constraint, true, fact, token, bindings);
            if (!lhs) {
                return false;
            }

            switch ((*runtime_predicate)->kind) {
                case rulesforge::MirRuntimePredicateKind::NumericLiteral:
                case rulesforge::MirRuntimePredicateKind::ValueList: {
                    auto result = kb->runtime_predicate(*(*runtime_predicate), {*lhs});
                    if (!result) {
                        throw_lhs_runtime_predicate_failed(*(*runtime_predicate), "unnest unary predicate");
                    }
                    if (!*result) {
                        return false;
                    }
                    continue;
                }
                case rulesforge::MirRuntimePredicateKind::CollectionContains:
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
                case rulesforge::MirRuntimePredicateKind::NumericExpression: {
                    auto args = resolve_mir_expression_arguments(
                        *kb,
                        (*runtime_predicate)->predicate_id,
                        fact,
                        &token,
                        &bindings);
                    if (args.status == MirExpressionArgsStatus::MissingValue) {
                        return false;
                    }
                    if (args.status != MirExpressionArgsStatus::Resolved) {
                        throw_lhs_runtime_predicate_failed(*(*runtime_predicate), "unnest expression predicate");
                    }
                    std::vector<ConstraintValue> runtime_args;
                    runtime_args.reserve(args.values.size() + 1);
                    runtime_args.push_back(*lhs);
                    runtime_args.insert(runtime_args.end(), args.values.begin(), args.values.end());
                    auto result = kb->runtime_predicate(*(*runtime_predicate), runtime_args);
                    if (!result) {
                        throw_lhs_runtime_predicate_failed(*(*runtime_predicate), "unnest expression predicate");
                    }
                    if (!*result) {
                        return false;
                    }
                    continue;
                }
                default:
                    break;
            }

            std::optional<ConstraintValue> rhs;
            if (constraint.right_bound_field) {
                rhs = resolve_constraint_field(constraint, false, fact, token, bindings);
            } else if (constraint.right_literal) {
                rhs = constraint.right_literal;
            } else {
                rhs = ConstraintValue{NilValue{}};
            }
            if (!rhs) {
                return false;
            }

            auto result = kb->runtime_predicate(*(*runtime_predicate), {*lhs, *rhs});
            if (!result) {
                throw_lhs_runtime_predicate_failed(*(*runtime_predicate), "unnest binary predicate");
            }
            if (!*result) {
                return false;
            }
        }

        return true;
    }

    template <typename T>
    void remove_from_vector(std::vector<T>& vec, T const& item) {
        auto it = std::find(vec.begin(), vec.end(), item);
        if (it != vec.end()) {
            *it = std::move(vec.back());  // O(1) swap-and-pop instead of O(n) shift
            vec.pop_back();
        }
    }
}   // namespace

namespace rulesforge::rete_prof {
void reset_stats() {
    g_alpha_checks.store(0, std::memory_order_relaxed);
    g_join_checks.store(0, std::memory_order_relaxed);
    g_compare_calls.store(0, std::memory_order_relaxed);
    g_field_lookups.store(0, std::memory_order_relaxed);
}

Stats get_stats() {
    Stats s;
    s.alpha_checks = g_alpha_checks.load(std::memory_order_relaxed);
    s.join_checks = g_join_checks.load(std::memory_order_relaxed);
    s.compare_calls = g_compare_calls.load(std::memory_order_relaxed);
    s.field_lookups = g_field_lookups.load(std::memory_order_relaxed);
    return s;
}
}  // namespace rulesforge::rete_prof

// --- ReteNode ---
void ReteNode::add_child(std::shared_ptr<ReteNode> const& child) {
    if (child) {
        children.push_back(child);
        children_raw.push_back(child.get());
        child->parents.push_back(shared_from_this());
    }
}

void ReteNode::add_parent(std::shared_ptr<ReteNode> const& parent) {
    if (parent) {
        parents.push_back(parent);
    }
}

// --- BetaConditionNode ---
BetaConditionNode::BetaConditionNode(NodeKind k, std::vector<ParsedConstraint> const& joins,
                                     std::map<std::string, int> const& bindings,
                                     std::vector<std::optional<rulesforge::MirRuntimePredicateRef>> mir_runtime_predicates_) :
    ReteNode(k),
    join_constraints(joins),
    binding_to_token_idx(bindings),
    mir_runtime_predicates(std::move(mir_runtime_predicates_)) {
        // Pre-parse paths for join constraints
        for (auto& join : join_constraints) {
            if (join.left_field.find('.') != std::string::npos || join.left_field.find('[') != std::string::npos) {
                join.cached_left_field_path = parse_field_path(join.left_field);
            }
            if (join.right_bound_field) {
                auto const& r_field = join.right_bound_field->second;
                if (r_field.find('.') != std::string::npos || r_field.find('[') != std::string::npos) {
                    join.cached_right_field_path = parse_field_path(r_field);
                }
            }
        }
    }

void BetaConditionNode::left_activate(StatefulSession& session, Token const& token) {
    auto& mem = session.net_mem().beta_condition[mem_slot];

    if (token.type == PropagationType::RETRACT) {
        if (mem.left.erase(token.wme) > 0) {
            for (auto* child : children_raw) { child->left_activate(session, token); }
        }
        return;
    }

    size_t match_count = 0;
    for (auto const& [fact_id, fact] : mem.right) {
        if (check_all_join_conditions(session, token, *fact, join_constraints, binding_to_token_idx,
                                      &mir_runtime_predicates)) {
            match_count++;
        }
    }

    NetworkMemory::BetaConditionMem::LeftMemoryItem new_item;
    new_item.wme = token.wme;
    new_item.match_count = match_count;
    mem.left[token.wme] = new_item;

    if (condition_passes(match_count)) {
        for (auto* child : children_raw) { child->left_activate(session, token); }
    }
}

void BetaConditionNode::right_activate(StatefulSession& session, Fact* fact, PropagationType p_type) {
    auto& mem = session.net_mem().beta_condition[mem_slot];

    // If the fact is being retracted, we must first check if it was in our memory.
    if (p_type == PropagationType::RETRACT) {
        if (mem.right.erase(fact->id) == 0) {
            return;   // Fact was not in our memory, so no state change is possible.
        }
    } else {   // ASSERT or MODIFY
        mem.right[fact->id] = fact;
    }

    // Iterate over all tokens in the left memory to see which ones are affected by this fact.
    for (auto& [wme_ptr, item] : mem.left) {
        Token token{item.wme, PropagationType::ASSERT};

        // Check if the arriving fact matches the conditions for the current token.
        bool matches = check_all_join_conditions(session, token, *fact, join_constraints, binding_to_token_idx,
                                                 &mir_runtime_predicates);

        // If it doesn't match, this fact doesn't affect this token's match count. Continue.
        if (!matches) { continue; }

        // The fact matches. Now update the count and check for a state change.
        bool was_passing_before = was_passing(item.match_count);

        if (p_type == PropagationType::ASSERT) {
            item.match_count++;
        } else {   // RETRACT
            item.match_count--;
        }

        bool is_passing_now = condition_passes(item.match_count);

        // Propagate only if the state has changed (e.g., from passing to not passing).
        if (was_passing_before && !is_passing_now) {
            Token retract_token{item.wme, PropagationType::RETRACT};
            for (auto* c : children_raw) { c->left_activate(session, retract_token); }
        } else if (!was_passing_before && is_passing_now) {
            Token assert_token{item.wme, PropagationType::ASSERT};
            for (auto* c : children_raw) { c->left_activate(session, assert_token); }
        }
    }
}

void BetaConditionNode::right_activate_batch(StatefulSession& session, std::vector<Fact*>& facts, PropagationType p_type) {
    auto& mem = session.net_mem().beta_condition[mem_slot];

    // Store all facts into right memory first
    for (auto& fact : facts) {
        if (p_type == PropagationType::RETRACT) {
            mem.right.erase(fact->id);
        } else {
            mem.right[fact->id] = fact;
        }
    }

    // Single pass over left memory, checking all new facts per token
    for (auto& [wme_ptr, item] : mem.left) {
        Token token{item.wme, PropagationType::ASSERT};
        bool was_passing_before = was_passing(item.match_count);

        for (auto& fact : facts) {
            bool matches = check_all_join_conditions(session, token, *fact, join_constraints, binding_to_token_idx,
                                                     &mir_runtime_predicates);
            if (!matches) continue;

            if (p_type == PropagationType::ASSERT) {
                item.match_count++;
            } else {
                item.match_count--;
            }
        }

        bool is_passing_now = condition_passes(item.match_count);

        // Propagate only if the state has changed (e.g., from passing to not passing).
        if (was_passing_before && !is_passing_now) {
            Token retract_token{item.wme, PropagationType::RETRACT};
            for (auto* c : children_raw) { c->left_activate(session, retract_token); }
        } else if (!was_passing_before && is_passing_now) {
            Token assert_token{item.wme, PropagationType::ASSERT};
            for (auto* c : children_raw) { c->left_activate(session, assert_token); }
        }
    }
}

void BetaConditionNode::right_activate_deferred(StatefulSession& session, Fact* fact, PropagationType p_type) {
    auto& mem = session.net_mem().beta_condition[mem_slot];
    mem.pending_facts.push_back(fact);
    mem.dirty = true;
}

void BetaConditionNode::right_activate_batch_deferred(StatefulSession& session, std::vector<Fact*>& facts, PropagationType p_type) {
    auto& mem = session.net_mem().beta_condition[mem_slot];
    mem.pending_facts.insert(mem.pending_facts.end(), facts.begin(), facts.end());
    mem.dirty = true;
}

bool BetaConditionNode::flush_pending(StatefulSession& session) {
    auto& mem = session.net_mem().beta_condition[mem_slot];
    if (!mem.dirty) return false;
    mem.dirty = false;
    auto pending = std::move(mem.pending_facts);
    mem.pending_facts.clear();
    right_activate_batch(session, pending, PropagationType::ASSERT);
    return true;
}

// --- AlphaNode ---
AlphaNode::AlphaNode(ParsedConstraint const& c) :
    AlphaNode(c, std::nullopt) {}

AlphaNode::AlphaNode(ParsedConstraint const& c,
                     std::optional<rulesforge::MirRuntimePredicateRef> mir_runtime_predicate) :
    ReteNode(NodeKind::Alpha), constraint(c),
    mir_runtime_predicate_(std::move(mir_runtime_predicate)) {
    // Pre-parse path if complex
    if (constraint.left_field.find('.') != std::string::npos || constraint.left_field.find('[') != std::string::npos) {
        constraint.cached_left_field_path = parse_field_path(constraint.left_field);
    }
}

void AlphaNode::left_activate(StatefulSession&, Token const&) {}

void AlphaNode::right_activate(StatefulSession& session, Fact* fact, PropagationType p_type) {
    if (!fact) return;

    auto& mem = session.net_mem().alpha[mem_slot];
    bool const was_passing = mem.passing_facts.count(fact->id) > 0;

    if (p_type == PropagationType::RETRACT) {
        if (!was_passing) return;
        mem.passing_facts.erase(fact->id);
        for (auto* child : children_raw) { child->right_activate(session, fact, PropagationType::RETRACT); }
        return;
    }

    if (p_type == PropagationType::MODIFY) {
        auto const* changed_fields = session.current_modified_fields();
        if (changed_fields && !alpha_constraint_affected(constraint, changed_fields)) {
            if (!was_passing) return;
            for (auto* child : children_raw) { child->right_activate(session, fact, PropagationType::MODIFY); }
            return;
        }
    }

    bool const passes = check_constraint(session, *fact);
    if (!passes) {
        if (!was_passing) return;
        mem.passing_facts.erase(fact->id);
        for (auto* child : children_raw) { child->right_activate(session, fact, PropagationType::RETRACT); }
        return;
    }

    mem.passing_facts.insert(fact->id);
    PropagationType child_type = p_type;
    if (p_type == PropagationType::MODIFY && !was_passing) {
        child_type = PropagationType::ASSERT;
    }
    for (auto* child : children_raw) { child->right_activate(session, fact, child_type); }
}

void AlphaNode::right_activate_batch(StatefulSession& session, std::vector<Fact*>& facts, PropagationType p_type) {
    if (p_type != PropagationType::ASSERT) {
        for (auto* fact : facts) {
            right_activate(session, fact, p_type);
        }
        return;
    }
    if (children_raw.size() == 1) {
        // Fast path: single child — filter in-place, zero allocation
        auto& mem = session.net_mem().alpha[mem_slot];
        size_t write = 0;
        for (size_t read = 0; read < facts.size(); ++read) {
            if (check_constraint(session, *facts[read])) {
                mem.passing_facts.insert(facts[read]->id);
                facts[write++] = facts[read];
            }
        }
        if (write == 0) return;
        facts.resize(write);
        children_raw[0]->right_activate_batch(session, facts, p_type);
    } else {
        // Multiple children: need a copy
        auto& mem = session.net_mem().alpha[mem_slot];
        std::vector<Fact*> survivors;
        survivors.reserve(facts.size());
        for (auto* fact : facts) {
            if (check_constraint(session, *fact)) {
                mem.passing_facts.insert(fact->id);
                survivors.push_back(fact);
            }
        }
        if (survivors.empty()) return;
        for (auto* child : children_raw) { child->right_activate_batch(session, survivors, p_type); }
    }
}

void AlphaNode::right_activate_deferred(StatefulSession& session, Fact* fact, PropagationType p_type) {
    if (p_type != PropagationType::ASSERT) {
        right_activate(session, fact, p_type);
        return;
    }
    if (check_constraint(session, *fact)) {
        session.net_mem().alpha[mem_slot].passing_facts.insert(fact->id);
        for (auto* child : children_raw) { child->right_activate_deferred(session, fact, p_type); }
    }
}

void AlphaNode::right_activate_batch_deferred(StatefulSession& session, std::vector<Fact*>& facts, PropagationType p_type) {
    if (p_type != PropagationType::ASSERT) {
        for (auto* fact : facts) {
            right_activate(session, fact, p_type);
        }
        return;
    }

    std::vector<Fact*> survivors;
    survivors.reserve(facts.size());
    auto& mem = session.net_mem().alpha[mem_slot];
    for (auto* fact : facts) {
        if (check_constraint(session, *fact)) {
            mem.passing_facts.insert(fact->id);
            survivors.push_back(fact);
        }
    }
    if (survivors.empty()) return;
    for (auto* child : children_raw) { child->right_activate_batch_deferred(session, survivors, p_type); }
}

bool AlphaNode::check_constraint(StatefulSession const& session, Fact const& fact) const {
    g_alpha_checks.fetch_add(1, std::memory_order_relaxed);
    // A constraint with an empty operator is a pure binding (like `$id: id`)
    // or an existence check (`name`).
    if (constraint.op == CompareOp::None) {
        // If there's no right literal, it's a pure binding. It should always pass
        // the alpha check, as the binding itself is handled elsewhere.
        if (!constraint.right_literal.has_value()) {
            return true; // Fast return (removed logd for perf)
        }
        // Otherwise, it's an existence check that was transformed to `field == 1`.
        // This will be handled by the main comparison logic below.
    }

    ConstraintValue const* lhs_ptr = nullptr;
    std::optional<ConstraintValue> temp_lhs; // Keep alive if returned by value from complex get_field

    if (constraint.cached_left_field_path.empty()) {
        // Fast path for simple fields
        if (constraint.left_field == "this") {
            temp_lhs = static_cast<int64_t>(fact.id);
            lhs_ptr = &*temp_lhs;
        } else {
            // Direct map lookup - zero copy, no string scanning
            auto it = fact.fields.find(constraint.left_field);
            g_field_lookups.fetch_add(1, std::memory_order_relaxed);
            if (it != fact.fields.end()) {
                lhs_ptr = &it->second;
            }
        }
    } else {
        // Complex path
        temp_lhs = fact.get_field(constraint.cached_left_field_path);
        g_field_lookups.fetch_add(1, std::memory_order_relaxed);
        if (temp_lhs) lhs_ptr = &*temp_lhs;
    }

    if (!lhs_ptr) {
        // Field not found - fail
        return false;
    }

    ConstraintValue const& lhs = *lhs_ptr;
    auto kb = session.get_knowledge_base();

    if (!kb || !kb->has_mir_execution_plan()) {
        throw std::runtime_error("MIR execution plan is required for alpha predicate");
    }
    if (!mir_runtime_predicate_) {
        throw_lhs_runtime_not_lowered("alpha predicate");
    }

    switch (mir_runtime_predicate_->kind) {
        case rulesforge::MirRuntimePredicateKind::NumericLiteral:
        case rulesforge::MirRuntimePredicateKind::ValueList: {
            auto result = kb->runtime_predicate(*mir_runtime_predicate_, {lhs});
            if (!result) {
                throw_lhs_runtime_predicate_failed(*mir_runtime_predicate_, "alpha unary predicate");
            }
            return *result;
        }
        case rulesforge::MirRuntimePredicateKind::CollectionContains:
            if (constraint.right_value_list) {
                auto rhs_list = make_typed_list(*constraint.right_value_list);
                auto result = kb->runtime_predicate(*mir_runtime_predicate_, {lhs, rhs_list});
                if (!result) {
                    throw_lhs_runtime_predicate_failed(
                        *mir_runtime_predicate_,
                        "alpha collection value-list predicate");
                }
                return *result;
            }
            {
                static const ConstraintValue nil_value{NilValue{}};
                ConstraintValue const& rhs = constraint.right_literal.has_value() ? *constraint.right_literal : nil_value;
                auto result = kb->runtime_predicate(*mir_runtime_predicate_, {lhs, rhs});
                if (!result) {
                    throw_lhs_runtime_predicate_failed(*mir_runtime_predicate_, "alpha binary predicate");
                }
                return *result;
            }
        case rulesforge::MirRuntimePredicateKind::NumericExpression: {
            auto args = resolve_mir_expression_arguments(
                *kb,
                mir_runtime_predicate_->predicate_id,
                fact,
                nullptr,
                nullptr);
            if (args.status == MirExpressionArgsStatus::MissingValue) {
                return false;
            }
            if (args.status != MirExpressionArgsStatus::Resolved) {
                throw_lhs_runtime_predicate_failed(*mir_runtime_predicate_, "alpha expression predicate");
            }
            std::vector<ConstraintValue> runtime_args;
            runtime_args.reserve(args.values.size() + 1);
            runtime_args.push_back(lhs);
            runtime_args.insert(runtime_args.end(), args.values.begin(), args.values.end());
            auto result = kb->runtime_predicate(*mir_runtime_predicate_, runtime_args);
            if (!result) {
                throw_lhs_runtime_predicate_failed(*mir_runtime_predicate_, "alpha expression predicate");
            }
            return *result;
        }
        default: {
            static const ConstraintValue nil_value{NilValue{}};
            ConstraintValue const& rhs = constraint.right_literal.has_value() ? *constraint.right_literal : nil_value;
            auto result = kb->runtime_predicate(*mir_runtime_predicate_, {lhs, rhs});
            if (!result) {
                throw_lhs_runtime_predicate_failed(*mir_runtime_predicate_, "alpha binary predicate");
            }
            return *result;
        }
    }
}

void AlphaNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"AlphaNode (" << id << ")\\n";
    if (!constraint.left_field.empty()) {
        os << constraint.left_field << " " << compare_op_str(constraint.op) << " "
           << ::to_string(constraint.right_literal.value_or(NilValue{}));
        if (mir_runtime_predicate_) {
            os << "\\nMIR predicate: " << static_cast<int>(mir_runtime_predicate_->kind)
               << "#" << mir_runtime_predicate_->predicate_id;
        }
    } else {
        os << "(No Constraint)";
    }
    os << "\", shape=ellipse, style=filled, fillcolor=orange];";
}

// --- EntryPointNode ---
void EntryPointNode::left_activate(StatefulSession&, Token const&) {
    // An EntryPointNode is the start of an alpha chain. It does not receive left activations.
}

void EntryPointNode::right_activate(StatefulSession& session, Fact* fact, PropagationType p_type) {
    for (auto* child : children_raw) { child->right_activate(session, fact, p_type); }
}

void EntryPointNode::right_activate_batch(StatefulSession& session, std::vector<Fact*>& facts, PropagationType p_type) {
    for (auto* child : children_raw) { child->right_activate_batch(session, facts, p_type); }
}

void EntryPointNode::right_activate_deferred(StatefulSession& session, Fact* fact, PropagationType p_type) {
    for (auto* child : children_raw) { child->right_activate_deferred(session, fact, p_type); }
}

void EntryPointNode::right_activate_batch_deferred(StatefulSession& session, std::vector<Fact*>& facts, PropagationType p_type) {
    for (auto* child : children_raw) { child->right_activate_batch_deferred(session, facts, p_type); }
}

void EntryPointNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"Entry Point (" << id << ")\", shape=house, style=filled, fillcolor=yellow];";
}

// --- BaseJoinNode ---
BaseJoinNode::BaseJoinNode(NodeKind k,
                           std::vector<ParsedConstraint> joins,
                           std::map<std::string, int> bindings,
                           std::vector<std::optional<rulesforge::MirRuntimePredicateRef>> mir_runtime_predicates) :
    ReteNode(k),
    join_constraints_(std::move(joins)),
    binding_to_token_idx_(std::move(bindings)),
    mir_runtime_predicates_(std::move(mir_runtime_predicates)) {}

void BaseJoinNode::propagate_assert(StatefulSession& session, Token const& token, Fact* fact,
                                    ChildMap& left_to_children,
                                    RightChildMap& /*right_to_children*/) {
    auto new_wme = session.get_or_create_wme(token.wme, fact);
    left_to_children[token.wme].push_back(new_wme);
    // PHREAK: skip right_to_children — retract finds WMEs via left_to_children scan
    Token new_token{new_wme, PropagationType::ASSERT};
    for (auto* c : children_raw) { c->left_activate(session, new_token); }
}

void BaseJoinNode::propagate_retract(StatefulSession& session, TokenWME const* wme, Fact* fact,
                                     ChildMap& left_to_children,
                                     RightChildMap& /*right_to_children*/) {
    auto it_left = left_to_children.find(wme);
    if (it_left == left_to_children.end()) return;

    TokenWME const* child_to_retract = nullptr;
    for (auto const& child_wme : it_left->second) {
        if (child_wme->fact->id == fact->id) {
            child_to_retract = child_wme;
            break;
        }
    }

    if (child_to_retract) {
        Token retract_token{child_to_retract, PropagationType::RETRACT};
        for (auto* c : children_raw) { c->left_activate(session, retract_token); }
        remove_from_vector(it_left->second, child_to_retract);
        if (it_left->second.empty()) left_to_children.erase(it_left);

        // Invalidate WME cache so that subsequent ASSERT can create a fresh WME
        session.invalidate_wme_cache(child_to_retract->hash);
    }
}

// --- HashedJoinNode ---
HashedJoinNode::HashedJoinNode(std::vector<ParsedConstraint> joins,
                               std::map<std::string, int> bindings,
                               std::vector<std::optional<rulesforge::MirRuntimePredicateRef>> mir_runtime_predicates,
                               std::pair<std::string, int> left_hash_key, std::string right_hash_key) :
    BaseJoinNode(NodeKind::HashedJoin,
                 std::move(joins),
                 std::move(bindings),
                 std::move(mir_runtime_predicates)),
    left_hash_key_(std::move(left_hash_key)),
    right_hash_key_(std::move(right_hash_key)) {}

std::optional<ConstraintValue> HashedJoinNode::get_key(Token const& token) const {
    auto fact_at_depth = token.get_fact_at_depth(left_hash_key_.second);
    if (!fact_at_depth) return std::nullopt;
    return fact_at_depth->get_field(left_hash_key_.first);
}

std::optional<ConstraintValue> HashedJoinNode::get_key(Fact const* fact) const {
    return fact->get_field(right_hash_key_);
}

void HashedJoinNode::left_activate(StatefulSession& session, Token const& token) {
    auto& mem = session.net_mem().hashed_join[mem_slot];
    auto key_opt = get_key(token);
    if (!key_opt) return;
    auto const& key = *key_opt;

    if (token.type == PropagationType::RETRACT) {
        auto mem_it = mem.left.find(key);
        if (mem_it != mem.left.end()) {
            remove_from_vector(mem_it->second, token.wme);
            if (mem_it->second.empty()) mem.left.erase(mem_it);
        }
        auto fact_it = mem.right.find(key);
        if (fact_it != mem.right.end()) {
            for (auto const& fact : fact_it->second) { propagate_retract(session, token.wme, fact, mem.left_to_children, mem.right_to_children); }
        }
        return;
    }

    mem.left[key].push_back(token.wme);
    auto it_right = mem.right.find(key);
    if (it_right != mem.right.end()) {
        for (auto const& fact : it_right->second) {
            if (check_all_join_conditions(session, token, *fact, join_constraints_, binding_to_token_idx_,
                                          &mir_runtime_predicates_)) {
                propagate_assert(session, token, fact, mem.left_to_children, mem.right_to_children);
            }
        }
    }
}

void HashedJoinNode::right_activate(StatefulSession& session, Fact* fact, PropagationType p_type) {
    auto& mem = session.net_mem().hashed_join[mem_slot];
    auto key_opt = get_key(fact);
    if (!key_opt) return;
    auto const& key = *key_opt;

    if (p_type == PropagationType::RETRACT) {
        auto mem_it = mem.right.find(key);
        if (mem_it != mem.right.end()) {
            remove_from_vector(mem_it->second, fact);
            if (mem_it->second.empty()) mem.right.erase(mem_it);
        }
        auto token_it = mem.left.find(key);
        if (token_it != mem.left.end()) {
            for (auto const& wme : token_it->second) { propagate_retract(session, wme, fact, mem.left_to_children, mem.right_to_children); }
        }
        return;
    }

    // For MODIFY: first retract old matches, then assert new ones
    if (p_type == PropagationType::MODIFY) {
        auto mem_it = mem.right.find(key);
        if (mem_it != mem.right.end()) {
            // Check if fact is in the memory for this key
            auto fact_it = std::find(mem_it->second.begin(), mem_it->second.end(), fact);
            if (fact_it != mem_it->second.end()) {
                auto token_it = mem.left.find(key);
                if (token_it != mem.left.end()) {
                    for (auto const& wme : token_it->second) { propagate_retract(session, wme, fact, mem.left_to_children, mem.right_to_children); }
                }
                // Remove from memory - will be re-added below
                remove_from_vector(mem_it->second, fact);
                if (mem_it->second.empty()) mem.right.erase(mem_it);
            }
        }
    }

    mem.right[key].push_back(fact);
    auto it_left = mem.left.find(key);
    if (it_left != mem.left.end()) {
        for (auto const& wme : it_left->second) {
            Token token{wme, PropagationType::ASSERT};
            if (check_all_join_conditions(session, token, *fact, join_constraints_, binding_to_token_idx_,
                                          &mir_runtime_predicates_)) {
                propagate_assert(session, token, fact, mem.left_to_children, mem.right_to_children);
            }
        }
    }
}

void HashedJoinNode::right_activate_batch(StatefulSession& session, std::vector<Fact*>& facts, PropagationType p_type) {
    auto& mem = session.net_mem().hashed_join[mem_slot];

    // Index all incoming facts by hash key, store into right memory
    std::unordered_map<ConstraintValue, std::vector<Fact*>, ConstraintValueHasher, ConstraintValueEquals> facts_by_key;
    for (auto& fact : facts) {
        auto key_opt = get_key(fact);
        if (!key_opt) continue;
        mem.right[*key_opt].push_back(fact);
        facts_by_key[*key_opt].push_back(fact);
    }

    // Single pass over left memory per key bucket
    for (auto& [key, keyed_facts] : facts_by_key) {
        auto it_left = mem.left.find(key);
        if (it_left == mem.left.end()) continue;
        for (auto const& wme : it_left->second) {
            Token token{wme, PropagationType::ASSERT};
            for (auto& fact : keyed_facts) {
                if (check_all_join_conditions(session, token, *fact, join_constraints_, binding_to_token_idx_,
                                              &mir_runtime_predicates_)) {
                    // Inline: uncached WME + skip right_to_children
                    auto new_wme = session.create_wme_uncached(token.wme, fact);
                    mem.left_to_children[token.wme].push_back(new_wme);
                    Token new_token{new_wme, PropagationType::ASSERT};
                    for (auto* c : children_raw) { c->left_activate(session, new_token); }
                }
            }
        }
    }
}

void HashedJoinNode::right_activate_deferred(StatefulSession& session, Fact* fact, PropagationType p_type) {
    auto& mem = session.net_mem().hashed_join[mem_slot];
    mem.pending_facts.push_back(fact);
    mem.dirty = true;
}

void HashedJoinNode::right_activate_batch_deferred(StatefulSession& session, std::vector<Fact*>& facts, PropagationType p_type) {
    auto& mem = session.net_mem().hashed_join[mem_slot];
    mem.pending_facts.insert(mem.pending_facts.end(), facts.begin(), facts.end());
    mem.dirty = true;
}

bool HashedJoinNode::flush_pending(StatefulSession& session) {
    auto& mem = session.net_mem().hashed_join[mem_slot];
    if (!mem.dirty) return false;
    mem.dirty = false;
    auto pending = std::move(mem.pending_facts);
    mem.pending_facts.clear();
    right_activate_batch(session, pending, PropagationType::ASSERT);
    return true;
}

void HashedJoinNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"HashedJoinNode (" << id << ")\\nIndex: " << right_hash_key_ << " == $"
       << left_hash_key_.second << "." << left_hash_key_.first;
    if (!join_constraints_.empty()) {
        os << "\\nJoins:";
        print_join_constraints_with_mir_ids(os,
                                            join_constraints_,
                                            mir_runtime_predicates_);
    }
    os << "\", shape=box, style=filled, fillcolor=lightblue];";
}

// --- CrossProductJoinNode ---
CrossProductJoinNode::CrossProductJoinNode(std::vector<ParsedConstraint> joins,
                                           std::map<std::string, int> bindings,
                                           std::vector<std::optional<rulesforge::MirRuntimePredicateRef>> mir_runtime_predicates) :
    BaseJoinNode(NodeKind::CrossProductJoin, std::move(joins), std::move(bindings),
                 std::move(mir_runtime_predicates)) {}

void CrossProductJoinNode::left_activate(StatefulSession& session, Token const& token) {
    auto& mem = session.net_mem().cross_product_join[mem_slot];
    if (token.type == PropagationType::RETRACT) {
        if (mem.left.erase(token.wme) > 0) {
            for (auto const& [fact_id, fact] : mem.right) { propagate_retract(session, token.wme, fact, mem.left_to_children, mem.right_to_children); }
        }
        return;
    }

    mem.left[token.wme] = token.wme;
    for (auto const& [fact_id, fact] : mem.right) {
        if (check_all_join_conditions(session, token, *fact, join_constraints_, binding_to_token_idx_,
                                      &mir_runtime_predicates_)) {
            propagate_assert(session, token, fact, mem.left_to_children, mem.right_to_children);
        }
    }
}

void CrossProductJoinNode::right_activate(StatefulSession& session, Fact* fact,
                                          PropagationType p_type) {
    auto& mem = session.net_mem().cross_product_join[mem_slot];

    if (p_type == PropagationType::RETRACT) {
        if (mem.right.erase(fact->id) > 0) {
            for (auto const& [ptr, wme] : mem.left) { propagate_retract(session, wme, fact, mem.left_to_children, mem.right_to_children); }
        }
        return;
    }

    // For MODIFY: first retract old matches, then assert new ones
    if (p_type == PropagationType::MODIFY && mem.right.count(fact->id) > 0) {
        for (auto const& [ptr, wme] : mem.left) { propagate_retract(session, wme, fact, mem.left_to_children, mem.right_to_children); }
    }

    mem.right[fact->id] = fact;
    for (auto const& [ptr, wme] : mem.left) {
        Token token{wme, PropagationType::ASSERT};
        if (check_all_join_conditions(session, token, *fact, join_constraints_, binding_to_token_idx_,
                                      &mir_runtime_predicates_)) {
            propagate_assert(session, token, fact, mem.left_to_children, mem.right_to_children);
        }
    }
}

void CrossProductJoinNode::right_activate_batch(StatefulSession& session, std::vector<Fact*>& facts, PropagationType p_type) {
    auto& mem = session.net_mem().cross_product_join[mem_slot];

    // Store all facts into right memory first
    mem.right.reserve(mem.right.size() + facts.size());
    for (auto& fact : facts) {
        mem.right[fact->id] = fact;
    }

    // PHREAK: set-oriented propagation — batch WME creation + batch left_activate
    for (auto const& [ptr, wme] : mem.left) {
        Token token{wme, PropagationType::ASSERT};

        // Pre-reserve left_to_children vector for this token
        auto& child_vec = mem.left_to_children[token.wme];
        child_vec.reserve(child_vec.size() + facts.size());

        // Collect all matching tokens for batch propagation
        std::vector<Token> batch_tokens;
        batch_tokens.reserve(facts.size());

        for (auto& fact : facts) {
            if (check_all_join_conditions(session, token, *fact, join_constraints_, binding_to_token_idx_,
                                          &mir_runtime_predicates_)) {
                auto new_wme = session.create_wme_uncached(token.wme, fact);
                child_vec.push_back(new_wme);
                batch_tokens.push_back(Token{new_wme, PropagationType::ASSERT});
            }
        }

        // Batch propagate to children
        if (!batch_tokens.empty()) {
            for (auto* c : children_raw) { c->left_activate_batch(session, batch_tokens); }
        }
    }
}

void CrossProductJoinNode::right_activate_deferred(StatefulSession& session, Fact* fact, PropagationType p_type) {
    auto& mem = session.net_mem().cross_product_join[mem_slot];
    mem.pending_facts.push_back(fact);
    mem.dirty = true;
}

void CrossProductJoinNode::right_activate_batch_deferred(StatefulSession& session, std::vector<Fact*>& facts, PropagationType p_type) {
    auto& mem = session.net_mem().cross_product_join[mem_slot];
    mem.pending_facts.insert(mem.pending_facts.end(), facts.begin(), facts.end());
    mem.dirty = true;
}

bool CrossProductJoinNode::flush_pending(StatefulSession& session) {
    auto& mem = session.net_mem().cross_product_join[mem_slot];
    if (!mem.dirty) return false;
    mem.dirty = false;
    auto pending = std::move(mem.pending_facts);
    mem.pending_facts.clear();
    right_activate_batch(session, pending, PropagationType::ASSERT);
    return true;
}

void CrossProductJoinNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"CrossProductJoinNode (" << id << ")";
    if (!join_constraints_.empty()) {
        os << "\\nJoins:";
        print_join_constraints_with_mir_ids(os,
                                            join_constraints_,
                                            mir_runtime_predicates_);
    }
    os << "\", shape=box, style=filled, fillcolor=lightgrey];";
}

// --- NotNode ---
NotNode::NotNode(std::vector<ParsedConstraint> const& joins,
                 std::map<std::string, int> const& bindings,
                 std::vector<std::optional<rulesforge::MirRuntimePredicateRef>> mir_runtime_predicates_) :
    BetaConditionNode(NodeKind::Not,
                      joins,
                      bindings,
                      std::move(mir_runtime_predicates_)) {}

void NotNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"NotNode (" << id << ")";
    if (!join_constraints.empty()) {
        os << "\\nJoins:";
        print_join_constraints_with_mir_ids(os,
                                            join_constraints,
                                            mir_runtime_predicates);
    }
    os << "\", shape=octagon, style=filled, fillcolor=salmon];";
}

// --- ExistsNode ---
ExistsNode::ExistsNode(std::vector<ParsedConstraint> const& joins,
                       std::map<std::string, int> const& bindings,
                       std::vector<std::optional<rulesforge::MirRuntimePredicateRef>> mir_runtime_predicates_) :
    BetaConditionNode(NodeKind::Exists,
                      joins,
                      bindings,
                      std::move(mir_runtime_predicates_)) {}

void ExistsNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"ExistsNode (" << id << ")";
    if (!join_constraints.empty()) {
        os << "\\nJoins:";
        print_join_constraints_with_mir_ids(os,
                                            join_constraints,
                                            mir_runtime_predicates);
    }
    os << "\", shape=octagon, style=filled, fillcolor=khaki];";
}

// --- AccumulateNode ---
namespace {
    std::optional<ConstraintValue> get_accumulate_value(
        StatefulSession& session,
        ParsedAccumulate const& info,
        Fact const& fact,
        Token const* token,
        std::map<std::string, int> const* bindings)
    {
        if (info.function == "count" && !info.uses_mir_value_expression && info.accumulate_field_name.empty()) {
            return ConstraintValue{int64_t{1}};
        }
        if (info.uses_mir_value_expression) {
            auto kb = session.get_knowledge_base();
            if (!kb || !kb->has_mir_execution_plan()) {
                throw std::runtime_error("MIR execution plan is required for accumulate expression kernel");
            }
            auto expression_id = kb->mir_numeric_value_expression_id(info.field);
            if (!expression_id) {
                throw_lhs_runtime_not_lowered("accumulate expression kernel");
            }
            auto const* variables = kb->mir_numeric_value_expression_variables(*expression_id);
            if (variables == nullptr) {
                throw_lhs_runtime_not_lowered("accumulate expression kernel");
            }
            std::vector<ConstraintValue> args;
            args.reserve(variables->size());
            for (auto const& variable : *variables) {
                auto value = resolve_mir_expression_variable(variable, fact, token, bindings);
                if (!value || !is_numeric_value(*value)) {
                    return std::nullopt;
                }
                args.push_back(std::move(*value));
            }
            auto result = kb->mir_runtime_numeric_value_expression(*expression_id, args);
            if (!result) {
                throw_lhs_runtime_not_lowered("accumulate expression kernel");
            }
            return ConstraintValue{*result};
        }
        // Otherwise use simple field lookup
        if (!info.accumulate_field_name.empty()) {
            return fact.get_field(info.accumulate_field_name);
        }
        return std::nullopt;
    }

    bool uses_mir_numeric_accumulate(ParsedAccumulate const& info) {
        if (info.function == "count" && !info.uses_mir_value_expression) {
            return true;
        }
        if (info.function != "sum"
            && info.function != "average"
            && info.function != "min"
            && info.function != "max") {
            return false;
        }
        return info.uses_mir_value_expression || !info.accumulate_field_name.empty();
    }

    bool uses_mir_collect_accumulate(ParsedAccumulate const& info) {
        return !info.uses_mir_value_expression
            && (info.function == "collect" || info.function == "collectList" || info.function == "collectSet");
    }

    void reset_mir_collect_accumulate(NetworkMemory::AccumulateMem::LeftMemoryItem& item) {
        item.contributing_facts_list.clear();
        item.contributing_facts_set.clear();
    }

    bool apply_mir_collect_accumulate(
        StatefulSession& session,
        ParsedAccumulate const& info,
        NetworkMemory::AccumulateMem::LeftMemoryItem& item,
        Fact* fact,
        std::int64_t direction) {
        auto kb = session.get_knowledge_base();
        if (!kb || !kb->has_mir_execution_plan()) {
            throw std::runtime_error("MIR execution plan is required for collect accumulate kernel");
        }
        std::optional<bool> changed;
        if (info.function == "collectSet") {
            changed = kb->mir_runtime_collect_set_update(item.contributing_facts_set, fact, direction);
        } else if (info.function == "collect" || info.function == "collectList") {
            changed = kb->mir_runtime_collect_list_update(item.contributing_facts_list, fact, direction);
        } else {
            throw_lhs_runtime_not_lowered("collect accumulate kernel");
        }
        if (!changed) {
            throw_lhs_runtime_not_lowered("collect accumulate kernel");
        }
        return *changed;
    }

    FactList mir_collect_accumulate_result(
        StatefulSession& session,
        ParsedAccumulate const& info,
        NetworkMemory::AccumulateMem::LeftMemoryItem const& item) {
        auto kb = session.get_knowledge_base();
        if (!kb || !kb->has_mir_execution_plan()) {
            throw std::runtime_error("MIR execution plan is required for collect accumulate result");
        }
        std::optional<FactList> result;
        if (info.function == "collectSet") {
            result = kb->mir_runtime_collect_set_result(item.contributing_facts_set);
        } else if (info.function == "collect" || info.function == "collectList") {
            result = kb->mir_runtime_collect_list_result(item.contributing_facts_list);
        } else {
            throw_lhs_runtime_not_lowered("collect accumulate result");
        }
        if (!result) {
            throw_lhs_runtime_not_lowered("collect accumulate result");
        }
        return *result;
    }

    void reset_mir_numeric_accumulate(NetworkMemory::AccumulateMem::LeftMemoryItem& item) {
        item.mir_numeric_sum = 0.0;
        item.mir_numeric_extreme = 0.0;
        item.mir_count = 0;
        item.mir_sum_is_double = false;
        item.mir_numeric_values.clear();
    }

    bool apply_mir_numeric_accumulate(
        StatefulSession& session,
        ParsedAccumulate const& info,
        NetworkMemory::AccumulateMem::LeftMemoryItem& item,
        Fact const& fact,
        Token const* token,
        std::map<std::string, int> const* bindings,
        std::int64_t direction) {
        auto kb = session.get_knowledge_base();
        if (!kb || !kb->has_mir_execution_plan()) {
            throw std::runtime_error("MIR execution plan is required for accumulate kernel");
        }

        if (info.function == "count") {
            auto next_count = kb->mir_runtime_count_aggregate(item.mir_count, direction);
            if (!next_count) {
                throw_lhs_runtime_not_lowered("count accumulate kernel");
            }
            item.mir_count = *next_count;
            return true;
        }

        if (info.function == "sum" || info.function == "average") {
            auto value_opt = get_accumulate_value(session, info, fact, token, bindings);
            if (!value_opt) {
                return false;
            }
            if (!std::holds_alternative<int64_t>(*value_opt) && !std::holds_alternative<double>(*value_opt)) {
                return false;
            }
            auto next_sum = kb->mir_runtime_sum_aggregate(item.mir_numeric_sum, *value_opt, direction);
            if (!next_sum) {
                throw_lhs_runtime_not_lowered("sum accumulate kernel");
            }
            item.mir_numeric_sum = *next_sum;
            if (info.function == "average") {
                auto next_count = kb->mir_runtime_count_aggregate(item.mir_count, direction);
                if (!next_count) {
                    throw_lhs_runtime_not_lowered("average accumulate count kernel");
                }
                item.mir_count = *next_count;
            }
            if (std::holds_alternative<double>(*value_opt)) {
                item.mir_sum_is_double = true;
            }
            return true;
        }

        if (info.function == "min" || info.function == "max") {
            auto value_opt = get_accumulate_value(session, info, fact, token, bindings);
            if (!value_opt) {
                return false;
            }
            double numeric_value = 0.0;
            if (std::holds_alternative<int64_t>(*value_opt)) {
                numeric_value = static_cast<double>(std::get<int64_t>(*value_opt));
            } else if (std::holds_alternative<double>(*value_opt)) {
                numeric_value = std::get<double>(*value_opt);
                item.mir_sum_is_double = true;
            } else {
                return false;
            }

            if (direction >= 0) {
                item.mir_numeric_values.insert(numeric_value);
            } else {
                auto it = item.mir_numeric_values.find(numeric_value);
                if (it != item.mir_numeric_values.end()) {
                    item.mir_numeric_values.erase(it);
                }
            }

            if (item.mir_numeric_values.empty()) {
                item.mir_numeric_extreme = 0.0;
                return true;
            }

            auto seed = *item.mir_numeric_values.begin();
            auto result = (info.function == "min")
                ? kb->mir_runtime_min_aggregate(seed, ConstraintValue{seed})
                : kb->mir_runtime_max_aggregate(seed, ConstraintValue{seed});
            if (!result) {
                throw_lhs_runtime_not_lowered("extreme accumulate kernel");
            }
            for (auto it = std::next(item.mir_numeric_values.begin());
                 it != item.mir_numeric_values.end();
                 ++it) {
                result = (info.function == "min")
                    ? kb->mir_runtime_min_aggregate(*result, ConstraintValue{*it})
                    : kb->mir_runtime_max_aggregate(*result, ConstraintValue{*it});
                if (!result) {
                    throw_lhs_runtime_not_lowered("extreme accumulate kernel");
                }
            }
            item.mir_numeric_extreme = *result;
            return true;
        }

        throw_lhs_runtime_not_lowered("accumulate kernel");
    }

    ConstraintValue mir_numeric_accumulate_result(
        ParsedAccumulate const& info,
        NetworkMemory::AccumulateMem::LeftMemoryItem const& item) {
        if (info.function == "count") {
            return item.mir_count;
        }
        if (info.function == "average") {
            if (item.mir_count == 0) {
                return 0.0;
            }
            return item.mir_numeric_sum / static_cast<double>(item.mir_count);
        }
        if (info.function == "min" || info.function == "max") {
            if (item.mir_sum_is_double) {
                return item.mir_numeric_extreme;
            }
            return static_cast<int64_t>(item.mir_numeric_extreme);
        }
        if (item.mir_sum_is_double) {
            return item.mir_numeric_sum;
        }
        return static_cast<int64_t>(item.mir_numeric_sum);
    }
}

#include "rete_node_accumulate.inc"

// =========================================================================
// === WINDOW NODE =========================================================
// =========================================================================

WindowNode::WindowNode(ParsedWindow const& window_info)
    : ReteNode(NodeKind::Window), info(window_info) {}

void WindowNode::right_activate(StatefulSession& session, Fact* fact, PropagationType p_type) {
    if (mem_slot < 0 || static_cast<size_t>(mem_slot) >= session.net_mem().window.size()) {
        throw std::runtime_error("WindowNode memory slot is not initialized");
    }
    auto& mem = session.net_mem().window[mem_slot];

    if (p_type == PropagationType::RETRACT) {
        remove_from_vector(mem.facts, fact);
        for (auto* child : children_raw) {
            child->right_activate(session, fact, p_type);
        }
        return;
    }

    if (p_type == PropagationType::ASSERT) {
        mem.facts.push_back(fact);
        evaluate_expiration(session);
        for (auto* child : children_raw) {
            child->right_activate(session, fact, p_type);
        }
    } else if (p_type == PropagationType::MODIFY) {
        evaluate_expiration(session);
        for (auto* child : children_raw) {
            child->right_activate(session, fact, p_type);
        }
    }
}

void WindowNode::evaluate_expiration(StatefulSession& session) {
    if (mem_slot < 0 || static_cast<size_t>(mem_slot) >= session.net_mem().window.size()) {
        throw std::runtime_error("WindowNode memory slot is not initialized");
    }
    auto& mem = session.net_mem().window[mem_slot];

    if (info.type == WindowType::LENGTH) {
        while (mem.facts.size() > static_cast<size_t>(info.size)) {
            Fact* expired_fact = mem.facts.front();
            mem.facts.erase(mem.facts.begin());
            for (auto* child : children_raw) {
                child->right_activate(session, expired_fact, PropagationType::RETRACT);
            }
        }
    } else if (info.type == WindowType::TIME) {
        int64_t current_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        auto it = mem.facts.begin();
        while (it != mem.facts.end()) {
            Fact* f = *it;
            // Eager eviction relying on an implicit "timestamp" field.
            auto ts_opt = f->get_field("timestamp");
            int64_t fact_ts = current_time;
            if (ts_opt && std::holds_alternative<int64_t>(*ts_opt)) {
                fact_ts = std::get<int64_t>(*ts_opt);
            }

            if (current_time - fact_ts > info.size) {
                for (auto* child : children_raw) {
                    child->right_activate(session, f, PropagationType::RETRACT);
                }
                it = mem.facts.erase(it);
            } else {
                break;
            }
        }
    }
}

void WindowNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"WindowNode (" << id << ")\\n"
       << (info.type == WindowType::TIME ? "TIME " : "LENGTH ") << info.size
       << "\", shape=box3d, style=filled, fillcolor=lightblue];";
}

#include "rete_node_unnest.inc"

#include "rete_node_eval.inc"

#include "rete_node_terminal.inc"

#include "rete_node_query_terminal.inc"

#include "rete_node_query_input.inc"

#include "rete_node_query_call.inc"
