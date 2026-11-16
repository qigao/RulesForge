#include "diagnostics/logging_control.hpp"

#include "runtime/knowledge_base.hpp"
#include "rete/nodes/transform.hpp"
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

using namespace rulesforge::rete_detail;

// --- AccumulateNode ---
namespace {
    std::optional<ConstraintValue> get_accumulate_value(
        StatefulSession& session,
        ParsedAccumulate const& info,
        Fact const& fact,
        Token const* token,
        std::map<std::string, int> const* bindings)
    {
        if (info.function == "count" && !info.uses_runtime_value_expression && info.accumulate_field_name.empty()) {
            return ConstraintValue{int64_t{1}};
        }
        if (info.uses_runtime_value_expression) {
            return evaluate_runtime_expression(info.value_program, fact, token, bindings, &info.inline_binding_to_field);
        }
        // Otherwise use simple field lookup
        if (!info.accumulate_field_name.empty()) {
            return fact.get_field(info.accumulate_field_name);
        }
        return std::nullopt;
    }

    bool uses_numeric_accumulate(ParsedAccumulate const& info) {
        if (info.function == "count" && !info.uses_runtime_value_expression) {
            return true;
        }
        if (info.function != "sum"
            && info.function != "average"
            && info.function != "min"
            && info.function != "max") {
            return false;
        }
        return info.uses_runtime_value_expression || !info.accumulate_field_name.empty();
    }

    bool uses_collect_accumulate(ParsedAccumulate const& info) {
        return !info.uses_runtime_value_expression
            && (info.function == "collect" || info.function == "collectList" || info.function == "collectSet");
    }

    void reset_collect_accumulate(NetworkMemory::AccumulateMem::LeftMemoryItem& item) {
        item.contributing_facts_list.clear();
        item.contributing_facts_set.clear();
    }

    bool apply_collect_accumulate(
        StatefulSession& session,
        ParsedAccumulate const& info,
        NetworkMemory::AccumulateMem::LeftMemoryItem& item,
        Fact* fact,
        std::int64_t direction) {
        (void)session;
        if (info.function == "collectSet") {
            if (direction >= 0) {
                return item.contributing_facts_set.insert(fact).second;
            }
            return item.contributing_facts_set.erase(fact) > 0;
        } else if (info.function == "collect" || info.function == "collectList") {
            if (direction >= 0) {
                item.contributing_facts_list.push_back(fact);
                return true;
            }
            auto it = std::find(item.contributing_facts_list.begin(), item.contributing_facts_list.end(), fact);
            if (it == item.contributing_facts_list.end()) {
                return false;
            }
            item.contributing_facts_list.erase(it);
            return true;
        } else {
            throw_lhs_runtime_not_lowered("collect accumulate kernel");
        }
    }

    FactList collect_accumulate_result(
        StatefulSession& session,
        ParsedAccumulate const& info,
        NetworkMemory::AccumulateMem::LeftMemoryItem const& item) {
        (void)session;
        FactList result;
        if (info.function == "collectSet") {
            result.facts.assign(item.contributing_facts_set.begin(), item.contributing_facts_set.end());
        } else if (info.function == "collect" || info.function == "collectList") {
            result.facts = item.contributing_facts_list;
        } else {
            throw_lhs_runtime_not_lowered("collect accumulate result");
        }
        return result;
    }

    void reset_numeric_accumulate(NetworkMemory::AccumulateMem::LeftMemoryItem& item) {
        item.numeric_sum = 0.0;
        item.numeric_extreme = 0.0;
        item.aggregate_count = 0;
        item.sum_is_double = false;
        item.numeric_values.clear();
    }

