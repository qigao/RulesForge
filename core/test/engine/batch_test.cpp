#include "rfl_parser.hpp"
#include "runtime/knowledge_base.hpp"
#include "runtime/stateful_session.hpp"
#include "tinytest.hpp"

#include <chrono>
#include <vector>
#include <iostream>

std::unique_ptr<StatefulSession> create_test_session() {
    std::string drl = R"(
        declare Person
            name: String
            age: int
            salary: double
        end
        declare HighEarner
            name: String
        end
        rule "Find High Earners"
        when
            $p : Person(age > 25, salary > 50000.0)
        then
            insert HighEarner { name = $p.name }
        end
    )";

    ParsingResult result;
    auto kb = build_knowledge_base(drl, result);
    if (!result.success || !kb) { throw std::runtime_error("Failed to build knowledge base"); }
    return kb->create_session();
}

std::vector<std::shared_ptr<Fact>> create_test_facts(int count) {
    std::vector<std::shared_ptr<Fact>> facts;
    facts.reserve(count);

    for (int i = 0; i < count; ++i) {
        auto fact = std::make_shared<Fact>();
        fact->type = "Person";
        fact->fields["name"] = "Person" + std::to_string(i);
        fact->fields["age"] = (int64_t)(25 + i % 40);
        fact->fields["salary"] = 55000.0 + (i % 50000);
        facts.push_back(fact);
    }

    return facts;
}

suite("Performance: Batch Operations") {
    group("Batch vs Single Operations") {
        it("handles single fact operations") {
            constexpr int FACT_COUNT = 1000;

            auto session = create_test_session();
            auto facts = create_test_facts(FACT_COUNT);

            auto start = std::chrono::high_resolution_clock::now();

            for (auto& fact : facts) {
                session->add_fact(fact);
            }

            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

            int fired = session->fire_all_rules();

            std::cout << "Single operations: " << FACT_COUNT << " facts in "
                      << duration.count() << " μs (" << fired << " rules fired)" << std::endl;

            check(session->get_fact_count() > FACT_COUNT);
            check(fired > 0);
        }

        it("handles batch operations") {
            constexpr int FACT_COUNT = 1000;

            auto session = create_test_session();
            auto facts = create_test_facts(FACT_COUNT);

            auto start = std::chrono::high_resolution_clock::now();

            session->add_facts(facts);

            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

            int fired = session->fire_all_rules();

            std::cout << "Batch operations: " << FACT_COUNT << " facts in "
                      << duration.count() << " μs (" << fired << " rules fired)" << std::endl;

            check(session->get_fact_count() > FACT_COUNT);
            check(fired > 0);
        }
    }

    group("Large Batch Operations") {
        it("handles large batch") {
            constexpr int LARGE_COUNT = 10000;

            auto session = create_test_session();
            auto facts = create_test_facts(LARGE_COUNT);

            auto start = std::chrono::high_resolution_clock::now();
            session->add_facts(facts);
            auto end = std::chrono::high_resolution_clock::now();

            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

            int fired = session->fire_all_rules();

            std::cout << "Large batch: " << LARGE_COUNT << " facts in "
                      << duration.count() << " ms (" << fired << " rules fired)" << std::endl;

            check(session->get_fact_count() >= LARGE_COUNT);
            check(fired > 0);
        }
    }

    group("Batch Retraction") {
        it("handles batch retraction") {
            constexpr int COUNT = 500;

            auto session = create_test_session();
            auto facts = create_test_facts(COUNT);

            session->add_facts(facts);
            session->fire_all_rules();

            auto initial_count = session->get_fact_count();

            auto start = std::chrono::high_resolution_clock::now();
            session->retract_facts(facts);
            auto end = std::chrono::high_resolution_clock::now();

            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

            std::cout << "Batch retraction: " << COUNT << " facts in "
                      << duration.count() << " μs" << std::endl;

            check(session->get_fact_count() < initial_count);
        }
    }
}

namespace {
std::unique_ptr<StatefulSession> staged_plan_session(double margin) {
    ParsingResult parsed;
    auto kb = build_knowledge_base(R"(
        declare Request end
        declare Plan stage: String margin: double end
        declare Choice feasible: bool end
        rule "Score" salience 900
        when Request() $plan: Plan(stage == "NEW")
        then update $plan { stage = "SCORED" } end
        rule "Reject" salience 600
        when $plan: Plan(stage == "SCORED", margin < 0.0)
        then update $plan { stage = "REJECTED" } end
        rule "Accept" salience 500
        when $plan: Plan(stage == "SCORED")
        then update $plan { stage = "ACCEPTED" } end
        rule "Select" salience 400
        when Request() Plan(stage == "ACCEPTED") not Choice()
        then insert Choice { feasible = true } end
        rule "Escalate" salience 400
        when Request() not Plan(stage == "ACCEPTED") not Choice()
        then insert Choice { feasible = false } end
        query "Choices" $row: Choice() end
        query "Accepted" $row: Plan(stage == "ACCEPTED") end
    )",parsed);
    if (!parsed.success || !kb) throw std::runtime_error("Cannot compile staged plan regression");
    auto session = kb->create_session();
    auto request = std::make_shared<Fact>(); request->type = "Request";
    auto plan = std::make_shared<Fact>(); plan->type = "Plan";
    plan->fields["stage"] = std::string("NEW"); plan->fields["margin"] = margin;
    session->add_fact(request); session->add_fact(plan);
    return session;
}
}

suite("Agenda selection after each RHS commit") {
    it("cancels an accepted activation when higher priority qualification rejects its plan") {
        auto session = staged_plan_session(-10);
        session->fire_all_rules(20);
        // An incorrectly detached Accept activation would overwrite REJECTED.
        check_equal(session->execute_query("Accepted").size(),0u);
        auto result = session->execute_query("Choices");
        check_equal(result.size(),1u);
        check_false((*result.begin()).getFieldAs<bool>("row","feasible").value());
    }
    it("runs newly enabled higher priority work before an already queued fallback") {
        auto session = staged_plan_session(10);
        session->fire_all_rules(20);
        auto result = session->execute_query("Choices");
        check_equal(result.size(),1u);
        check_true((*result.begin()).getFieldAs<bool>("row","feasible").value());
    }
    it("does not fire a detached negative match after its blocker was inserted") {
        ParsingResult parsed;
        auto kb = build_knowledge_base(R"(
            declare Request end declare Blocker end declare Wrong end
            rule "Block" salience 900
            when Request() not Blocker() then insert Blocker {} end
            rule "Stale" salience 500
            when Request() not Blocker() then insert Wrong {} end
            query "Wrong" $row: Wrong() end
        )",parsed);
        check_true(parsed.success); check_not_null(kb.get());
        auto session = kb->create_session();
        auto request = std::make_shared<Fact>(); request->type = "Request";
        session->add_fact(request);
        session->fire_all_rules(10);
        check_equal(session->execute_query("Wrong").size(),0u);
    }
}
