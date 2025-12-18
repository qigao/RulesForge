#include "catch2/catch_test_macros.hpp"
#include "rfl_parser.hpp"
#include "knowledge_base.hpp"
#include "stateful_session.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

struct CepTestFixture {
    std::shared_ptr<KnowledgeBase> kb;

    CepTestFixture() {
        std::string const cep_drl = R"(
            package com.example.cep;

            declare LoginAttempt
                @role( event ) // Mark as event for temporal reasoning
                id: long
                username: String
                ipAddress: String
                status: String
                timestamp: long
            end

            declare PotentialFraud
            end

            rule "Detect Repeated Failed Logins"
            when
                $e1: LoginAttempt(status == "fail")
                $e2: LoginAttempt(
                    status == "fail",
                    username == $e1.username,
                    ipAddress == $e1.ipAddress,
                    timestamp after $e1.timestamp,
                    within 10s of $e1
                )
                $e3: LoginAttempt(
                    status == "fail",
                    username == $e1.username,
                    ipAddress == $e1.ipAddress,
                    timestamp after $e2.timestamp,
                    within 10s of $e1
                )
            then
                rfl.retract($e1);
                rfl.retract($e2);
                rfl.retract($e3);
                rfl.insert({type: "PotentialFraud"});
            end
        )";
        ParsingResult result;
        kb = build_knowledge_base(cep_drl, result);
        if (!result.success) {
            for (auto const& err : result.errors) { FAIL(err.to_string()); }
        }
        REQUIRE(result.success);
        REQUIRE(kb != nullptr);
    }

    std::shared_ptr<Fact> create_login_event(int64_t id, std::string const& user, std::string const& ip,
                                             std::string const& status, int64_t timestamp) {
        auto event = std::make_shared<Fact>();
        event->type = "com.example.cep.LoginAttempt";
        event->fields["id"] = id;
        event->fields["username"] = user;
        event->fields["ipAddress"] = ip;
        event->fields["status"] = status;
        event->fields["timestamp"] = timestamp;
        return event;
    }
};

TEST_CASE_METHOD(CepTestFixture, "Engine: CEP Operators", "[engine][cep]") {

    SECTION("Successful detection") {
        auto session = kb->create_session();
        session->add_fact(create_login_event(1, "Alice", "1.2.3.4", "fail", 1000));
        session->add_fact(create_login_event(2, "Alice", "1.2.3.4", "fail", 5000));
        session->add_fact(create_login_event(3, "Alice", "1.2.3.4", "fail", 8000));

        int fired = session->fire_all_rules();
        CHECK(fired == 1);
        CHECK(session->get_fact_count() == 1);
    }

    SECTION("Events too far apart (violates 'within' constraint)") {
        auto session = kb->create_session();
        session->add_fact(create_login_event(10, "Bob", "5.6.7.8", "fail", 100000));
        session->add_fact(create_login_event(11, "Bob", "5.6.7.8", "fail", 105000));
        session->add_fact(create_login_event(12, "Bob", "5.6.7.8", "fail", 111000));   // 111k - 100k > 10k

        int fired = session->fire_all_rules();
        CHECK(fired == 0);
        CHECK(session->get_fact_count() == 3);
    }

    SECTION("Events out of order (violates 'after' constraint)") {
        auto session = kb->create_session();
        session->add_fact(create_login_event(20, "Carl", "9.0.0.1", "fail", 8000));
        session->add_fact(create_login_event(21, "Carl", "9.0.0.1", "fail", 5000));
        session->add_fact(create_login_event(22, "Carl", "9.0.0.1", "fail", 1000));

        int fired = session->fire_all_rules();
        CHECK(fired == 1);
        CHECK(session->get_fact_count() == 1);
    }
}


