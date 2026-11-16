#include "diagnostics/logging_control.hpp"

#include "runtime/knowledge_base.hpp"
#include "rete/nodes/query.hpp"
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

// --- QueryInputNode ---
QueryInputNode::QueryInputNode(std::shared_ptr<QueryTerminalNode> terminal) : ReteNode(NodeKind::QueryInput), terminal_node(terminal) {}

void QueryInputNode::execute(StatefulSession& session, std::vector<Fact*> const& args) {
    if (auto terminal = terminal_node.lock()) {
        terminal->clear_results(session);
    } else {
        return;
    }

    TokenWME const* current_wme = session.get_dummy_wme();
    for (auto* arg_fact : args) { current_wme = session.get_or_create_wme(current_wme, arg_fact); }
    Token initial_token{current_wme, PropagationType::ASSERT};
    for (auto* c : children_raw) { c->left_activate(session, initial_token); }
}

void QueryInputNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"Query Input (" << id << ")\", shape=invhouse, style=filled, fillcolor=yellow];";
}

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
