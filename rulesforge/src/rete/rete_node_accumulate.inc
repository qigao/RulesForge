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
    logi("[AccumulateNode::left_activate] node={} p_type={} wme={} mem.left.size={} mem.right.size={}",
         id, (int)token.type, (void*)wme, mem.left.size(), mem.right.size());
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
    logi("[AccumulateNode::left_activate] created result_fact ptr={} type={}", (void*)new_item.result_fact, result_fact_type);
    bool is_collect_list = (info.function == "collect" || info.function == "collectList");
    bool is_collect_set = (info.function == "collectSet");
    for (auto const& [fact_id, fact_ptr] : mem.right) {
        if (check_all_join_conditions(session, token, *fact_ptr, join_constraints, binding_to_token_idx)) {
            if (is_collect_set) {
                new_item.contributing_facts_set.insert(fact_ptr);
            } else if (is_collect_list) {
                new_item.contributing_facts_list.push_back(fact_ptr);
            } else {
                if (auto value_opt = get_accumulate_value(info, *fact_ptr)) {
                    new_item.accumulator->accumulate(*value_opt);
                }
            }
        }
    }
    mem.left[wme] = std::move(new_item);
    logi("[AccumulateNode::left_activate] stored item in mem.left, calling update_and_propagate_result");
    update_and_propagate_result(session, mem.left[wme]);
    logi("[AccumulateNode::left_activate] done");
}

void AccumulateNode::right_activate(StatefulSession& session, Fact* fact, PropagationType p_type) {
    auto& mem = session.net_mem().accumulate[mem_slot];
    logi("[AccumulateNode::right_activate] node={} fact_ptr={} fact_type={} fact_id={} p_type={} mem.left.size={} mem.right.size={}",
         id, (void*)fact, fact->type, fact->id, (int)p_type, mem.left.size(), mem.right.size());

    // Skip our own result facts to prevent self-referential infinite loop
    for (auto const& [wme_ptr, item] : mem.left) {
        logi("[AccumulateNode::right_activate] checking guard: item.result_fact={} vs fact={}", (void*)item.result_fact, (void*)fact);
        if (item.result_fact == fact) {
            logi("[AccumulateNode::right_activate] GUARD HIT - skipping result fact");
            return;
        }
    }

    bool is_collect_list = (info.function == "collect" || info.function == "collectList");
    bool is_collect_set = (info.function == "collectSet");

    // MODIFY requires full rebuild — the old field values are unknown
    if (p_type == PropagationType::MODIFY) {
        mem.right[fact->id] = fact;
        for (auto& [wme_ptr, item] : mem.left) {
            Token current_token{item.wme, PropagationType::ASSERT};
            item.accumulator->clear();
            item.contributing_facts_list.clear();
            item.contributing_facts_set.clear();
            for (auto const& [fact_id, f_ptr] : mem.right) {
                if (check_all_join_conditions(session, current_token, *f_ptr, join_constraints,
                                              binding_to_token_idx)) {
                    if (is_collect_set) {
                        item.contributing_facts_set.insert(f_ptr);
                    } else if (is_collect_list) {
                        item.contributing_facts_list.push_back(f_ptr);
                    } else {
                        if (auto value_opt = get_accumulate_value(info, *f_ptr)) {
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
                if (is_collect_set) {
                    item.contributing_facts_set.insert(fact);
                    update_and_propagate_result(session, item);
                } else if (is_collect_list) {
                    item.contributing_facts_list.push_back(fact);
                    update_and_propagate_result(session, item);
                } else {
                    if (auto value_opt = get_accumulate_value(info, *fact)) {
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
            if (is_collect_set) {
                item.contributing_facts_set.erase(fact);
                update_and_propagate_result(session, item);
            } else if (is_collect_list) {
                auto& list = item.contributing_facts_list;
                auto it = std::find(list.begin(), list.end(), fact);
                if (it != list.end()) {
                    *it = std::move(list.back());
                    list.pop_back();
                }
                update_and_propagate_result(session, item);
            } else {
                if (auto value_opt = get_accumulate_value(info, *fact)) {
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
    bool is_collect_list = (info.function == "collect" || info.function == "collectList");
    bool is_collect_set = (info.function == "collectSet");

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
                if (is_collect_set) {
                    item.contributing_facts_set.insert(fact);
                    changed = true;
                } else if (is_collect_list) {
                    item.contributing_facts_list.push_back(fact);
                    changed = true;
                } else {
                    if (auto value_opt = get_accumulate_value(info, *fact)) {
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
    logi("[AccumulateNode::update_and_propagate_result] node={} result_fact_ptr={} result_fact_id={} is_new={}",
         id, (void*)item.result_fact, item.result_fact->id, is_new_fact);
    auto modifier = [&](Fact& f) {
        bool is_collect = (info.function == "collect" || info.function == "collectList");
        bool is_collect_set = (info.function == "collectSet");
        if (is_collect) {
            FactList fl;
            fl.facts = item.contributing_facts_list;
            f.fields["result"] = fl;
        } else if (is_collect_set) {
            FactList fl;
            fl.facts.assign(item.contributing_facts_set.begin(), item.contributing_facts_set.end());
            f.fields["result"] = fl;
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
        logi("[AccumulateNode::update_and_propagate_result] calling session.add_fact for NEW result_fact ptr={}", (void*)item.result_fact);
        session.add_fact(item.result_fact);
        logi("[AccumulateNode::update_and_propagate_result] add_fact returned, result_fact id={}", item.result_fact->id);
        auto result_wme = session.get_or_create_wme(item.wme, item.result_fact);
        Token assert_token{result_wme, PropagationType::ASSERT};
        for (auto* c : children_raw) { c->left_activate(session, assert_token); }
    } else {
        logi("[AccumulateNode::update_and_propagate_result] calling session.update_fact for EXISTING result_fact ptr={} id={}", (void*)item.result_fact, item.result_fact->id);
        session.update_fact(item.result_fact, modifier);
        logi("[AccumulateNode::update_and_propagate_result] update_fact returned");
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