    bool apply_numeric_accumulate(
        StatefulSession& session,
        ParsedAccumulate const& info,
        NetworkMemory::AccumulateMem::LeftMemoryItem& item,
        Fact const& fact,
        Token const* token,
        std::map<std::string, int> const* bindings,
        std::int64_t direction) {
        if (info.function == "count") {
            item.aggregate_count += direction;
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
            double numeric_value = std::holds_alternative<int64_t>(*value_opt)
                ? static_cast<double>(std::get<int64_t>(*value_opt))
                : std::get<double>(*value_opt);
            item.numeric_sum += numeric_value * static_cast<double>(direction);
            if (info.function == "average") {
                item.aggregate_count += direction;
            }
            if (std::holds_alternative<double>(*value_opt)) {
                item.sum_is_double = true;
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
                item.sum_is_double = true;
            } else {
                return false;
            }

            if (direction >= 0) {
                item.numeric_values.insert(numeric_value);
            } else {
                auto it = item.numeric_values.find(numeric_value);
                if (it != item.numeric_values.end()) {
                    item.numeric_values.erase(it);
                }
            }

            if (item.numeric_values.empty()) {
                item.numeric_extreme = 0.0;
                return true;
            }

            double result = *item.numeric_values.begin();
            for (auto it = std::next(item.numeric_values.begin());
                 it != item.numeric_values.end();
                 ++it) {
                result = (info.function == "min") ? std::min(result, *it) : std::max(result, *it);
            }
            item.numeric_extreme = result;
            return true;
        }

        throw_lhs_runtime_not_lowered("accumulate kernel");
    }

    ConstraintValue numeric_accumulate_result(
        ParsedAccumulate const& info,
        NetworkMemory::AccumulateMem::LeftMemoryItem const& item) {
        if (info.function == "count") {
            return item.aggregate_count;
        }
        if (info.function == "average") {
            if (item.aggregate_count == 0) {
                return 0.0;
            }
            return item.numeric_sum / static_cast<double>(item.aggregate_count);
        }
        if (info.function == "min" || info.function == "max") {
            if (item.sum_is_double) {
                return item.numeric_extreme;
            }
            return static_cast<int64_t>(item.numeric_extreme);
        }
        if (item.sum_is_double) {
            return item.numeric_sum;
        }
        return static_cast<int64_t>(item.numeric_sum);
    }
}

// --- AccumulateNode ---
AccumulateNode::AccumulateNode(IAccumulator const* prototype, ParsedAccumulate&& accumulate_info,
                               std::string res_fact_type,std::map<std::string, int> bindings,
                               std::vector<ParsedConstraint> joins,
                               std::map<std::string, std::string> scalar_fields,
                               std::unique_ptr<ConstraintNode> root,
                               std::vector<ParsedConstraint> constraints,
                               std::vector<std::optional<rulesforge::RuntimePredicateRef>> predicates,
                               std::unique_ptr<ConstraintNode> source,
                               std::vector<std::optional<rulesforge::RuntimePredicateRef>> source_refs) :
    ReteNode(NodeKind::Accumulate),
    accumulator_prototype(prototype), info(std::move(accumulate_info)), result_fact_type(std::move(res_fact_type)),
    binding_to_token_idx(std::move(bindings)), join_constraints(std::move(joins)),
    scalar_binding_fields(std::move(scalar_fields)),
    result_root(std::move(root)), result_constraints(std::move(constraints)),
    result_predicates(std::move(predicates)), source_root(std::move(source)),
    source_predicates(std::move(source_refs)) {}

bool AccumulateNode::check_source(StatefulSession& session, Token const& token, Fact const& fact) const {
    if (!source_root) return check_all_join_conditions(session, token, fact, join_constraints,
        binding_to_token_idx, scalar_binding_fields);
    size_t leaf = 0;
    return check_accumulate_result(session,token,fact,source_root.get(),join_constraints,
        binding_to_token_idx,scalar_binding_fields,source_predicates,leaf);
}

