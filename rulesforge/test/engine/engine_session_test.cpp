#include "rfl_parser.hpp"
#include "engine/knowledge_base.hpp"
#include "engine/stateful_session.hpp"
#include "core/exceptions.hpp"
#include "test_helpers.hpp"
#include "tinytest.hpp"

struct TestFixture {
    std::unique_ptr<StatefulSession> session;

    void build_session(std::string const& drl) {
        ParsingResult result;
        auto kb = build_knowledge_base(drl, result);
        if (!result.success) throw_parse_failure(result);
        if (!kb) { throw std::runtime_error("KnowledgeBase is null"); }
        session = kb->create_session();
        if (!session) { throw std::runtime_error("Session is null"); }
    }
};

static std::shared_ptr<KnowledgeBase> create_test_kb() {
    std::string drl = R"(
        package com.example.testing;

        declare Person
            name: String
            age: int
        end
        declare Adult
            name: String
        end
        declare NameParam
            name: String
        end

        rule "Find Adults"
        when
            $p : Person(age >= 18)
        then
            insert Adult { name = $p.name }
        end

        query "findAdults"(NameParam $param)
            $a: Adult(name == $param.name)
        end

        query "allAdults"
            $a: Adult()
        end
    )";

    ParsingResult result;
    auto kb = build_knowledge_base(drl, result);
    if (!result.success) throw_parse_failure(result);
    if (!kb) { throw std::runtime_error("KnowledgeBase is null"); }
    return kb;
}

struct SessionTestFixture {
    std::vector<std::shared_ptr<Fact>> facts_;

    std::shared_ptr<Fact> make_person(std::string const& name, int age) {
        auto fact = std::make_shared<Fact>();
        fact->type = "com.example.testing.Person";
        fact->fields["name"] = name;
        fact->fields["age"] = static_cast<int64_t>(age);
        facts_.push_back(fact);
        return fact;
    }
};

struct ReentrantFireListener final : IEngineListener {
    StatefulSession& session;
    bool fail_fast;
    bool attempted = false;
    bool rejected = false;

    explicit ReentrantFireListener(StatefulSession& s, bool strict)
        : session(s), fail_fast(strict) {}

    void before_rule_fired(std::string const&) override {
        if (attempted) return;
        attempted = true;
        try {
            if (fail_fast) session.fire_all_rules_fail_fast();
            else session.fire_all_rules();
        } catch (std::logic_error const&) {
            rejected = true;
        }
    }
};

struct ResetWasAccepted {};

struct ResetDuringFireListener final : IEngineListener {
    StatefulSession& session;
    bool attempted = false;
    bool rejected = false;

    explicit ResetDuringFireListener(StatefulSession& s) : session(s) {}

    void before_rule_fired(std::string const&) override {
        if (attempted) return;
        attempted = true;
        try {
            session.reset();
        } catch (std::logic_error const&) {
            rejected = true;
            return;
        }
        throw ResetWasAccepted{};
    }
};

struct ReentrantAfterFireListener final : IEngineListener {
    StatefulSession& session;
    bool attempted = false;
    bool rejected = false;

    explicit ReentrantAfterFireListener(StatefulSession& s) : session(s) {}

    void after_rule_fired(std::string const&) override {
        if (attempted) return;
        attempted = true;
        try {
            session.fire_all_rules();
        } catch (std::logic_error const&) {
            rejected = true;
        }
    }
};

struct OtherSessionFireListener final : IEngineListener {
    StatefulSession& session;
    bool attempted = false;
    int fired = -1;

    explicit OtherSessionFireListener(StatefulSession& s) : session(s) {}

    void before_rule_fired(std::string const&) override {
        if (attempted) return;
        attempted = true;
        fired = session.fire_all_rules();
    }
};

struct ThrowOnceBeforeFireListener final : IEngineListener {
    bool attempted = false;

    void before_rule_fired(std::string const&) override {
        if (attempted) return;
        attempted = true;
        throw std::runtime_error("listener failure");
    }
};

