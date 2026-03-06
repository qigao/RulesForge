// --- QueryTerminalNode ---
QueryTerminalNode::QueryTerminalNode(std::map<std::string, int> bindings) : ReteNode(NodeKind::QueryTerminal), binding_to_token_idx(std::move(bindings)) {}

void QueryTerminalNode::left_activate(StatefulSession& session, Token const& token) {
    auto& mem = session.net_mem().query_terminal[mem_slot];
    auto const* wme = token.wme;
    if (token.type == PropagationType::ASSERT) {
        mem.results[wme] = token;
    } else {
        mem.results.erase(wme);
    }
}

void QueryTerminalNode::clear_results(StatefulSession& session) {
    session.net_mem().query_terminal[mem_slot].results.clear();
}

std::map<TokenWME const*, Token> const& QueryTerminalNode::get_results(StatefulSession& session) {
    return session.net_mem().query_terminal[mem_slot].results;
}

void QueryTerminalNode::set_bindings(std::map<std::string, int> const& bindings) { binding_to_token_idx = bindings; }

void QueryTerminalNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"Query Terminal (" << id << ")\\n";
    os << "Bindings: ";
    bool first = true;
    for (auto const& [name, idx] : binding_to_token_idx) {
        if (!first) os << ", ";
        os << name << "->" << idx;
        first = false;
    }
    os << "\", shape=Mdiamond, style=filled, fillcolor=lemonchiffon, peripheries=2];";
}