void AccumulateNode::left_activate(StatefulSession& session, Token const& token) {
    auto& mem = session.net_mem().accumulate[mem_slot];
    auto const& wme = token.wme;
    if (token.type == PropagationType::RETRACT) {
        auto it = mem.left.find(wme);
        if (it != mem.left.end()) {
            if (it->second.result_propagated) {
                it->second.result_propagated = false;
                auto result_wme = session.get_or_create_wme(it->second.wme, it->second.result_fact);
                Token retract{result_wme, PropagationType::RETRACT};
                for (auto* child : children_raw) child->left_activate(session, retract);
            }
            session.retract_fact(it->second.result_fact);
            mem.left.erase(it);
        }
        return;
    }
    if (mem.left.count(wme)) return;
    NetworkMemory::AccumulateMem::LeftMemoryItem new_item;
    new_item.wme = wme;
    new_item.accumulator = accumulator_prototype->clone();
    new_item.result_fact = session.create_fact(result_fact_type);
    bool is_numeric_accumulate = uses_numeric_accumulate(info);
    bool is_collect_accumulate = uses_collect_accumulate(info);
    for (auto const& [fact_id, fact_ptr] : mem.right) {
        if (check_source(session, token, *fact_ptr)) {
            if (is_numeric_accumulate) {
                apply_numeric_accumulate(session, info, new_item, *fact_ptr, &token, &binding_to_token_idx, 1);
            } else if (is_collect_accumulate) {
                apply_collect_accumulate(session, info, new_item, fact_ptr, 1);
            } else {
                if (auto value_opt = get_accumulate_value(session, info, *fact_ptr, &token, &binding_to_token_idx)) {
                    new_item.accumulator->accumulate(*value_opt);
                }
            }
        }
    }
    mem.left[wme] = std::move(new_item);
    update_and_propagate_result(session, mem.left[wme]);
}

void AccumulateNode::right_activate(StatefulSession& session, Fact* fact, PropagationType p_type) {
    auto& mem = session.net_mem().accumulate[mem_slot];

    // Skip our own result facts to prevent self-referential infinite loop
    for (auto const& [wme_ptr, item] : mem.left) {
        if (item.result_fact == fact) {
            return;
        }
    }

    bool is_numeric_accumulate = uses_numeric_accumulate(info);
    bool is_collect_accumulate = uses_collect_accumulate(info);

    // MODIFY requires full rebuild — the old field values are unknown
    if (p_type == PropagationType::MODIFY) {
        mem.right[fact->id] = fact;
        for (auto& [wme_ptr, item] : mem.left) {
            Token current_token{item.wme, PropagationType::ASSERT};
            item.accumulator->clear();
            reset_numeric_accumulate(item);
            reset_collect_accumulate(item);
            for (auto const& [fact_id, f_ptr] : mem.right) {
                if (check_source(session, current_token, *f_ptr)) {
                    if (is_numeric_accumulate) {
                        apply_numeric_accumulate(session, info, item, *f_ptr, &current_token, &binding_to_token_idx, 1);
                    } else if (is_collect_accumulate) {
                        apply_collect_accumulate(session, info, item, f_ptr, 1);
                    } else {
                        if (auto value_opt = get_accumulate_value(session, info, *f_ptr, &current_token, &binding_to_token_idx)) {
                            item.accumulator->accumulate(*value_opt);
                        }
                    }
                }
            }
            update_and_propagate_result(session, item);
        }
        return;
    }

    // ASSERT: incremental add
    if (p_type == PropagationType::ASSERT) {
        mem.right[fact->id] = fact;
        for (auto& [wme_ptr, item] : mem.left) {
            Token token{item.wme, PropagationType::ASSERT};
            if (check_source(session, token, *fact)) {
                if (is_numeric_accumulate) {
                    if (apply_numeric_accumulate(session, info, item, *fact, &token, &binding_to_token_idx, 1)) {
                        update_and_propagate_result(session, item);
                    }
                } else if (is_collect_accumulate) {
                    if (apply_collect_accumulate(session, info, item, fact, 1)) {
                        update_and_propagate_result(session, item);
                    }
                } else {
                    if (auto value_opt = get_accumulate_value(session, info, *fact, &token, &binding_to_token_idx)) {
                        item.accumulator->accumulate(*value_opt);
                        update_and_propagate_result(session, item);
                    }
                }
            }
        }
        return;
    }

    // RETRACT: incremental remove
    if (mem.right.count(fact->id) == 0) return;
    for (auto& [wme_ptr, item] : mem.left) {
        Token token{item.wme, PropagationType::ASSERT};
        if (check_source(session, token, *fact)) {
            if (is_numeric_accumulate) {
                if (apply_numeric_accumulate(session, info, item, *fact, &token, &binding_to_token_idx, -1)) {
                    update_and_propagate_result(session, item);
                }
            } else if (is_collect_accumulate) {
                if (apply_collect_accumulate(session, info, item, fact, -1)) {
                    update_and_propagate_result(session, item);
                }
            } else {
                if (auto value_opt = get_accumulate_value(session, info, *fact, &token, &binding_to_token_idx)) {
                    item.accumulator->reverse(*value_opt);
                    update_and_propagate_result(session, item);
                }
            }
        }
    }
    mem.right.erase(fact->id);
}

