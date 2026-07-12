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
                    &runtime_predicates_)) {
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
