#pragma once
#include "node.h"

#include <map>
#include <string>
#include <yaml-cpp/yaml.h>

// The entire specialization must be inside the YAML namespace.
namespace YAML {

    template <>
    struct convert<Endpoint> {
        static bool decode(const YAML::Node& y_node, Endpoint& rhs) {
            if (!y_node.IsMap() || !y_node["host"] || !y_node["port"]) { return false; }
            rhs.host = y_node["host"].as<std::string>();
            rhs.port = y_node["port"].as<uint16_t>();
            rhs.path = y_node["path"] ? y_node["path"].as<std::string>() : "";
            rhs.auth_token = y_node["auth_token"] ? y_node["auth_token"].as<std::string>() : "";
            return true;
        }

        static Node encode(Endpoint const& rhs) {
            YAML::Node node;
            node["host"] = rhs.host;
            node["port"] = rhs.port;
            if (!rhs.path.empty()) node["path"] = rhs.path;
            if (!rhs.auth_token.empty()) node["auth_token"] = rhs.auth_token;
            return node;
        }
    };

    template <>
    struct convert<phmap::flat_hash_map<std::string, Endpoint>> {
        static bool decode(const YAML::Node& y_node, phmap::flat_hash_map<std::string, Endpoint>& rhs) {
            if (!y_node.IsMap()) { return false; }
            for (YAML::const_iterator it = y_node.begin(); it != y_node.end(); ++it) {
                rhs[it->first.as<std::string>()] = it->second.as<Endpoint>();
            }
            return true;
        }

        static Node encode(phmap::flat_hash_map<std::string, Endpoint> const& rhs) {
            YAML::Node node(YAML::NodeType::Map);
            for (auto const& [key, val] : rhs) { node[key] = val; }
            return node;
        }
    };

    template <>
    struct convert<HostNode> {
        static Node encode(HostNode const& rhs) {
            YAML::Node y_node;
            y_node["id"] = rhs.id;
            y_node["endpoints"] = rhs.endpoints;

            // --- Encode state enum to string ---
            std::string state_str;
            switch (rhs.state) {
                case HostNode::State::INITIALIZING:
                    state_str = "INITIALIZING";
                    break;
                case HostNode::State::ACTIVE:
                    state_str = "ACTIVE";
                    break;
                case HostNode::State::BUSY:
                    state_str = "BUSY";
                    break;
                case HostNode::State::DRAINING:
                    state_str = "DRAINING";
                    break;
                case HostNode::State::INACTIVE:
                    state_str = "INACTIVE";
                    break;
                case HostNode::State::OFFLINE:
                    state_str = "OFFLINE";
                    break;
            }
            y_node["state"] = state_str;

            y_node["weight"] = rhs.weight;
            if (!rhs.version.empty()) y_node["version"] = rhs.version;
            if (!rhs.region.empty()) y_node["region"] = rhs.region;
            if (!rhs.rack_id.empty()) y_node["rack_id"] = rhs.rack_id;

            return y_node;
        }

        static bool decode(const YAML::Node& y_node, HostNode& rhs) {
            if (!y_node.IsMap() || !y_node["id"] || !y_node["endpoints"]) { return false; }

            rhs.id = y_node["id"].as<std::string>();
            rhs.endpoints = y_node["endpoints"].as<phmap::flat_hash_map<std::string, Endpoint>>();

            rhs.weight = y_node["weight"] ? y_node["weight"].as<uint32_t>() : 100;
            rhs.version = y_node["version"] ? y_node["version"].as<std::string>() : "";
            rhs.region = y_node["region"] ? y_node["region"].as<std::string>() : "";
            rhs.rack_id = y_node["rack_id"] ? y_node["rack_id"].as<std::string>() : "";

            if (y_node["state"]) {
                static std::map<std::string, HostNode::State> const state_map = {
                    {"INITIALIZING", HostNode::State::INITIALIZING},
                    {"ACTIVE", HostNode::State::ACTIVE},
                    {"BUSY", HostNode::State::BUSY},
                    {"DRAINING", HostNode::State::DRAINING},
                    {"INACTIVE", HostNode::State::INACTIVE},
                    {"OFFLINE", HostNode::State::OFFLINE}};
                auto it = state_map.find(y_node["state"].as<std::string>());
                if (it == state_map.end()) return false;
                rhs.state = it->second;
            } else {
                rhs.state = HostNode::State::ACTIVE;
            }

            return true;
        }
    };

}   // namespace YAML
