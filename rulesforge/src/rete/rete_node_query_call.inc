// --- QueryCallNode ---
QueryCallNode::QueryCallNode(ParsedQueryCall call,
                             std::map<std::string, int> bindings,
                             std::vector<std::string> output_bindings)
    : ReteNode(NodeKind::QueryCall),
      call_(std::move(call)),
      binding_to_token_idx_(std::move(bindings)),
      output_bindings_(std::move(output_bindings)) {}

std::vector<TokenWME const*> QueryCallNode::project_query_results(StatefulSession& session, Token const& token) const {
    std::vector<TokenWME const*> projected;
    std::vector<Fact*> args;
    args.reserve(call_.arguments.size());
    for (auto const& argument : call_.arguments) {
        auto binding_it = binding_to_token_idx_.find(argument);
        if (binding_it == binding_to_token_idx_.end()) {
            return projected;
        }
        auto* fact = token.get_fact_at_depth(binding_it->second);
        if (fact == nullptr) {
            return projected;
        }
        args.push_back(fact);
    }

    auto result = session.execute_query(call_.query_name, args);
    if (!result.success()) {
        return projected;
    }
    if (output_bindings_.empty()) {
        if (result.size() > 0) {
            projected.push_back(token.wme);
        }
        return projected;
    }
    for (auto row : result) {
        auto* current_wme = token.wme;
        bool complete = true;
        for (auto const& binding : output_bindings_) {
            auto fact_opt = row.get(binding);
            if (!fact_opt || *fact_opt == nullptr) {
                complete = false;
                break;
            }
            current_wme = session.get_or_create_wme(current_wme, *fact_opt);
        }
        if (complete && current_wme != nullptr) {
            projected.push_back(current_wme);
        }
    }
    return projected;
}

void QueryCallNode::left_activate(StatefulSession& session, Token const& token) {
    auto& mem = session.net_mem().query_call[mem_slot];

    if (token.type == PropagationType::RETRACT) {
        mem.left.erase(token.wme);
        auto child_it = mem.parent_to_children.find(token.wme);
        if (child_it != mem.parent_to_children.end()) {
            for (auto const* child_wme : child_it->second) {
                Token retract_token{child_wme, PropagationType::RETRACT};
                for (auto* child : children_raw) {
                    child->left_activate(session, retract_token);
                }
            }
            mem.parent_to_children.erase(child_it);
        }
        return;
    }

    mem.left[token.wme] = token;
    auto projected = project_query_results(session, token);
    auto child_it = mem.parent_to_children.find(token.wme);
    if (child_it != mem.parent_to_children.end()) {
        auto const& previous = child_it->second;
        for (auto const* child_wme : previous) {
            if (std::find(projected.begin(), projected.end(), child_wme) != projected.end()) {
                continue;
            }
            Token retract_token{child_wme, PropagationType::RETRACT};
            for (auto* child : children_raw) {
                child->left_activate(session, retract_token);
            }
        }
        for (auto const* child_wme : projected) {
            if (std::find(previous.begin(), previous.end(), child_wme) != previous.end()) {
                continue;
            }
            Token assert_token{child_wme, PropagationType::ASSERT};
            for (auto* child : children_raw) {
                child->left_activate(session, assert_token);
            }
        }
        if (projected.empty()) {
            mem.parent_to_children.erase(child_it);
        } else {
            child_it->second = std::move(projected);
        }
        return;
    }

    if (!projected.empty()) {
        auto& stored = mem.parent_to_children[token.wme];
        stored = std::move(projected);
        for (auto const* child_wme : stored) {
            Token assert_token{child_wme, PropagationType::ASSERT};
            for (auto* child : children_raw) {
                child->left_activate(session, assert_token);
            }
        }
    }
}

void QueryCallNode::refresh(StatefulSession& session) {
    auto& mem = session.net_mem().query_call[mem_slot];
    std::vector<Token> tokens;
    tokens.reserve(mem.left.size());
    for (auto const& [wme, token] : mem.left) {
        (void)wme;
        tokens.push_back(token);
    }
    for (auto& token : tokens) {
        token.type = PropagationType::ASSERT;
        left_activate(session, token);
    }
}

void QueryCallNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"QueryCallNode (" << id << ")\\n"
       << call_.query_name << "(";
    for (std::size_t index = 0; index < call_.arguments.size(); ++index) {
        if (index != 0) {
            os << ", ";
        }
        os << call_.arguments[index];
    }
    os << ")\", shape=component, style=filled, fillcolor=lightcyan];";
}
