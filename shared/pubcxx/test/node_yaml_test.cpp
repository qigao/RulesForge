#include "pubcxx/node_yaml.hpp"

#include <catch2/catch_all.hpp>
#include <string>
#include <vector>

TEST_CASE("YAML Multi-Endpoint HostNode Converter", "[yaml][host_node][multi-endpoint]") {

    SECTION("1. Successful Decoding of Multi-Endpoint Node") {
        char const* yaml_string = R"(
id: multi-service-1
weight: 200
state: BUSY
endpoints:
  http:
    host: 10.1.1.5
    port: 8080
    path: /api
  ws:
    host: 10.1.1.5
    port: 9090
    auth_token: "ws-secret"
)";
        YAML::Node yaml_node = YAML::Load(yaml_string);
        HostNode host_node = yaml_node.as<HostNode>();

        REQUIRE(host_node.id == "multi-service-1");
        REQUIRE(host_node.weight == 200);
        REQUIRE(host_node.state == HostNode::State::BUSY);

        // Verify the endpoints map
        REQUIRE(host_node.endpoints.size() == 2);

        // Check HTTP endpoint
        REQUIRE(host_node.endpoints.count("http") == 1);
        auto const& http_ep = host_node.endpoints.at("http");
        REQUIRE(http_ep.host == "10.1.1.5");
        REQUIRE(http_ep.port == 8080);
        REQUIRE(http_ep.path == "/api");
        REQUIRE(http_ep.auth_token.empty());

        // Check WebSocket endpoint
        REQUIRE(host_node.endpoints.count("ws") == 1);
        auto const& ws_ep = host_node.endpoints.at("ws");
        REQUIRE(ws_ep.host == "10.1.1.5");
        REQUIRE(ws_ep.port == 9090);
        REQUIRE(ws_ep.path.empty());
        REQUIRE(ws_ep.auth_token == "ws-secret");
    }

    SECTION("2. Successful Encoding of Multi-Endpoint Node") {
        HostNode host_node;
        host_node.id = "encode-multi";
        host_node.endpoints["https"] = {"api.test.com", 443, "/v2"};
        host_node.endpoints["grpc"] = {"grpc.test.com", 50051};

        YAML::Node yaml_node = YAML::convert<HostNode>::encode(host_node);

        REQUIRE(yaml_node["id"].as<std::string>() == "encode-multi");
        REQUIRE(yaml_node["endpoints"].IsMap());
        REQUIRE(yaml_node["endpoints"].size() == 2);

        REQUIRE(yaml_node["endpoints"]["https"]["host"].as<std::string>() == "api.test.com");
        REQUIRE(yaml_node["endpoints"]["https"]["port"].as<uint16_t>() == 443);
        REQUIRE(yaml_node["endpoints"]["grpc"]["host"].as<std::string>() == "grpc.test.com");
    }

    SECTION("3. Decoding Failure") {
        // Missing the entire 'endpoints' map
        REQUIRE_THROWS_AS(YAML::Load("{id: n1}").as<HostNode>(), YAML::BadConversion);
        // 'endpoints' is not a map
        REQUIRE_THROWS_AS(YAML::Load("{id: n1, endpoints: [1,2,3]}").as<HostNode>(), YAML::BadConversion);
        // An endpoint within the map is malformed (missing port)
        REQUIRE_THROWS_AS(YAML::Load("{id: n1, endpoints: {http: {host: h1}}}").as<HostNode>(), YAML::BadConversion);
    }

    SECTION("4. Endpoint::full_url() Test with External Scheme") {
        Endpoint ep;
        ep.host = "example.com";
        ep.port = 8080;
        ep.path = "/test";

        REQUIRE(ep.full_url("http") == "http://example.com:8080/test");
        REQUIRE(ep.full_url("wss") == "wss://example.com:8080/test");
    }
}
