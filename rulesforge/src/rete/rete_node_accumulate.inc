// --- AccumulateNode ---
AccumulateNode::AccumulateNode(IAccumulator const* prototype, ParsedAccumulate&& accumulate_info,
                               std::string res_fact_type,std::map<std::string, int> bindings,
                               std::vector<ParsedConstraint> joins) :
    ReteNode(NodeKind::Accumulate),
    accumulator_prototype(prototype), info(std::move(accumulate_info)), result_fact_type(std::move(res_fact_type)),
    binding_to_token_idx(std::move(bindings)), join_constraints(std::move(joins)) {}

void AccumulateNode::left_activate(StatefulSession& session, Token const& token) {
    auto& mem = session.net_mem().accumulate[mem_slot];
    auto const& wme = token.wme;
    if (token.type == PropagationType::RETRACT) {
        auto it = mem.left.find(wme);
        if (it != mem.left.end()) {
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
    bool is_mir_numeric = uses_mir_numeric_accumulate(info);
    bool is_mir_collect = uses_mir_collect_accumulate(info);
    for (auto const& [fact_id, fact_ptr] : mem.right) {
        if (check_all_join_conditions(session, token, *fact_ptr, join_constraints, binding_to_token_idx)) {
            if (is_mir_numeric) {
                apply_mir_numeric_accumulate(session, info, new_item, *fact_ptr, &token, &binding_to_token_idx, 1);
            } else if (is_mir_collect) {
                apply_mir_collect_accumulate(session, info, new_item, fact_ptr, 1);
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

    bool is_mir_numeric = uses_mir_numeric_accumulate(info);
    bool is_mir_collect = uses_mir_collect_accumulate(info);

    // MODIFY requires full rebuild — the old field values are unknown
    if (p_type == PropagationType::MODIFY) {
        mem.right[fact->id] = fact;
        for (auto& [wme_ptr, item] : mem.left) {
            Token current_token{item.wme, PropagationType::ASSERT};
            item.accumulator->clear();
            reset_mir_numeric_accumulate(item);
            reset_mir_collect_accumulate(item);
            for (auto const& [fact_id, f_ptr] : mem.right) {
                if (check_all_join_conditions(session, current_token, *f_ptr, join_constraints,
                                              binding_to_token_idx)) {
                    if (is_mir_numeric) {
                        apply_mir_numeric_accumulate(session, info, item, *f_ptr, &current_token, &binding_to_token_idx, 1);
                    } else if (is_mir_collect) {
                        apply_mir_collect_accumulate(session, info, item, f_ptr, 1);
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
            if (check_all_join_conditions(session, token, *fact, join_constraints, binding_to_token_idx)) {
                if (is_mir_numeric) {
                    if (apply_mir_numeric_accumulate(session, info, item, *fact, &token, &binding_to_token_idx, 1)) {
                        update_and_propagate_result(session, item);
                    }
                } else if (is_mir_collect) {
                    if (apply_mir_collect_accumulate(session, info, item, fact, 1)) {
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
        if (check_all_join_conditions(session, token, *fact, join_constraints, binding_to_token_idx)) {
            if (is_mir_numeric) {
                if (apply_mir_numeric_accumulate(session, info, item, *fact, &token, &binding_to_token_idx, -1)) {
                    update_and_propagate_result(session, item);
                }
            } else if (is_mir_collect) {
                if (apply_mir_collect_accumulate(session, info, item, fact, -1)) {
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
    bool is_mir_numeric = uses_mir_numeric_accumulate(info);
    bool is_mir_collect = uses_mir_collect_accumulate(info);

    // Store all facts into right memory first
    for (auto& fact : facts) {
        mem.right[fact->id] = fact;
    }

    // Single pass over left memory, accumulate all new facts per token
    for (auto& [wme_ptr, item] : mem.left) {
        Token token{item.wme, PropagationType::ASSERT};
        bool changed = false;
        for (auto& fact : facts) {
            if (check_all_join_conditions(session, token, *fact, join_constraints, binding_to_token_idx)) {
                if (is_mir_numeric) {
                    if (apply_mir_numeric_accumulate(session, info, item, *fact, &token, &binding_to_token_idx, 1)) {
                        changed = true;
                    }
                } else if (is_mir_collect) {
                    if (apply_mir_collect_accumulate(session, info, item, fact, 1)) {
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
        if (uses_mir_collect_accumulate(info)) {
            f.fields["result"] = mir_collect_accumulate_result(session, info, item);
        } else if (uses_mir_numeric_accumulate(info)) {
            ConstraintValue result_val = mir_numeric_accumulate_result(info, item);
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
        auto result_wme = session.get_or_create_wme(item.wme, item.result_fact);
        Token assert_token{result_wme, PropagationType::ASSERT};
        for (auto* c : children_raw) { c->left_activate(session, assert_token); }
    } else {
        auto result_wme = session.get_or_create_wme(item.wme, item.result_fact);
        Token retract_token{result_wme, PropagationType::RETRACT};
        for (auto* c : children_raw) { c->left_activate(session, retract_token); }
        session.update_fact(item.result_fact, modifier);
        Token assert_token{result_wme, PropagationType::ASSERT};
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
