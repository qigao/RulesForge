#include "core/fact.hpp"
#include "engine/knowledge_base.hpp"
#include "engine/query_result.hpp"
#include "engine/stateful_session.hpp"
#include "rfl_parser.hpp"
#include "test_helpers.hpp"
#include "tinytest.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace rulesforge;

namespace {

struct ConsolidatedAlert {
  std::string account_id;
  std::string risk_level;
  std::string action;
  int severity = 0;
  int64_t score = 0;
};

int severity_rank(std::string const &risk) {
  if (risk == "Critical")
    return 5;
  if (risk == "Very High")
    return 4;
  if (risk == "High")
    return 3;
  if (risk == "Medium")
    return 2;
  if (risk == "Low")
    return 1;
  return 0;
}

std::shared_ptr<Fact> make_profile(char const *account_id, char const *account_type,
                                   double avg_monthly_spend, double avg_transaction_amount,
                                   char const *primary_country, double latitude, double longitude,
                                   int64_t last_transaction_time) {
  auto fact = std::make_shared<Fact>();
  fact->type = "com.bank.fraud.AccountProfile";
  fact->fields["accountId"] = std::string(account_id);
  fact->fields["accountType"] = std::string(account_type);
  fact->fields["avgMonthlySpend"] = avg_monthly_spend;
  fact->fields["avgTransactionAmount"] = avg_transaction_amount;
  fact->fields["primaryCountry"] = std::string(primary_country);
  fact->fields["lastKnownLatitude"] = latitude;
  fact->fields["lastKnownLongitude"] = longitude;
  fact->fields["lastTransactionTime"] = last_transaction_time;
  return fact;
}

std::shared_ptr<Fact> make_country(char const *country_code) {
  auto fact = std::make_shared<Fact>();
  fact->type = "com.bank.fraud.HighRiskCountry";
  fact->fields["countryCode"] = std::string(country_code);
  fact->fields["riskLevel"] = std::string("high");
  return fact;
}

std::shared_ptr<Fact> make_txn(char const *id, char const *account_id, double amount,
                               char const *country, char const *channel, int64_t timestamp,
                               double latitude = std::numeric_limits<double>::quiet_NaN(),
                               double longitude = std::numeric_limits<double>::quiet_NaN()) {
  auto fact = std::make_shared<Fact>();
  fact->type = "com.bank.fraud.Transaction";
  fact->fields["transactionId"] = std::string(id);
  fact->fields["accountId"] = std::string(account_id);
  fact->fields["amount"] = amount;
  fact->fields["merchantCountry"] = std::string(country);
  fact->fields["channel"] = std::string(channel);
  fact->fields["timestamp"] = timestamp;
  if (!std::isnan(latitude)) {
    fact->fields["latitude"] = latitude;
  }
  if (!std::isnan(longitude)) {
    fact->fields["longitude"] = longitude;
  }
  return fact;
}

std::unordered_map<std::string, ConsolidatedAlert> consolidate_alerts(QueryResult const &alerts,
                                                                      QueryResult const &signals) {
  std::unordered_map<std::string, int64_t> score_by_tx;
  for (auto const row : signals) {
    auto tx = row.getFieldAs<std::string>("signal", "transactionId");
    auto score = row.getFieldAs<int64_t>("signal", "score");
    if (tx && score) {
      score_by_tx[*tx] += *score;
    }
  }

  std::unordered_map<std::string, ConsolidatedAlert> best_by_tx;
  for (auto const row : alerts) {
    auto tx = row.getFieldAs<std::string>("alert", "transactionId");
    auto account = row.getFieldAs<std::string>("alert", "accountId");
    auto risk = row.getFieldAs<std::string>("alert", "riskLevel");
    auto action = row.getFieldAs<std::string>("alert", "action");
    if (!tx || !risk) {
      continue;
    }

    int severity = severity_rank(*risk);
    auto &slot = best_by_tx[*tx];
    if (severity >= slot.severity) {
      slot.account_id = account.value_or("");
      slot.risk_level = *risk;
      slot.action = action.value_or("");
      slot.severity = severity;
      slot.score = score_by_tx[*tx];
    }
  }
  return best_by_tx;
}

std::unordered_map<std::string, std::unordered_set<std::string>>
collect_signals(QueryResult const &signals) {
  std::unordered_map<std::string, std::unordered_set<std::string>> signal_types;
  for (auto const row : signals) {
    auto tx = row.getFieldAs<std::string>("signal", "transactionId");
    auto type = row.getFieldAs<std::string>("signal", "signalType");
    if (tx && type) {
      signal_types[*tx].insert(*type);
    }
  }
  return signal_types;
}

bool has_signal(
    std::unordered_map<std::string, std::unordered_set<std::string>> const &signal_types,
    std::string const &tx, char const *type) {
  auto const it = signal_types.find(tx);
  return it != signal_types.end() && it->second.contains(type);
}

} // namespace

