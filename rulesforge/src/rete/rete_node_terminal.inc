// --- TerminalNode ---
TerminalNode::TerminalNode(ParsedRule const& r,std::map<std::string, int> b) :
    ReteNode(NodeKind::Terminal), rule_name(r.name), cached_rule_(&r), binding_to_token_idx(std::move(b)) {}

void TerminalNode::left_activate(StatefulSession& session, Token const& token) {
    auto& mem = session.net_mem().terminal[mem_slot];
    auto wme_ptr = token.wme;

    auto const* rule = cached_rule_;
    if (!rule) {
        rule = session.get_knowledge_base()->find_rule_by_name(rule_name);
        if (!rule) return;
    }

    if (token.type == PropagationType::RETRACT) {
        if (mem.memory.erase(wme_ptr) > 0) {
            size_t hash = std::hash<TokenWME const*>{}(wme_ptr) ^ reinterpret_cast<uintptr_t>(rule);
            session.remove_activation(hash);
            for (auto& listener : session.get_listeners()) {
                listener->on_activation_retracted(rule->name, token.get_facts());
            }
        }
        session.logical_retract(wme_ptr);
    } else if (token.type == PropagationType::ASSERT) {
        auto [it, inserted] = mem.memory.insert(wme_ptr);
        if (inserted) {
            size_t hash = std::hash<TokenWME const*>{}(wme_ptr) ^ reinterpret_cast<uintptr_t>(rule);
            Activation activation{rule, token, hash, &this->binding_to_token_idx};
            session.add_activation(activation);
            // Notify listeners about creation
            auto& listeners = session.get_listeners();
            if (!listeners.empty()) {
                for (auto& listener : listeners) {
                    listener->on_activation_created(rule->name, token.get_facts());
                }
            }
            if (rule->auto_focus && rule->agenda_group) {
                session.set_focus(*rule->agenda_group);
            }
        }
    }
}

void TerminalNode::left_activate_batch(StatefulSession& session, std::vector<Token>& tokens) {
    if (tokens.empty()) return;

    auto& mem = session.net_mem().terminal[mem_slot];
    auto const* rule = cached_rule_;
    if (!rule) {
        rule = session.get_knowledge_base()->find_rule_by_name(rule_name);
        if (!rule) return;
    }

    // PHREAK: pre-reserve memory for batch insert
    mem.memory.reserve(mem.memory.size() + tokens.size());

    // Separate ASSERT and RETRACT tokens
    std::vector<Activation> batch_activations;
    batch_activations.reserve(tokens.size());

    auto& listeners = session.get_listeners();
    bool has_listeners = !listeners.empty();

    for (auto& token : tokens) {
        auto wme_ptr = token.wme;

        if (token.type == PropagationType::RETRACT) {
            if (mem.memory.erase(wme_ptr) > 0) {
                size_t hash = std::hash<TokenWME const*>{}(wme_ptr) ^ reinterpret_cast<uintptr_t>(rule);
                session.remove_activation(hash);
                if (has_listeners) {
                    for (auto& listener : listeners) {
                        listener->on_activation_retracted(rule->name, token.get_facts());
                    }
                }
            }
            session.logical_retract(wme_ptr);
        } else if (token.type == PropagationType::ASSERT) {
            auto [it, inserted] = mem.memory.insert(wme_ptr);
            if (inserted) {
                size_t hash = std::hash<TokenWME const*>{}(wme_ptr) ^ reinterpret_cast<uintptr_t>(rule);
                batch_activations.push_back(Activation{rule, token, hash, &this->binding_to_token_idx});
                if (has_listeners) {
                    for (auto& listener : listeners) {
                        listener->on_activation_created(rule->name, token.get_facts());
                    }
                }
            }
        }
    }

    // Batch insert all activations at once
    if (!batch_activations.empty()) {
        if (rule->auto_focus && rule->agenda_group) {
            session.set_focus(*rule->agenda_group);
        }
        session.add_activations_batch(batch_activations);
    }
}

void TerminalNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"TerminalNode (" << id << ")\\nRule: " << rule_name
       << "\", shape=box, style=filled, fillcolor=lightgreen, peripheries=2];";
}
