#include "catch2/catch_all.hpp"
#include "drools_rete_defs.hpp"
#include "i_network_callback.hpp"
#include "tms.hpp"

/**
 * @class MockTmsNetwork
 * @brief A mock that correctly implements the INetworkCallback interface
 *        specifically for testing the TruthMaintenanceSystem.
 */
class MockTmsNetwork : public INetworkCallback {
public:
    // The TMS will be constructed with a reference to this mock.
    TruthMaintenanceSystem tms;

    // State tracking for tests
    std::map<int64_t, std::shared_ptr<Fact>> facts;
    std::vector<int64_t> retracted_fact_ids;
    int64_t next_fact_id = 1;

    MockTmsNetwork() : tms(*this) {}

    // --- INetworkCallback Interface Implementation ---

    // Method needed by TMS to find facts to retract
    std::optional<std::shared_ptr<Fact>> get_fact_by_id(int64_t id) override {
        auto it = facts.find(id);
        if (it != facts.end()) { return it->second; }
        return std::nullopt;
    }

    // Method needed by TMS to trigger retractions
    void retract_fact(std::shared_ptr<Fact> fact) override {
        if (facts.count(fact->id)) {
            retracted_fact_ids.push_back(fact->id);
            facts.erase(fact->id);
            // In the real network, retract_fact is the one that notifies the TMS.
            // We simulate that behavior here for a correct, non-recursive test.
            tms.on_fact_retracted(fact.get());
        }
    }

    // --- Unused Interface Methods (for this test file) ---
    // We must provide implementations, but they can be empty.
    void add_fact(std::shared_ptr<Fact> fact) override {}

    void update_fact(std::shared_ptr<Fact> fact, std::function<void(Fact&)> modifier) override {}

    void logical_insert(Token& token, std::shared_ptr<Fact> fact) override {}

    void set_focus(std::string const& group_name) override {}

    std::map<std::string, sol::object> const& get_global_values() const override {
        static std::map<std::string, sol::object> const empty_globals;
        return empty_globals;
    }

    // --- Test Helper Methods ---

    // Helper to create and add a fact to our mock memory
    std::shared_ptr<Fact> make_fact(std::string const& type) {
        auto fact = std::make_shared<Fact>();
        fact->id = next_fact_id++;
        fact->type = type;
        facts[fact->id] = fact;
        return fact;
    }
};

TEST_CASE_METHOD(MockTmsNetwork, "TMS: Add Justification", "[tms]") {
    auto token_wme_a = std::make_shared<TokenWME>();
    auto fact_b = make_fact("B");

    SECTION("Single justification") {
        tms.add_justification(token_wme_a, fact_b);
        // This test primarily ensures the function can be called without error.
        // The real verification comes from the retraction tests.
        SUCCEED("add_justification completed without error.");
    }
}

TEST_CASE_METHOD(MockTmsNetwork, "TMS: Simple Logical Retraction", "[tms]") {
    auto token_wme_a = std::make_shared<TokenWME>();
    auto fact_b = make_fact("B");

    // A supports B
    tms.add_justification(token_wme_a, fact_b);

    // Retract token A
    tms.remove_justifications_by_token(token_wme_a.get());

    // B should have been retracted because it lost its only supporter.
    REQUIRE(retracted_fact_ids.size() == 1);
    CHECK(retracted_fact_ids[0] == fact_b->id);
}

TEST_CASE_METHOD(MockTmsNetwork, "TMS: Retraction with Multiple Supporters", "[tms]") {
    auto token_wme_a = std::make_shared<TokenWME>();
    auto token_wme_x = std::make_shared<TokenWME>();
    auto fact_b = make_fact("B");

    // A supports B
    tms.add_justification(token_wme_a, fact_b);
    // X also supports B
    tms.add_justification(token_wme_x, fact_b);

    // Retract token A
    tms.remove_justifications_by_token(token_wme_a.get());

    // B should NOT be retracted because it is still supported by X.
    REQUIRE(retracted_fact_ids.empty());

    // Now, retract token X
    tms.remove_justifications_by_token(token_wme_x.get());

    // NOW B should be retracted.
    REQUIRE(retracted_fact_ids.size() == 1);
    CHECK(retracted_fact_ids[0] == fact_b->id);
}

TEST_CASE_METHOD(MockTmsNetwork, "TMS: Recursive Logical Retraction", "[tms]") {
    // Create the token WMEs and Facts for the chain: A -> B -> C
    auto token_wme_a = std::make_shared<TokenWME>();
    auto fact_b = make_fact("B");

    // The token for fact B needs to contain fact B.
    auto token_wme_b = std::make_shared<TokenWME>();
    token_wme_b->fact = fact_b;
    auto fact_c = make_fact("C");

    // Setup justifications: A supports B, B supports C
    tms.add_justification(token_wme_a, fact_b);
    tms.add_justification(token_wme_b, fact_c);

    // Retract the root token A. This should trigger the retraction of B.
    tms.remove_justifications_by_token(token_wme_a.get());

    // Because our mock's `retract_fact` doesn't propagate signals, we must
    // manually simulate the consequence: the retraction of token B.
    tms.remove_justifications_by_token(token_wme_b.get());

    // The retraction of B should trigger the retraction of C.
    REQUIRE(retracted_fact_ids.size() == 2);
    // Check that both B and C were retracted. Order might vary.
    CHECK(std::find(retracted_fact_ids.begin(), retracted_fact_ids.end(), fact_b->id) != retracted_fact_ids.end());
    CHECK(std::find(retracted_fact_ids.begin(), retracted_fact_ids.end(), fact_c->id) != retracted_fact_ids.end());
}

TEST_CASE_METHOD(MockTmsNetwork, "TMS: Manual Fact Retraction Cleanup", "[tms]") {
    auto token_wme_a = std::make_shared<TokenWME>();
    auto fact_b = make_fact("B");

    // A supports B
    tms.add_justification(token_wme_a, fact_b);

    // User manually retracts B. We call the hook directly.
    tms.on_fact_retracted(fact_b.get());

    // Now, if we were to later retract token A, it should not try to retract B again,
    // as the justification links have been cleaned up.
    tms.remove_justifications_by_token(token_wme_a.get());

    // No new retractions should have been triggered by the TMS.
    REQUIRE(retracted_fact_ids.empty());
}

TEST_CASE_METHOD(MockTmsNetwork, "TMS: Clear", "[tms]") {
    auto token_wme_a = std::make_shared<TokenWME>();
    auto fact_b = make_fact("B");
    tms.add_justification(token_wme_a, fact_b);

    tms.clear();

    // After clearing, retracting the token should do nothing.
    tms.remove_justifications_by_token(token_wme_a.get());

    REQUIRE(retracted_fact_ids.empty());
}