void AccumulateNode::right_activate_batch(StatefulSession& session, std::vector<Fact*>& facts, PropagationType p_type) {
    auto& mem = session.net_mem().accumulate[mem_slot];
    bool is_numeric_accumulate = uses_numeric_accumulate(info);
    bool is_collect_accumulate = uses_collect_accumulate(info);

    // Store all facts into right memory first
    for (auto& fact : facts) {
        mem.right[fact->id] = fact;
    }

    // Single pass over left memory, accumulate all new facts per token
    for (auto& [wme_ptr, item] : mem.left) {
        Token token{item.wme, PropagationType::ASSERT};
        bool changed = false;
        for (auto& fact : facts) {
            if (check_source(session, token, *fact)) {
                if (is_numeric_accumulate) {
                    if (apply_numeric_accumulate(session, info, item, *fact, &token, &binding_to_token_idx, 1)) {
                        changed = true;
                    }
                } else if (is_collect_accumulate) {
                    if (apply_collect_accumulate(session, info, item, fact, 1)) {
                        changed = true;
                    }
                } else {
                    if (auto value_opt = get_accumulate_value(session, info, *fact, &token, &binding_to_token_idx)) {
                        item.accumulator->accumulate(*value_opt);
                        changed = true;
                    }
                }
            }
        }
        if (changed) {
            update_and_propagate_result(session, item);
        }
    }
}

void AccumulateNode::right_activate_deferred(StatefulSession& session, Fact* fact, PropagationType p_type) {
    auto& mem = session.net_mem().accumulate[mem_slot];
    mem.pending_facts.push_back(fact);
    mem.dirty = true;
}

void AccumulateNode::right_activate_batch_deferred(StatefulSession& session, std::vector<Fact*>& facts, PropagationType p_type) {
    auto& mem = session.net_mem().accumulate[mem_slot];
    mem.pending_facts.insert(mem.pending_facts.end(), facts.begin(), facts.end());
    mem.dirty = true;
}

bool AccumulateNode::flush_pending(StatefulSession& session) {
    auto& mem = session.net_mem().accumulate[mem_slot];
    if (!mem.dirty) return false;
    mem.dirty = false;
    auto pending = std::move(mem.pending_facts);
    mem.pending_facts.clear();
    right_activate_batch(session, pending, PropagationType::ASSERT);
    return true;
}

