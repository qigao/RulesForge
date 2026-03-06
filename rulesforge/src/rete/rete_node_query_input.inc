// --- QueryInputNode ---
QueryInputNode::QueryInputNode(std::shared_ptr<QueryTerminalNode> terminal) : ReteNode(NodeKind::QueryInput), terminal_node(terminal) {}

void QueryInputNode::execute(StatefulSession& session, std::vector<Fact*> const& args) {
    if (auto terminal = terminal_node.lock()) {
        terminal->clear_results(session);
    } else {
        return;
    }

    for (auto* arg_fact : args) {
        if (arg_fact) { session._internal_add_fact(arg_fact); }
    }

    TokenWME const* current_wme = session.get_dummy_wme();
    for (auto* arg_fact : args) { current_wme = session.get_or_create_wme(current_wme, arg_fact); }
    Token initial_token{current_wme, PropagationType::ASSERT};
    for (auto* c : children_raw) { c->left_activate(session, initial_token); }

    for (auto* arg_fact : args) {
        if (arg_fact) { session._internal_remove_fact(arg_fact->id); }
    }
}

void QueryInputNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"Query Input (" << id << ")\", shape=invhouse, style=filled, fillcolor=yellow];";
}
