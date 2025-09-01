#include "pubcxx/logger.hpp"

#include "rete/rete_node.hpp"
#include "rete/rete_serializer.hpp"
#include "stateful_session.hpp"

// Helper to add common fields to a node's JSON representation
void add_common_node_fields(nlohmann::json& j, std::shared_ptr<ReteNode const> node) {
    j["id"] = node->id;
    std::vector<int> child_ids;
    for (auto const& weak_child : node->get_children()) {
        if (auto child = weak_child.lock()) { child_ids.push_back(child->id); }
    }
    j["children"] = child_ids;
}

// Non-const constructor
ReteSerializer::ReteSerializer(StatefulSession& session) : session_(&session), const_session_(&session) {}

// Const constructor
ReteSerializer::ReteSerializer(StatefulSession const& session) : session_(nullptr), const_session_(&session) {}

std::string ReteSerializer::serialize() const {
    LOG_DEBUG("ReteSerializer::serialize starting.");
    nlohmann::json j;
    j["next_node_id"] = const_session_->next_node_id_;

    nlohmann::json nodes_json = nlohmann::json::array();
    for (auto const& node : const_session_->all_nodes_) { nodes_json.push_back(node_to_json(node)); }
    j["nodes"] = nodes_json;

    nlohmann::json alpha_entries_json = nlohmann::json::object();
    for (auto const& pair : const_session_->alpha_entry_points_) { alpha_entries_json[pair.first] = pair.second->id; }
    j["alpha_entry_points"] = alpha_entries_json;

    nlohmann::json query_nodes_json = nlohmann::json::object();
    for (auto const& pair : const_session_->query_nodes_) { query_nodes_json[pair.first] = pair.second->id; }
    j["query_nodes"] = query_nodes_json;

    nlohmann::json param_queries_json = nlohmann::json::object();
    for (auto const& pair : const_session_->parameterized_query_inputs_) {
        param_queries_json[pair.first] = pair.second->id;
    }
    j["parameterized_query_inputs"] = param_queries_json;

    nlohmann::json agenda_json = nlohmann::json::array();
    for (auto const& pair : const_session_->agenda_map_) {
        nlohmann::json activation_json;
        activation_json["hash"] = pair.first;
        activation_json["rule_name"] = pair.second.rule->name;
        // Note: Tokens are not serialized as they are runtime state.
        // The activation will be re-created with a dummy token upon deserialization.
        agenda_json.push_back(activation_json);
    }
    j["agenda"] = agenda_json;

    LOG_DEBUG("ReteSerializer::serialize finished.");
    return j.dump(4);
}