void AccumulateNode::update_and_propagate_result(StatefulSession& session, NetworkMemory::AccumulateMem::LeftMemoryItem& item) {
    bool is_new_fact = (item.result_fact->id == 0);
    auto modifier = [&](Fact& f) {
        if (uses_collect_accumulate(info)) {
            f.fields["result"] = collect_accumulate_result(session, info, item);
        } else if (uses_numeric_accumulate(info)) {
            ConstraintValue result_val = numeric_accumulate_result(info, item);
            f.fields["result"] = result_val;

            double numeric_result = 0.0;
            if (std::holds_alternative<int64_t>(result_val)) {
                numeric_result = static_cast<double>(std::get<int64_t>(result_val));
            } else if (std::holds_alternative<double>(result_val)) {
                numeric_result = std::get<double>(result_val);
            }
            f.fields["intValue"] = static_cast<int64_t>(numeric_result);
            f.fields["longValue"] = static_cast<int64_t>(numeric_result);
            f.fields["doubleValue"] = numeric_result;
            f.fields["floatValue"] = numeric_result;
            f.fields["value"] = numeric_result;
        } else {
            ConstraintValue result_val = item.accumulator->get_result();
            f.fields["result"] = result_val;

            // For Number type, provide Java-like accessor methods (intValue, doubleValue, etc.)
            // This fulfills the contract promised by the Number type schema
            double numeric_result = 0.0;
            if (std::holds_alternative<int64_t>(result_val)) {
                numeric_result = static_cast<double>(std::get<int64_t>(result_val));
            } else if (std::holds_alternative<double>(result_val)) {
                numeric_result = std::get<double>(result_val);
            }
            f.fields["intValue"] = static_cast<int64_t>(numeric_result);
            f.fields["longValue"] = static_cast<int64_t>(numeric_result);
            f.fields["doubleValue"] = numeric_result;
            f.fields["floatValue"] = numeric_result;
            f.fields["value"] = numeric_result;
        }
    };
    if (is_new_fact) {
        modifier(*item.result_fact);
        session.add_fact(item.result_fact);
    } else {
        if (item.result_propagated) {
            item.result_propagated = false;
            auto result_wme = session.get_or_create_wme(item.wme, item.result_fact);
            Token retract_token{result_wme, PropagationType::RETRACT};
            for (auto* c : children_raw) { c->left_activate(session, retract_token); }
        }
        session.update_fact(item.result_fact, modifier);
    }
    auto result_wme = session.get_or_create_wme(item.wme, item.result_fact);
    Token assert_token{result_wme, PropagationType::ASSERT};
    size_t leaf = 0;
    if (check_accumulate_result(session, assert_token, *item.result_fact, result_root.get(),
            result_constraints, binding_to_token_idx, scalar_binding_fields, result_predicates, leaf)) {
        item.result_propagated = true;
        for (auto* c : children_raw) { c->left_activate(session, assert_token); }
    }
}

void AccumulateNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"AccumulateNode (" << id << ")\\nFunction: " << info.function;
    if (!info.accumulate_field_name.empty()) { os << "\\nField: " << info.accumulate_field_name; }
    os << "\\nResult Type: " << result_fact_type;
    if (!join_constraints.empty()) {
        os << "\\nJoins:";
        for (auto const& join : join_constraints) { os << "\\n" << constraint_to_string(join); }
    }
    os << "\", shape=cylinder, style=filled, fillcolor=plum];";
}

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
        auto position = std::find(mem.facts.begin(), mem.facts.end(), fact);
        if (position != mem.facts.end()) {
            mem.facts.erase(position);
            for (auto* child : children_raw) {
                child->right_activate(session, fact, p_type);
            }
        }
        return;
    }

    if (p_type == PropagationType::ASSERT) {
        if (info.type == WindowType::TIME) {
            auto timestamp_of = [](Fact const* item) {
                auto ts = item ? item->get_field("timestamp") : std::nullopt;
                return ts && std::holds_alternative<int64_t>(*ts)
                    ? std::get<int64_t>(*ts)
                    : std::numeric_limits<int64_t>::max();
            };
            auto const fact_ts = session.fact_event_time(fact).value_or(timestamp_of(fact));
            auto position = std::upper_bound(
                mem.facts.begin(), mem.facts.end(), fact_ts,
                [&](int64_t timestamp, Fact const* item) {
                    return timestamp < session.fact_event_time(item).value_or(timestamp_of(item));
                });
            mem.facts.insert(position, fact);
        } else {
            mem.facts.push_back(fact);
        }
        evaluate_expiration(session);
        if (std::find(mem.facts.begin(), mem.facts.end(), fact) != mem.facts.end()) {
            for (auto* child : children_raw) {
                child->right_activate(session, fact, p_type);
            }
        }
    } else if (p_type == PropagationType::MODIFY) {
        evaluate_expiration(session);
        for (auto* child : children_raw) {
            child->right_activate(session, fact, p_type);
        }
    }
}

