#include "rfl_parser.hpp"
#include "engine/knowledge_base.hpp"
#include "engine/stateful_session.hpp"
#include "test_helpers.hpp"
#include "tinytest.h"

#include <fstream>
#include <iostream>
#include <sstream>

struct CepTestFixture {
    std::shared_ptr<KnowledgeBase> kb;
    std::vector<std::shared_ptr<Fact>> facts_;

    CepTestFixture() {
        std::string const cep_drl = R"(
            package com.example.cep;

            declare LoginAttempt
                @role( event )
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
                retract $e1
                retract $e2
                retract $e3
                insert PotentialFraud { }
            end
        )";
        ParsingResult result;
        kb = build_knowledge_base(cep_drl, result);
        if (!result.success) throw_parse_failure(result);
        if (!kb) { throw std::runtime_error("KnowledgeBase is null"); }
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
        facts_.push_back(event);
        return event;
    }
};

suite("Engine CEP Operators") {
    group("CEP detection") {
        it("detects repeated failed logins") {
            CepTestFixture fixture;
            auto session = fixture.kb->create_session();
            session->add_fact(fixture.create_login_event(1, "Alice", "1.2.3.4", "fail", 1000));
            session->add_fact(fixture.create_login_event(2, "Alice", "1.2.3.4", "fail", 5000));
            session->add_fact(fixture.create_login_event(3, "Alice", "1.2.3.4", "fail", 8000));

            int fired = session->fire_all_rules();
            check(fired == 1);
            check(session->get_fact_count() == 1);
        }

        it("does not fire when events are too far apart") {
            CepTestFixture fixture;
            auto session = fixture.kb->create_session();
            session->add_fact(fixture.create_login_event(10, "Bob", "5.6.7.8", "fail", 100000));
            session->add_fact(fixture.create_login_event(11, "Bob", "5.6.7.8", "fail", 105000));
            session->add_fact(fixture.create_login_event(12, "Bob", "5.6.7.8", "fail", 111000));

            int fired = session->fire_all_rules();
            check(fired == 0);
            check(session->get_fact_count() == 3);
        }

        it("handles events out of order") {
            CepTestFixture fixture;
            auto session = fixture.kb->create_session();
            session->add_fact(fixture.create_login_event(20, "Carl", "9.0.0.1", "fail", 8000));
            session->add_fact(fixture.create_login_event(21, "Carl", "9.0.0.1", "fail", 5000));
            session->add_fact(fixture.create_login_event(22, "Carl", "9.0.0.1", "fail", 1000));

            int fired = session->fire_all_rules();
            check(fired == 1);
            check(session->get_fact_count() == 1);
        }
    }
}