suite("Fraud Example") {
  it("keeps the documented fraud example runnable after consolidation") {
    ParsingResult result;
    auto kb = build_knowledge_base(result, RULESFORGE_FRAUD_EXAMPLE_RFL);
    if (!result.success)
      throw_parse_failure(result);

    auto session = kb->create_session();
    std::vector<std::shared_ptr<Fact>> owned;

    owned.push_back(
        make_profile("ACC-001", "checking", 2500.0, 85.0, "US", 37.7749, -122.4194, 1704067200000));
    owned.push_back(
        make_profile("ACC-002", "credit", 5000.0, 150.0, "US", 40.7128, -74.0060, 1704063600000));
    owned.push_back(make_profile("ACC-003", "business", 25000.0, 500.0, "US", 34.0522, -118.2437,
                                 1704060000000));
    owned.push_back(make_country("NG"));
    owned.push_back(make_country("RO"));

    owned.push_back(make_txn("TXN-TEST-001", "ACC-001", 1.0, "US", "online", 1704070800000));
    owned.push_back(make_txn("TXN-TEST-002", "ACC-001", 1.0, "US", "online", 1704070810000));
    owned.push_back(make_txn("TXN-TEST-003", "ACC-001", 1.0, "US", "online", 1704070820000));
    owned.push_back(make_txn("TXN-TEST-004", "ACC-001", 1.0, "US", "online", 1704070830000));
    owned.push_back(make_txn("TXN-TEST-005", "ACC-001", 1.0, "US", "online", 1704070840000));
    owned.push_back(make_txn("TXN-TEST-006", "ACC-001", 500.0, "US", "online", 1704070850000));
    owned.push_back(make_txn("TXN-NORM-001", "ACC-001", 75.5, "US", "pos", 1704070800000));
    owned.push_back(
        make_txn("TXN-TRAVEL-001", "ACC-001", 200.0, "GB", "pos", 1704069000000, 51.5074, -0.1278));
    owned.push_back(make_txn("TXN-COUNTRY-001", "ACC-002", 3000.0, "NG", "online", 1704074400000));
    owned.push_back(make_txn("TXN-ATM-001", "ACC-001", 400.0, "US", "atm", 1704070800000));
    owned.push_back(make_txn("TXN-ATM-002", "ACC-001", 400.0, "US", "atm", 1704071100000));
    owned.push_back(make_txn("TXN-ATM-003", "ACC-001", 400.0, "US", "atm", 1704071400000));
    owned.push_back(make_txn("TXN-ATM-004", "ACC-001", 400.0, "US", "atm", 1704071700000));
    owned.push_back(make_txn("TXN-STRUCT-001", "ACC-003", 9500.0, "US", "pos", 1704070800000));
    owned.push_back(make_txn("TXN-STRUCT-002", "ACC-003", 9800.0, "US", "pos", 1704074400000));
    owned.push_back(make_txn("TXN-STRUCT-003", "ACC-003", 9999.0, "US", "pos", 1704078000000));
    owned.push_back(make_txn("TXN-ATO-001", "ACC-001", 2500.0, "RO", "online", 1704070800000,
                             44.4268, 26.1025));
    owned.push_back(make_txn("TXN-ATO-002", "ACC-001", 2500.0, "RO", "online", 1704070920000,
                             44.4268, 26.1025));
    owned.push_back(make_txn("TXN-ATO-003", "ACC-001", 2500.0, "RO", "online", 1704071040000,
                             44.4268, 26.1025));

    for (size_t i = 0; i < 5; ++i) {
      session->add_fact(owned[i]);
    }
    for (size_t i = 5; i < owned.size(); ++i) {
      session->insert_into("transaction-stream", owned[i].get());
    }

    int fired = session->fire_all_rules();
    check(fired > 0);

    auto alerts = session->execute_query("ActiveAlerts");
    check(alerts.success());
    auto signals = session->execute_query("AllSignals");
    check(signals.success());

    auto consolidated = consolidate_alerts(alerts, signals);
    auto signal_types = collect_signals(signals);

    auto atm = consolidated.find("TXN-ATM-004");
    check(atm != consolidated.end());
    check(has_signal(signal_types, "TXN-ATM-004", "ATM_HAMMERING"));
    check(has_signal(signal_types, "TXN-ATM-004", "ROUND_AMOUNT"));
    check(!atm->second.risk_level.empty());
    check(!atm->second.action.empty());

    auto travel = consolidated.find("TXN-TRAVEL-001");
    check(travel != consolidated.end());
    check(has_signal(signal_types, "TXN-TRAVEL-001", "IMPOSSIBLE_TRAVEL"));
    check(has_signal(signal_types, "TXN-TRAVEL-001", "NEW_COUNTRY"));
    check(!travel->second.risk_level.empty());
    check(!travel->second.action.empty());

    auto country = consolidated.find("TXN-COUNTRY-001");
    check(country != consolidated.end());
    check(has_signal(signal_types, "TXN-COUNTRY-001", "HIGH_RISK_COUNTRY"));
    check(has_signal(signal_types, "TXN-COUNTRY-001", "NEW_COUNTRY"));
    check(has_signal(signal_types, "TXN-COUNTRY-001", "LARGE_AMOUNT"));
    check(!country->second.risk_level.empty());
    check(!country->second.action.empty());
    check(country->second.account_id == "ACC-002");
  }
}