std::size_t WindowNode::evaluate_expiration(StatefulSession& session) {
    if (mem_slot < 0 || static_cast<size_t>(mem_slot) >= session.net_mem().window.size()) {
        throw std::runtime_error("WindowNode memory slot is not initialized");
    }
    auto& mem = session.net_mem().window[mem_slot];
    std::size_t expired_count = 0;

    if (info.type == WindowType::LENGTH) {
        while (mem.facts.size() > static_cast<size_t>(info.size)) {
            Fact* expired_fact = mem.facts.front();
            mem.facts.erase(mem.facts.begin());
            for (auto* child : children_raw) {
                child->right_activate(session, expired_fact, PropagationType::RETRACT);
            }
            ++expired_count;
        }
    } else if (info.type == WindowType::TIME) {
        if (session.is_event_time_mode() && !session.event_time_watermark()) {
            return expired_count;
        }
        int64_t current_time = session.event_time_watermark().value_or(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());

        auto it = mem.facts.begin();
        while (it != mem.facts.end()) {
            Fact* f = *it;
            // Eager eviction relying on an implicit "timestamp" field.
            auto event_ts = session.fact_event_time(f);
            auto ts_opt = f->get_field("timestamp");
            int64_t fact_ts = current_time;
            if (event_ts) {
                fact_ts = *event_ts;
            } else if (ts_opt && std::holds_alternative<int64_t>(*ts_opt)) {
                fact_ts = std::get<int64_t>(*ts_opt);
            }

            if (current_time - fact_ts > info.size) {
                for (auto* child : children_raw) {
                    child->right_activate(session, f, PropagationType::RETRACT);
                }
                it = mem.facts.erase(it);
                ++expired_count;
            } else {
                break;
            }
        }
    }
    return expired_count;
}

void WindowNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"WindowNode (" << id << ")\\n"
       << (info.type == WindowType::TIME ? "TIME " : "LENGTH ") << info.size
       << "\", shape=box3d, style=filled, fillcolor=lightblue];";
}
// --- UnnestNode ---
UnnestNode::UnnestNode(
    ParsedUnnest const& unnest_info,
    std::string target_fact_type,
    std::map<std::string, int> const& bindings,
    std::vector<ParsedConstraint> constraints,
    std::vector<std::optional<rulesforge::RuntimePredicateRef>> runtime_predicates) :
    ReteNode(NodeKind::Unnest),
    info(unnest_info),
    target_fact_type_(std::move(target_fact_type)),
    binding_to_token_idx(bindings),
    constraints_(std::move(constraints)),
    runtime_predicates_(std::move(runtime_predicates)) {
    for (auto& constraint : constraints_) {
        if (constraint.left_field.find('.') != std::string::npos
            || constraint.left_field.find('[') != std::string::npos) {
            constraint.cached_left_field_path = parse_field_path(constraint.left_field);
        }
    }
}