suite("Engine Session") {
    group("Simple Rule Fire") {
        it("fires rule and inserts fact") {
            TestFixture fixture;
            fixture.build_session(R"(
                package com.example.testing;
                declare Person
                    name: String
                    age: int
                end
                declare Adult
                    name: String
                end
                rule "Find Adults"
                when
                    $p : Person(age >= 18)
                then
                    insert Adult { name = $p.name }
                end
            )");

            auto person = std::make_shared<Fact>();
            person->type = "com.example.testing.Person";
            person->fields["name"] = "John";
            person->fields["age"] = (int64_t)25;

            fixture.session->add_fact(person);
            int fired = fixture.session->fire_all_rules();

            check(fired == 1);
            check(fixture.session->get_fact_count() == 2);
        }
    }

    group("Rule Firing and Queries") {
        it("rule firing inserts a new fact") {
            SessionTestFixture fixture;
            auto kb = create_test_kb();
            std::unique_ptr<StatefulSession> session = kb->create_session();
            check(session != nullptr);

            check(session->get_fact_count() == 0);

            session->add_fact(fixture.make_person("John", 30));
            session->add_fact(fixture.make_person("Amy", 15));
            check(session->get_fact_count() == 2);

            int fired_count = session->fire_all_rules();
            check(fired_count == 1);
            check(session->get_fact_count() == 3);
        }

        it("querying finds facts inserted by rules") {
            SessionTestFixture fixture;
            auto kb = create_test_kb();
            std::unique_ptr<StatefulSession> session = kb->create_session();
            check(session != nullptr);

            session->add_fact(fixture.make_person("John", 30));
            session->fire_all_rules();
            check(session->get_fact_count() == 2);

            QueryResult all_adults = session->execute_query("allAdults");
            check(all_adults.size() == 1);
            auto inserted_adult_name = all_adults.single().getFieldAs<std::string>("$a", "name");
            check(inserted_adult_name.has_value());
            check(inserted_adult_name.value() == "John");

            auto query_arg_fact = std::make_shared<Fact>();
            query_arg_fact->type = "com.example.testing.NameParam";
            query_arg_fact->fields["name"] = "John";

            QueryResult query_results = session->execute_query("findAdults", {query_arg_fact.get()});

            check(query_results.size() == 1);

            QueryResultRow row = query_results.single();

            std::optional<std::string> adult_name = row.getFieldAs<std::string>("$a", "name");

            check(adult_name.has_value());
            check(adult_name.value() == "John");
        }

        it("parameterized packaged query finds manually inserted facts") {
            SessionTestFixture fixture;
            auto kb = create_test_kb();
            std::unique_ptr<StatefulSession> session = kb->create_session();
            check(session != nullptr);

            auto adult = std::make_shared<Fact>();
            adult->type = "com.example.testing.Adult";
            adult->fields["name"] = "John";
            session->add_fact(adult);

            auto query_arg_fact = std::make_shared<Fact>();
            query_arg_fact->type = "com.example.testing.NameParam";
            query_arg_fact->fields["name"] = "John";

            QueryResult query_results = session->execute_query("findAdults", {query_arg_fact.get()});
            check(query_results.size() == 1);
        }
    }

    group("Isolation between sessions") {
        it("sessions are isolated from each other") {
            SessionTestFixture fixture;
            auto kb = create_test_kb();
            std::unique_ptr<StatefulSession> session1 = kb->create_session();
            std::unique_ptr<StatefulSession> session2 = kb->create_session();
            check(session1 != nullptr);
            check(session2 != nullptr);

            session1->add_fact(fixture.make_person("Alice", 40));
            check(session1->get_fact_count() == 1);
            check(session2->get_fact_count() == 0);

            int fired1 = session1->fire_all_rules();
            check(fired1 == 1);
            check(session1->get_fact_count() == 2);
            check(session2->get_fact_count() == 0);

            session2->add_fact(fixture.make_person("Bob", 12));
            check(session1->get_fact_count() == 2);
            check(session2->get_fact_count() == 1);

            int fired2 = session2->fire_all_rules();
            check(fired2 == 0);
            check(session1->get_fact_count() == 2);
            check(session2->get_fact_count() == 1);
        }
    }

    group("Execution admission") {
        it("rejects same-session nested rule execution") {
            SessionTestFixture fixture;
            auto kb = create_test_kb();
            auto session = kb->create_session();
            auto listener = std::make_shared<ReentrantFireListener>(*session, false);
            session->addListener(listener);
            session->add_fact(fixture.make_person("John", 30));

            check_equal(session->fire_all_rules(), 1);
            check_equal(listener->attempted, true);
            check_equal(listener->rejected, true);
            check_equal(session->get_fact_count(), size_t{2});
        }

        it("rejects same-session nested fail-fast rule execution") {
            SessionTestFixture fixture;
            auto kb = create_test_kb();
            auto session = kb->create_session();
            auto listener = std::make_shared<ReentrantFireListener>(*session, true);
            session->addListener(listener);
            session->add_fact(fixture.make_person("John", 30));

            check_equal(session->fire_all_rules(), 1);
            check_equal(listener->attempted, true);
            check_equal(listener->rejected, true);
            check_equal(session->get_fact_count(), size_t{2});
        }

        it("rejects reset during rule execution") {
            SessionTestFixture fixture;
            auto kb = create_test_kb();
            auto session = kb->create_session();
            auto listener = std::make_shared<ResetDuringFireListener>(*session);
            session->addListener(listener);
            session->add_fact(fixture.make_person("John", 30));

            int fired = -1;
            try {
                fired = session->fire_all_rules();
            } catch (ResetWasAccepted const&) {
                // The sentinel safely stops the legacy path before the RHS can
                // use data invalidated by an accepted reset.
            }

            check_equal(listener->attempted, true);
            check_equal(listener->rejected, true);
            check_equal(fired, 1);
            check_equal(session->get_fact_count(), size_t{2});
        }

        it("rejects same-session nested execution from after notification") {
            SessionTestFixture fixture;
            auto kb = create_test_kb();
            auto session = kb->create_session();
            auto listener = std::make_shared<ReentrantAfterFireListener>(*session);
            session->addListener(listener);
            session->add_fact(fixture.make_person("John", 30));

            check_equal(session->fire_all_rules(), 1);
            check_equal(listener->attempted, true);
            check_equal(listener->rejected, true);
            check_equal(session->get_fact_count(), size_t{2});
        }

        it("allows nested execution on a different session") {
            SessionTestFixture fixture;
            auto kb = create_test_kb();
            auto outer_session = kb->create_session();
            auto inner_session = kb->create_session();
            auto listener = std::make_shared<OtherSessionFireListener>(*inner_session);
            outer_session->addListener(listener);
            outer_session->add_fact(fixture.make_person("John", 30));
            inner_session->add_fact(fixture.make_person("Jane", 31));

            check_equal(outer_session->fire_all_rules(), 1);
            check_equal(listener->attempted, true);
            check_equal(listener->fired, 1);
            check_equal(outer_session->get_fact_count(), size_t{2});
            check_equal(inner_session->get_fact_count(), size_t{2});
        }

        it("allows reset and execution after normal return") {
            SessionTestFixture fixture;
            auto kb = create_test_kb();
            auto session = kb->create_session();
            session->add_fact(fixture.make_person("John", 30));
            check_equal(session->fire_all_rules(), 1);

            session->reset();
            session->add_fact(fixture.make_person("Jane", 31));

            check_equal(session->fire_all_rules(), 1);
            check_equal(session->get_fact_count(), size_t{2});
        }

        it("releases execution admission after listener exception") {
            SessionTestFixture fixture;
            auto kb = create_test_kb();
            auto session = kb->create_session();
            auto listener = std::make_shared<ThrowOnceBeforeFireListener>();
            session->addListener(listener);
            session->add_fact(fixture.make_person("John", 30));

            check_throws_as(session->fire_all_rules(), std::runtime_error);
            session->removeListener(listener);
            session->reset();
            session->add_fact(fixture.make_person("Jane", 31));

            check_equal(session->fire_all_rules(), 1);
            check_equal(session->get_fact_count(), size_t{2});
        }

        it("releases fail-fast execution admission after listener exception") {
            SessionTestFixture fixture;
            auto kb = create_test_kb();
            auto session = kb->create_session();
            auto listener = std::make_shared<ThrowOnceBeforeFireListener>();
            session->addListener(listener);
            session->add_fact(fixture.make_person("John", 30));

            check_throws_as(session->fire_all_rules_fail_fast(), std::runtime_error);
            session->removeListener(listener);
            session->reset();
            session->add_fact(fixture.make_person("Jane", 31));

            check_equal(session->fire_all_rules(), 1);
            check_equal(session->get_fact_count(), size_t{2});
        }
    }

    group("Query with no parameters") {
        it("executes query without parameters") {
            std::string drl = R"(
                declare Item name:String end
                query "GetAllItems" $i: Item() end
            )";
            ParsingResult result;
            auto kb = build_knowledge_base(drl, result);
            check(result.success);

            auto session = kb->create_session();

            auto item1 = std::make_shared<Fact>();
            item1->type = "Item";
            item1->fields["name"] = "Anvil";

            auto item2 = std::make_shared<Fact>();
            item2->type = "Item";
            item2->fields["name"] = "Rocket";

            session->add_fact(item1);
            session->add_fact(item2);

            QueryResult query_results = session->execute_query("GetAllItems");
            check(query_results.size() == 2);

            std::vector<std::string> item_names = query_results.getColumnFieldAs<std::string>("$i", "name");

            // Check both items are present (order may vary)
            bool has_anvil = std::find(item_names.begin(), item_names.end(), "Anvil") != item_names.end();
            bool has_rocket = std::find(item_names.begin(), item_names.end(), "Rocket") != item_names.end();
            check(has_anvil);
            check(has_rocket);
        }
    }

    group("Global Containers") {
        it("uses global List in RHS assignment and expression") {
            TestFixture fixture;
            fixture.build_session(R"(
                global List results

                declare Trigger
                    flag: int
                end

                declare Snapshot
                    items: List<int>
                    itemCount: int
                end

                query "findSnapshots"
                    $s: Snapshot()
                end

                rule "Use Global List"
                when
                    $t: Trigger(flag == 1)
                then
                    insert Snapshot { items = $results, itemCount = $results.size }
                end
            )");

            fixture.session->set_global("results", make_typed_list({int64_t(10), int64_t(20)}));

            auto trigger = std::make_shared<Fact>();
            trigger->type = "Trigger";
            trigger->fields["flag"] = int64_t(1);
            fixture.session->add_fact(trigger);

            int fired = fixture.session->fire_all_rules();
            check(fired == 1);

            QueryResult qr = fixture.session->execute_query("findSnapshots");
            check(qr.size() == 1);
            auto row = qr.single();
            auto snap_opt = row.get("$s");
            check(snap_opt.has_value());
            Fact* snap = *snap_opt;

            auto list_field = snap->get_field("items");
            check(list_field.has_value());
            auto list_ptr = std::get_if<std::shared_ptr<TypedList>>(&*list_field);
            check(list_ptr != nullptr);
            check(*list_ptr != nullptr);
            check((*list_ptr)->values.size() == 2);
            check(std::get<int64_t>((*list_ptr)->values[0]) == 10);
            check(std::get<int64_t>((*list_ptr)->values[1]) == 20);

            auto item_count = snap->get_field("itemCount");
            check(item_count.has_value());
            check(std::get<int64_t>(*item_count) == 2);
        }

        it("iterates global List in for loop") {
            TestFixture fixture;
            fixture.build_session(R"(
                global List results

                declare Trigger
                    flag: int
                end

                declare Result
                    value: int
                end

                query "findResults"
                    $r: Result()
                end

                rule "Iterate Global List"
                when
                    $t: Trigger(flag == 1)
                then
                    for $x in $results {
                        insert Result { value = $x.value }
                    }
                end
            )");

            fixture.session->set_global("results", make_typed_list({int64_t(3), int64_t(5), int64_t(8)}));

            auto trigger = std::make_shared<Fact>();
            trigger->type = "Trigger";
            trigger->fields["flag"] = int64_t(1);
            fixture.session->add_fact(trigger);

            int fired = fixture.session->fire_all_rules();
            check(fired == 1);

            QueryResult qr = fixture.session->execute_query("findResults");
            check(qr.size() == 3);
        }
    }

    group("Runtime Safety") {
        it("resets halt state between fire_all_rules calls") {
            TestFixture fixture;
            fixture.build_session(R"(
                declare Signal
                    id: int
                end

                rule "StopSignal"
                when
                    $s : Signal()
                then
                    halt
                end
            )");

            auto s1 = std::make_shared<Fact>();
            s1->type = "Signal";
            s1->fields["id"] = static_cast<int64_t>(1);
            fixture.session->add_fact(s1);
            check(fixture.session->fire_all_rules() == 1);

            auto s2 = std::make_shared<Fact>();
            s2->type = "Signal";
            s2->fields["id"] = static_cast<int64_t>(2);
            fixture.session->add_fact(s2);

            // If halt flag leaks between runs, this becomes 0.
            check(fixture.session->fire_all_rules() == 1);
        }

        it("validates facts in add_facts strict mode before insertion") {
            TestFixture fixture;
            fixture.build_session(R"(
                declare Person
                    age: int
                end
            )");
            fixture.session->set_validation_mode(ValidationMode::Strict);

            auto valid = std::make_shared<Fact>();
            valid->type = "Person";
            valid->fields["age"] = static_cast<int64_t>(42);

            auto invalid = std::make_shared<Fact>();
            invalid->type = "Person";
            invalid->fields["age"] = std::string("not-an-int");

            bool threw = false;
            try {
                fixture.session->add_facts(std::vector<Fact*>{valid.get(), invalid.get()});
            } catch (SchemaValidationException const&) {
                threw = true;
            }
            check(threw);
            check(fixture.session->get_fact_count() == 0);
        }

        it("rolls back update when rhs insert fails in strict mode") {
            TestFixture fixture;
            fixture.build_session(R"(
                declare Person
                    name: String
                    age: int
                end
                declare Audit
                    code: int
                end

                query "findPeople"
                    $p : Person()
                end

                rule "UpdateThenFail"
                when
                    $p : Person(name == "Alice")
                then
                    update $p { name = "Diana" }
                    insert Audit { code = "bad" }
                end
            )");
            fixture.session->set_validation_mode(ValidationMode::Strict);

            auto person = std::make_shared<Fact>();
            person->type = "Person";
            person->fields["name"] = std::string("Alice");
            person->fields["age"] = static_cast<int64_t>(30);
            fixture.session->add_fact(person);

            bool threw = false;
            try {
                fixture.session->fire_all_rules();
            } catch (SchemaValidationException const&) {
                threw = true;
            }
            check(threw);

            // Failed RHS must leave working memory in the pre-activation state.
            check(fixture.session->get_fact_count() == 1);
            QueryResult query_results = fixture.session->execute_query("findPeople");
            check(query_results.size() == 1);
            QueryResultRow row = query_results.single();
            auto restored_name = row.getFieldAs<std::string>("$p", "name");
            check(restored_name.has_value());
            check(restored_name.value() == "Alice");
        }

        it("rolls back mixed update retract and insert failure") {
            TestFixture fixture;
            fixture.build_session(R"(
                declare Person
                    name: String
                    age: int
                end
                declare Temp
                    marker: int
                end
                declare Audit
                    code: int
                end

                query "findPeople"
                    $p : Person()
                end

                rule "MixedFailure"
                when
                    $p : Person(name == "Alice")
                then
                    update $p { name = "Diana" }
                    insert Temp { marker = 1 }
                    retract $p
                    insert Audit { code = "bad" }
                end
            )");
            fixture.session->set_validation_mode(ValidationMode::Strict);

            auto person = std::make_shared<Fact>();
            person->type = "Person";
            person->fields["name"] = std::string("Alice");
            person->fields["age"] = static_cast<int64_t>(30);
            fixture.session->add_fact(person);

            bool threw = false;
            try {
                fixture.session->fire_all_rules();
            } catch (SchemaValidationException const&) {
                threw = true;
            }
            check(threw);

            check(fixture.session->get_fact_count() == 1);
            QueryResult query_results = fixture.session->execute_query("findPeople");
            check(query_results.size() == 1);
            QueryResultRow row = query_results.single();
            auto restored_name = row.getFieldAs<std::string>("$p", "name");
            check(restored_name.has_value());
            check(restored_name.value() == "Alice");
        }

        it("blocks mutating apis after rollback failure until reset") {
            TestFixture fixture;
            fixture.build_session(R"(
                declare Person
                    name: String
                    age: int
                end
                declare Audit
                    code: int
                end

                rule "RollbackFailure"
                when
                    $p : Person(name == "Alice")
                then
                    update $p { age = "invalid" }
                    retract $p
                    insert Audit { code = "bad" }
                end
            )");
            fixture.session->set_validation_mode(ValidationMode::Strict);

            auto person = std::make_shared<Fact>();
            person->type = "Person";
            person->fields["name"] = std::string("Alice");
            person->fields["age"] = static_cast<int64_t>(30);
            fixture.session->add_fact(person);

            bool fire_threw = false;
            try {
                fixture.session->fire_all_rules();
            } catch (SchemaValidationException const&) {
                fire_threw = true;
            }
            check(fire_threw);
            check(!fixture.session->is_consistent());
            check(fixture.session->get_fact_count() == 0);
            check_equal(fixture.session->fire_all_rules(), 0);
            check_equal(fixture.session->fire_all_rules_fail_fast(), 0);

            auto another = std::make_shared<Fact>();
            another->type = "Person";
            another->fields["name"] = std::string("Bob");
            another->fields["age"] = static_cast<int64_t>(40);

            bool add_threw = false;
            try {
                fixture.session->add_fact(another);
            } catch (std::runtime_error const&) {
                add_threw = true;
            }
            check(add_threw);
            check(fixture.session->get_fact_count() == 0);

            bool retract_threw = false;
            try {
                fixture.session->retract_fact(person);
            } catch (std::runtime_error const&) {
                retract_threw = true;
            }
            check(retract_threw);

            bool global_threw = false;
            try {
                fixture.session->set_global("threshold", static_cast<int64_t>(10));
            } catch (std::runtime_error const&) {
                global_threw = true;
            }
            check(global_threw);

            bool focus_threw = false;
            try {
                fixture.session->set_focus("MAIN");
            } catch (std::runtime_error const&) {
                focus_threw = true;
            }
            check(focus_threw);

            bool validation_mode_threw = false;
            try {
                fixture.session->set_validation_mode(ValidationMode::Warn);
            } catch (std::runtime_error const&) {
                validation_mode_threw = true;
            }
            check(validation_mode_threw);

            bool tracing_threw = false;
            try {
                fixture.session->enable_tracing(true);
            } catch (std::runtime_error const&) {
                tracing_threw = true;
            }
            check(tracing_threw);

            auto listener = std::make_shared<IEngineListener>();
            bool add_listener_threw = false;
            try {
                fixture.session->addListener(listener);
            } catch (std::runtime_error const&) {
                add_listener_threw = true;
            }
            check(add_listener_threw);

            bool remove_listener_threw = false;
            try {
                fixture.session->removeListener(listener);
            } catch (std::runtime_error const&) {
                remove_listener_threw = true;
            }
            check(remove_listener_threw);

            fixture.session->reset();
            check(fixture.session->is_consistent());

            fixture.session->set_global("threshold", static_cast<int64_t>(10));
            auto threshold = fixture.session->get_global("threshold");
            check(threshold.has_value());
            check(std::get<int64_t>(*threshold) == 10);

            fixture.session->set_validation_mode(ValidationMode::Warn);
            check(fixture.session->get_validation_mode() == ValidationMode::Warn);

            fixture.session->enable_tracing(true);
            fixture.session->addListener(listener);
            fixture.session->removeListener(listener);

            fixture.session->add_fact(another);
            check(fixture.session->get_fact_count() == 1);
        }
    }
}
