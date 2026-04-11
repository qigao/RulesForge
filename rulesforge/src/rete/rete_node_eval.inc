// --- EvalNode ---
EvalNode::EvalNode(std::string expr,
                   std::map<std::string, int> bindings,
                   std::map<std::string, std::string> scalar_binding_fields) :
    ReteNode(NodeKind::Eval),
    expression(std::move(expr)),
    binding_to_token_idx(std::move(bindings)),
    scalar_binding_to_field(std::move(scalar_binding_fields)) {
    if (!expression.empty()) {
        std::string compile_error;
        compiled_expression = rulesforge::ExpressionEvaluator::compile(expression, &compile_error);
        if (!compiled_expression) {
            loge("EvalNode: failed to compile eval expression '{}': {}", expression, compile_error);
            return;
        }

        for (auto const& var_name : compiled_expression->variables()) {
            EvalResolvedVar r{};
            std::string binding_name = var_name;
            size_t dot_pos = var_name.find('.');
            if (dot_pos != std::string::npos) {
                binding_name = var_name.substr(0, dot_pos);
                r.field_name = var_name.substr(dot_pos + 1);
            } else {
                auto field_it = scalar_binding_to_field.find(binding_name);
                if (field_it != scalar_binding_to_field.end()) {
                    r.field_name = field_it->second;
                } else {
                    r.field_name = "this";
                }
            }
            r.use_fact_id = (r.field_name == "this" || r.field_name == "id");

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
    } else if (compiled_expression) {
        auto resolver = [this, &token](std::string const& var_name) -> ConstraintValue {
            auto it = resolved_vars_.find(var_name);
            if (it == resolved_vars_.end()) return 0.0;

            EvalResolvedVar const& r = it->second;
            if (r.token_depth < 0) return 0.0;

            auto fact = token.get_fact_at_depth(r.token_depth);
            if (!fact) return 0.0;

            if (r.use_fact_id) {
                return static_cast<double>(fact->id);
            }
            
            auto val_opt = fact->get_field(r.field_name);
            if (val_opt) return *val_opt;
            return 0.0;
        };
        ConstraintValue cv_result = compiled_expression->evaluate(resolver);
        if (std::holds_alternative<double>(cv_result)) {
            result = std::get<double>(cv_result) != 0.0;
        } else if (std::holds_alternative<int64_t>(cv_result)) {
            result = std::get<int64_t>(cv_result) != 0;
        } else {
            result = false;
        }
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
    os << "\", shape=underline, style=filled, fillcolor=orchid];";
}
