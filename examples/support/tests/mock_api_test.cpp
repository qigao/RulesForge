#include "runtime.hpp"
#include <tinytest.hpp>
#include <limits>

using namespace scenario_support;
namespace {
Decision decision(std::string_view id, double metric = 1) {
    return {Text(id), Text("ACCEPT"), Text("fixture"), Text("target"), metric};
}
}
suite("Scenario mock API commit and delivery protocol") {
    it("atomically rejects conflicting IDs within a batch") {
        MockDecisionApi repository;
        std::array input{decision("one"), decision("one", 2)};
        check_throws_with(repository.commit(input), "Conflicting replay");
        check_equal(repository.size(), std::size_t{0});
    }
    it("replays identical decisions without duplicating delivery") {
        MockDecisionApi repository;
        MockActionApi actions;
        std::array input{decision("one")};
        repository.commit(input);
        repository.deliver(actions);
        repository.commit(input);
        repository.deliver(actions);
        check_equal(repository.size(), std::size_t{1});
        check_equal(actions.size(), std::size_t{1});
        check_true(repository.rows()[0].delivered);
    }
    it("requires exact metric replay and treats signed zero as the same value") {
        MockDecisionApi repository;
        repository.commit(std::array{decision("one", 1.0)});
        check_throws_with(repository.commit(std::array{
            decision("one", std::nextafter(1.0, 2.0))}), "Conflicting replay");
        check_equal(repository.get("one").metric, 1.0);
        repository.commit(std::array{decision("zero", 0.0)});
        repository.commit(std::array{decision("zero", -0.0)});
        check_equal(repository.size(), std::size_t{2});
    }
    it("leaves a committed row pending after failed delivery until an explicit call") {
        MockDecisionApi repository;
        MockActionApi actions;
        repository.commit(std::array{decision("one")});
        actions.fail_send = true;
        check_throws_with(repository.deliver(actions), "Mock action API failed");
        check_false(repository.rows()[0].delivered);
        check_equal(actions.size(), std::size_t{0});
        actions.fail_send = false;
        repository.deliver(actions);
        check_true(repository.rows()[0].delivered);
        check_equal(actions.size(), std::size_t{1});
    }
    it("preserves the entire original table on capacity overflow") {
        MockDecisionApi repository;
        std::array<Decision, max_decisions> full;
        for (std::size_t i = 0; i < full.size(); ++i) {
            std::array<char, 16> key{};
            std::snprintf(key.data(), key.size(), "decision-%zu", i);
            full[i] = decision(key.data());
        }
        repository.commit(full);
        std::array excess{decision("extra"), decision("decision-0", 2)};
        check_throws_with(repository.commit(excess), "capacity exceeded");
        check_equal(repository.size(), max_decisions);
        check_equal(repository.get("decision-0").metric, 1.0);
        repository.commit(std::array{decision("decision-0")});
    }
    it("fails persistence before changing state or delivering effects") {
        MockDecisionApi repository;
        MockActionApi actions;
        repository.fail_commit = true;
        check_throws_with(repository.commit(std::array{decision("one")}), "Mock persistence API failed");
        check_equal(repository.size(), std::size_t{0});
        check_equal(actions.size(), std::size_t{0});
    }
    it("rejects non-finite metrics and oversized text") {
        MockDecisionApi repository;
        check_throws_as(repository.commit(std::array{decision("one", std::numeric_limits<double>::infinity())}),
                        std::invalid_argument);
        std::array<char, text_capacity> text;
        text.fill('x');
        check_throws_as(Text(std::string_view(text.data(), text.size())), std::length_error);
        check_equal(repository.size(), std::size_t{0});
    }
    it("propagates input API failures") {
        std::array input{FactInput{"Type", "{}"}};
        MockInputApi source(input);
        source.fail_fetch = true;
        check_throws_with(source.fetch(), "Mock input API failed");
    }
    it("bounds input records before the engine acquires any facts") {
        std::array<FactInput, max_input_records + 1> records;
        records.fill({"Type", "{}"});
        check_nothrow(MockInputApi(std::span(records).first(max_input_records)));
        check_throws_as(MockInputApi(records), std::length_error);
    }
    it("bounds accepted actions and allows identical replay at capacity") {
        MockActionApi actions;
        for (std::size_t index = 0; index < max_decisions; ++index) {
            std::array<char, 16> key{};
            std::snprintf(key.data(), key.size(), "action-%zu", index);
            actions.send(decision(key.data()));
        }
        check_nothrow(actions.send(decision("action-0")));
        check_throws_as(actions.send(decision("overflow")), std::length_error);
        check_equal(actions.size(), max_decisions);
    }
}
