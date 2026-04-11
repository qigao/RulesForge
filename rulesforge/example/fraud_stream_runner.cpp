#include "rfl_parser.hpp"

#include "core/fact.hpp"
#include "engine/query_result.hpp"
#include "engine/stateful_session.hpp"

#include <jsoncons/json.hpp>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

std::string read_file(std::string const& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot open file: " + path);
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

ConstraintValue to_constraint_value(jsoncons::json const& value) {
    if (value.is_string()) {
        return value.as<std::string>();
    }
    if (value.is_bool()) {
        return static_cast<int64_t>(value.as<bool>() ? 1 : 0);
    }
    if (value.is_int64()) {
        return value.as<int64_t>();
    }
    if (value.is_uint64()) {
        return static_cast<int64_t>(value.as<uint64_t>());
    }
    if (value.is_double()) {
        return value.as<double>();
    }
    if (value.is_null()) {
        return NilValue{};
    }
    throw std::runtime_error("unsupported JSON value in flat example data");
}

std::shared_ptr<Fact> make_fact(std::string const& type_name, jsoncons::json const& object) {
    if (!object.is_object()) {
        throw std::runtime_error("expected JSON object for fact type " + type_name);
    }

    auto fact = std::make_shared<Fact>();
    fact->type = type_name;
    for (auto const& member : object.object_range()) {
        if (!member.key().empty() && member.key()[0] == '_') {
            continue;
        }
        auto interned = rulesforge::StringInterner::instance().intern(member.key());
        fact->fields[rulesforge::InternedString(interned)] = to_constraint_value(member.value());
    }
    return fact;
}

void add_fact_array(StatefulSession& session,
                    jsoncons::json const& root,
                    char const* array_key,
                    std::string const& type_name,
                    std::vector<std::shared_ptr<Fact>>& owned_facts) {
    auto const it = root.find(array_key);
    if (it == root.object_range().end() || !it->value().is_array()) {
        return;
    }
    for (auto const& item : it->value().array_range()) {
        auto fact = make_fact(type_name, item);
        session.add_fact(fact);
        owned_facts.push_back(std::move(fact));
    }
}

void insert_stream_array(StatefulSession& session,
                         jsoncons::json const& root,
                         char const* array_key,
                         std::string const& type_name,
                         char const* stream_name,
                         std::vector<std::shared_ptr<Fact>>& owned_facts) {
    auto const it = root.find(array_key);
    if (it == root.object_range().end() || !it->value().is_array()) {
        return;
    }
    for (auto const& item : it->value().array_range()) {
        auto fact = make_fact(type_name, item);
        session.insert_into(stream_name, fact.get());
        owned_facts.push_back(std::move(fact));
    }
}

int severity_rank(std::string const& risk) {
    if (risk == "Critical") return 5;
    if (risk == "Very High") return 4;
    if (risk == "High") return 3;
    if (risk == "Medium") return 2;
    if (risk == "Low") return 1;
    return 0;
}

struct ConsolidatedAlert {
    std::string account_id;
    std::string risk_level;
    std::string action;
    int severity = 0;
};

void print_alerts(QueryResult const& alerts, QueryResult const& signals) {
    std::unordered_map<std::string, int64_t> score_by_tx;
    for (auto const row : signals) {
        auto id = row.getFieldAs<std::string>("signal", "transactionId");
        auto score = row.getFieldAs<int64_t>("signal", "score");
        if (id && score) {
            score_by_tx[*id] += *score;
        }
    }

    std::unordered_map<std::string, ConsolidatedAlert> best_by_tx;
    std::vector<std::string> order;
    for (auto const row : alerts) {
        auto id = row.getFieldAs<std::string>("alert", "transactionId");
        auto account = row.getFieldAs<std::string>("alert", "accountId");
        auto risk = row.getFieldAs<std::string>("alert", "riskLevel");
        auto action = row.getFieldAs<std::string>("alert", "action");
        if (!id || !risk) {
            continue;
        }

        int severity = severity_rank(*risk);
        auto it = best_by_tx.find(*id);
        if (it == best_by_tx.end()) {
            order.push_back(*id);
            best_by_tx.emplace(*id, ConsolidatedAlert{
                account ? *account : "<missing>",
                *risk,
                action ? *action : "<missing>",
                severity
            });
            continue;
        }

        if (severity > it->second.severity) {
            it->second.account_id = account ? *account : "<missing>";
            it->second.risk_level = *risk;
            it->second.action = action ? *action : "<missing>";
            it->second.severity = severity;
        }
    }

    std::cout << "alerts=" << best_by_tx.size() << "\n";
    for (auto const& id : order) {
        auto const it = best_by_tx.find(id);
        if (it == best_by_tx.end()) {
            continue;
        }
        auto score_it = score_by_tx.find(id);
        int64_t score = score_it != score_by_tx.end() ? score_it->second : 0;
        std::cout << id << " | "
                  << it->second.account_id << " | "
                  << score << " | "
                  << it->second.risk_level << " | "
                  << it->second.action << "\n";
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: fraud_stream_runner <rules.rfl> <fraud-flat.json>\n";
        return 2;
    }

    try {
        ParsingResult result;
        auto kb = build_knowledge_base(result, std::string(argv[1]));
        if (!kb || !result.success) {
            std::cerr << "rule compilation failed\n";
            for (auto const& err : result.errors) {
                std::cerr << err.to_string() << "\n";
            }
            return 1;
        }

        auto session = kb->create_session();
        auto root = jsoncons::json::parse(read_file(argv[2]));

        std::vector<std::shared_ptr<Fact>> owned_facts;
        add_fact_array(*session, root, "accountProfiles", "com.bank.fraud.AccountProfile", owned_facts);
        add_fact_array(*session, root, "highRiskCountries", "com.bank.fraud.HighRiskCountry", owned_facts);
        insert_stream_array(*session, root, "transactions", "com.bank.fraud.Transaction", "transaction-stream",
                            owned_facts);

        int fired = session->fire_all_rules();
        std::cout << "rules_fired=" << fired << "\n";

        auto alerts = session->execute_query("ActiveAlerts");
        if (!alerts.success()) {
            std::cerr << "query failed: " << alerts.error_message() << "\n";
            return 1;
        }
        auto signals = session->execute_query("AllSignals");
        if (!signals.success()) {
            std::cerr << "query failed: " << signals.error_message() << "\n";
            return 1;
        }
        print_alerts(alerts, signals);
        return 0;
    } catch (std::exception const& ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }
}