void ReteSerializer::deserialize(std::string const& json_data) {
    if (!session_) { throw std::runtime_error("Deserialization requires a non-const session."); }
    LOG_DEBUG("ReteSerializer::deserialize starting.");
    auto j = nlohmann::json::parse(json_data);
    session_->next_node_id_ = j.at("next_node_id").get<int>();

    // Clear old state before loading
    session_->all_nodes_.clear();
    session_->alpha_entry_points_.clear();
    session_->query_nodes_.clear();
    session_->parameterized_query_inputs_.clear();

    std::map<int, std::shared_ptr<ReteNode>> node_map;
    // First pass: Create all nodes from their type and deserialize their specific data
    LOG_DEBUG("  -> First pass: Creating nodes.");
    for (auto const& node_json : j.at("nodes")) {
        std::string type = node_json.at("type").get<std::string>();
        std::shared_ptr<ReteNode> new_node;
        LOG_DEBUG("    -> Deserializing node of type: {}", type);

        if (type == "AlphaNode") {
            auto p = std::make_shared<AlphaNode>();
            from_json(node_json.at("data"), *p);
            new_node = p;
        } else if (type == "EntryPointNode") {
            new_node = std::make_shared<EntryPointNode>();
        } else if (type == "HashedJoinNode") {
            auto p = std::make_shared<HashedJoinNode>(std::vector<ParsedConstraint>{}, std::map<std::string, int>{},
                                                      std::pair<std::string, int>{"", 0}, "");
            from_json(node_json.at("data"), *p);
            new_node = p;
        } else if (type == "CrossProductJoinNode") {
            auto p =
                std::make_shared<CrossProductJoinNode>(std::vector<ParsedConstraint>{}, std::map<std::string, int>{});
            from_json(node_json.at("data"), *p);
            new_node = p;
        } else if (type == "NotNode") {
            auto p = std::make_shared<NotNode>();
            from_json(node_json.at("data"), *p);
            new_node = p;
        } else if (type == "ExistsNode") {
            auto p = std::make_shared<ExistsNode>();
            from_json(node_json.at("data"), *p);
            new_node = p;
        } else if (type == "AccumulateNode") {
            auto p = std::make_shared<AccumulateNode>();
            from_json(node_json.at("data"), *p);
            new_node = p;
        } else if (type == "UnnestNode") {
            auto p = std::make_shared<UnnestNode>();
            from_json(node_json.at("data"), *p);
            new_node = p;
        } else if (type == "EvalNode") {
            auto p = std::make_shared<EvalNode>();
            from_json(node_json.at("data"), *p);
            new_node = p;
        } else if (type == "TerminalNode") {
            auto p = std::make_shared<TerminalNode>();
            from_json(node_json.at("data"), *p);
            new_node = p;
        } else if (type == "QueryTerminalNode") {
            auto p = std::make_shared<QueryTerminalNode>();
            from_json(node_json.at("data"), *p);
            new_node = p;
        } else if (type == "QueryInputNode") {
            auto p = std::make_shared<QueryInputNode>();
            from_json(node_json.at("data"), *p);
            new_node = p;
        } else {
            throw std::runtime_error("Unknown node type for deserialization: " + type);
        }

        new_node->id = node_json.at("id").get<int>();
        session_->all_nodes_.push_back(new_node);
        node_map[new_node->id] = new_node;
    }

    // Second pass: Relink children and other special pointers
    LOG_DEBUG("  -> Second pass: Relinking nodes.");
    for (auto const& node_json : j.at("nodes")) {
        int id = node_json.at("id").get<int>();
        auto node = node_map.at(id);

        // Relink children
        for (int child_id : node_json.at("children")) { node->add_child(node_map.at(child_id)); }

        // Relink special node pointers
        if (auto p = std::dynamic_pointer_cast<AccumulateNode>(node)) {
            p->accumulator_prototype = session_->kb_->get_accumulator_registry().get_prototype(p->info.function);
        } else if (auto p = std::dynamic_pointer_cast<QueryInputNode>(node)) {
            int terminal_id = node_json.at("data")["terminal_id"].get<int>();
            if (terminal_id != -1) {
                p->terminal_node = std::dynamic_pointer_cast<QueryTerminalNode>(node_map.at(terminal_id));
            }
        }
    }

    // Relink top-level entry points
    LOG_DEBUG("  -> Relinking entry points.");
    for (auto& [key, val_id] : j.at("alpha_entry_points").items()) {
        session_->alpha_entry_points_[key] = node_map.at(val_id.get<int>());
    }
    for (auto& [key, val_id] : j.at("query_nodes").items()) {
        session_->query_nodes_[key] = std::dynamic_pointer_cast<QueryTerminalNode>(node_map.at(val_id.get<int>()));
    }
    for (auto& [key, val_id] : j.at("parameterized_query_inputs").items()) {
        session_->parameterized_query_inputs_[key] =
            std::dynamic_pointer_cast<QueryInputNode>(node_map.at(val_id.get<int>()));
    }

    // Deserialize the agenda
    if (j.contains("agenda")) {
        LOG_DEBUG("  -> Deserializing agenda.");
        for (auto const& activation_json : j.at("agenda")) {
            size_t hash = activation_json.at("hash").get<size_t>();
            std::string rule_name = activation_json.at("rule_name").get<std::string>();
            auto const* rule = session_->kb_->find_rule_by_name(rule_name);
            if (rule) {
                // Re-create activation with a dummy token. The real token is runtime-dependent.
                auto dummy_token = std::make_shared<Token>(session_->get_dummy_wme(), PropagationType::ASSERT);
                Activation act{rule, dummy_token, hash, {}};
                session_->add_activation(act);
            } else {
                LOG_WARN("Could not find rule '{}' during agenda deserialization.", rule_name);
            }
        }
    }
    LOG_DEBUG("ReteSerializer::deserialize finished.");
}

nlohmann::json ReteSerializer::node_to_json(std::shared_ptr<ReteNode const> node) const {
    nlohmann::json j;
    add_common_node_fields(j, node);

    if (auto p = std::dynamic_pointer_cast<AlphaNode const>(node)) {
        j["type"] = "AlphaNode";
        j["data"] = *p;
    } else if (auto p = std::dynamic_pointer_cast<EntryPointNode const>(node)) {
        j["type"] = "EntryPointNode";
        j["data"] = *p;
    } else if (auto p = std::dynamic_pointer_cast<HashedJoinNode const>(node)) {
        j["type"] = "HashedJoinNode";
        j["data"] = *p;
    } else if (auto p = std::dynamic_pointer_cast<CrossProductJoinNode const>(node)) {
        j["type"] = "CrossProductJoinNode";
        j["data"] = *p;
    } else if (auto p = std::dynamic_pointer_cast<NotNode const>(node)) {
        j["type"] = "NotNode";
        j["data"] = *p;
    } else if (auto p = std::dynamic_pointer_cast<ExistsNode const>(node)) {
        j["type"] = "ExistsNode";
        j["data"] = *p;
    } else if (auto p = std::dynamic_pointer_cast<AccumulateNode const>(node)) {
        j["type"] = "AccumulateNode";
        j["data"] = *p;
    } else if (auto p = std::dynamic_pointer_cast<UnnestNode const>(node)) {
        j["type"] = "UnnestNode";
        j["data"] = *p;
    } else if (auto p = std::dynamic_pointer_cast<EvalNode const>(node)) {
        j["type"] = "EvalNode";
        j["data"] = *p;
    } else if (auto p = std::dynamic_pointer_cast<TerminalNode const>(node)) {
        j["type"] = "TerminalNode";
        j["data"] = *p;
    } else if (auto p = std::dynamic_pointer_cast<QueryTerminalNode const>(node)) {
        j["type"] = "QueryTerminalNode";
        j["data"] = *p;
    } else if (auto p = std::dynamic_pointer_cast<QueryInputNode const>(node)) {
        j["type"] = "QueryInputNode";
        j["data"] = *p;
    } else {
        throw std::runtime_error("Unknown ReteNode type for serialization.");
    }
    return j;
}
