#include "engine/query_engine.hpp"

#include "engine/knowledge_base.hpp"
#include "engine/stateful_session.hpp"
#include "rete/compiled_network.hpp"

QueryEngine::QueryEngine(std::shared_ptr<KnowledgeBase const> kb) : kb_(std::move(kb)) {}

QueryResult QueryEngine::execute(std::string const& query_name,
                                 std::vector<Fact*> const& args,
                                 StatefulSession& session) const {
    auto const& net = kb_->network();
    auto it = net.query_nodes.find(query_name);
    if (it == net.query_nodes.end()) {
        return QueryResult::error("Query '" + query_name + "' not found.");
    }
    auto& terminal_node = it->second;

    auto param_it = net.parameterized_query_inputs.find(query_name);
    if (param_it != net.parameterized_query_inputs.end()) {
        param_it->second->execute(session, args);
    }

    std::vector<std::map<std::string, Fact*>> raw_results;
    for (auto const& [wme, token] : terminal_node->get_results(session)) {
        (void)wme;
        std::map<std::string, Fact*> row;
        auto facts = token.get_facts();
        for (auto const& [binding, depth] : terminal_node->get_bindings()) {
            if (!binding.empty() && depth < facts.size()) {
                row[binding] = facts[depth];
            }
        }
        if (!row.empty()) {
            raw_results.push_back(row);
        }
    }
    return QueryResult(std::move(raw_results), kb_);
}