void UnnestNode::left_activate(StatefulSession& session, Token const& token) {
    auto& mem = session.net_mem().unnest[mem_slot];

    if (token.type == PropagationType::RETRACT) {
        auto it = mem.parent_to_children.find(token.wme);
        if (it != mem.parent_to_children.end()) {
            for (auto const& child_wme : it->second.second) {
                Token child_token{child_wme, PropagationType::RETRACT};
                for (auto* child : children_raw) { child->left_activate(session, child_token); }
            }
            mem.parent_to_children.erase(it);
        }
        return;
    }

    auto bind_it = binding_to_token_idx.find(info.source_binding);
    if (bind_it == binding_to_token_idx.end()) return;
    auto fact_at_depth = token.get_fact_at_depth(bind_it->second);
    if (!fact_at_depth) return;

    auto field_it = fact_at_depth->fields.find(info.source_field);
    if (field_it == fact_at_depth->fields.end()) return;

    std::vector<TokenWME const*> child_wmes;
    if (std::holds_alternative<FactList>(field_it->second)) {
        auto const& list = std::get<FactList>(field_it->second).facts;
        if (list.empty()) return;
        child_wmes.reserve(list.size());
        for (auto const& item_fact : list) {
            if (item_fact == nullptr) {
                continue;
            }
            if (!target_fact_type_.empty() && item_fact->type != target_fact_type_) {
                continue;
            }
            auto new_wme = session.get_or_create_wme(token.wme, item_fact);
            Token new_token{new_wme, PropagationType::ASSERT};
            if (!check_all_runtime_constraints(
                    session,
                    new_token,
                    *item_fact,
                    constraints_,
                    binding_to_token_idx,
                    runtime_predicates_)) {
                continue;
            }
            child_wmes.push_back(new_wme);
            for (auto* c : children_raw) { c->left_activate(session, new_token); }
        }
    }

    mem.parent_to_children[token.wme] = {token.wme, std::move(child_wmes)};
}

void UnnestNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"UnnestNode (" << id << ")\\nFrom: " << info.source_binding << "."
       << info.source_field << "\", shape=trapezium, style=filled, fillcolor=darksalmon];";
}

// --- EvalNode ---
EvalNode::EvalNode(std::string expr,
                   std::map<std::string, int> bindings,
                   std::map<std::string, std::string> scalar_binding_fields,
                   std::optional<rulesforge::RuntimePredicateRef> eval_runtime_predicate,
                   std::vector<std::string> eval_runtime_variables,
                   std::vector<EvalRuntimeArgument> runtime_arguments) :
    ReteNode(NodeKind::Eval),
    expression(std::move(expr)),
    binding_to_token_idx(std::move(bindings)),
    scalar_binding_to_field(std::move(scalar_binding_fields)),
    eval_runtime_predicate_(std::move(eval_runtime_predicate)),
    eval_runtime_variables_(std::move(eval_runtime_variables)),
    runtime_arguments_(std::move(runtime_arguments)) {

    if (!expression.empty() && !eval_runtime_predicate_)
        expression_program_ = rulesforge::ExpressionDescriptor::compile_required(expression);
    for (auto& argument : runtime_arguments_)
        if (argument.kind == EvalRuntimeArgument::Kind::NumericExpression)
            argument.program = rulesforge::ExpressionDescriptor::compile_required(argument.text);
    if (runtime_arguments_.empty()) {
        runtime_arguments_.reserve(eval_runtime_variables_.size());
        for (auto const& variable : eval_runtime_variables_) {
            runtime_arguments_.push_back(
                EvalRuntimeArgument{EvalRuntimeArgument::Kind::Variable, variable, NilValue{}});
        }
    }
    if (!expression.empty() && eval_runtime_predicate_) {
        for (auto const& runtime_arg : runtime_arguments_) {
            if (runtime_arg.kind != EvalRuntimeArgument::Kind::Variable) {
                continue;
            }
            auto const& var_name = runtime_arg.text;
            EvalResolvedVar r{};
            std::string binding_name = trim_ascii_copy(var_name);
            size_t dot_pos = var_name.find('.');
            if (dot_pos != std::string::npos) {
                binding_name = trim_ascii_copy(var_name.substr(0, dot_pos));
                r.field_name = trim_ascii_copy(var_name.substr(dot_pos + 1));
            } else {
                auto field_it = scalar_binding_to_field.find(binding_name);
                if (field_it != scalar_binding_to_field.end()) {
                    r.field_name = field_it->second;
                } else {
                    r.field_name = "this";
                }
            }
            r.use_fact_id = (r.field_name == "this");

            auto it = binding_to_token_idx.find(binding_name);
            if (it == binding_to_token_idx.end() && !binding_name.empty() && binding_name[0] != '$') {
                it = binding_to_token_idx.find("$" + binding_name);
            }
            if (it != binding_to_token_idx.end()) r.token_depth = it->second;
            resolved_vars_.emplace(var_name, std::move(r));
        }
    }
}

