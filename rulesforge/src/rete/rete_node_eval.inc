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
                    runtime_arg.text,
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
        auto evaluated = evaluate_runtime_boolean_expression(
            expression,
            *current_fact,
            &token,
            &binding_to_token_idx,
            &scalar_binding_to_field);
        if (!evaluated) {
            return;
        }
        result = *evaluated;
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