void EvalNode::left_activate(StatefulSession& session, Token const& token) {
    auto const* wme = token.wme;
    auto& mem = session.net_mem().eval[mem_slot];
    if (token.type == PropagationType::RETRACT) {
        if (mem.memory.erase(wme) > 0) {
            for (auto* c : children_raw) { c->left_activate(session, token); }
        }
        return;
    }
    bool result = false;
    if (expression.empty()) {
        result = true;
    } else if (eval_runtime_predicate_) {
        auto kb = session.get_knowledge_base();
        if (!kb) {
            throw std::runtime_error("KnowledgeBase is required for eval(...) predicate");
        }
        std::vector<ConstraintValue> args;
        args.reserve(runtime_arguments_.size());
        for (auto const& runtime_arg : runtime_arguments_) {
            if (runtime_arg.kind == EvalRuntimeArgument::Kind::Literal) {
                args.push_back(runtime_arg.literal);
                continue;
            }
            if (runtime_arg.kind == EvalRuntimeArgument::Kind::NumericExpression) {
                auto const* current_fact = token.get_fact();
                if (current_fact == nullptr) {
                    return;
                }
                auto expression_value = evaluate_runtime_expression(
                    runtime_arg.program,
                    *current_fact,
                    &token,
                    &binding_to_token_idx,
                    &scalar_binding_to_field);
                if (!expression_value) {
                    return;
                }
                auto numeric = numeric_value(*expression_value);
                if (!numeric) {
                    return;
                }
                args.push_back(*numeric);
                continue;
            }

            auto const& variable = runtime_arg.text;
            auto resolved_it = resolved_vars_.find(variable);
            if (resolved_it == resolved_vars_.end()) {
                throw_lhs_runtime_not_lowered("eval(...) predicate");
            }
            auto const& resolved = resolved_it->second;
            auto* fact = token.get_fact_at_depth(resolved.token_depth);
            if (fact == nullptr) {
                return;
            }
            ConstraintValue value;
            if (resolved.use_fact_id) {
                value = static_cast<int64_t>(fact->id);
            } else {
                auto field_value = fact->get_field(resolved.field_name);
                if (!field_value) {
                    return;
                }
                value = std::move(*field_value);
            }
            args.push_back(std::move(value));
        }
        auto runtime_result = kb->runtime_predicate(*eval_runtime_predicate_, args);
        if (!runtime_result) {
            throw_lhs_runtime_predicate_failed(*eval_runtime_predicate_, "eval(...) predicate");
        }
        result = *runtime_result;
    } else {
        auto const* current_fact = token.get_fact();
        if (current_fact == nullptr) {
            return;
        }
        auto evaluated = evaluate_runtime_expression(
            expression_program_,
            *current_fact,
            &token,
            &binding_to_token_idx,
            &scalar_binding_to_field);
        if (!evaluated) {
            return;
        }
        result = rulesforge::ExpressionDescriptor::truthy(*evaluated);
    }
    if (result) {
        mem.memory[wme] = wme;
        for (auto* c : children_raw) { c->left_activate(session, token); }
    }
}

void EvalNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"EvalNode (" << id << ")\\n" << expression;
    os << "\\nBindings: ";
    bool first = true;
    for (auto const& [name, idx] : binding_to_token_idx) {
        if (!first) os << ", ";
        os << name << "->" << idx;
        first = false;
    }
    if (eval_runtime_predicate_) {
        os << "\\nNative eval predicate: " << eval_runtime_predicate_->external_name;
    }
    os << "\", shape=underline, style=filled, fillcolor=orchid];";
}
