#include "rfl_parser.hpp"
#include "engine/knowledge_base.hpp"
#include "engine/stateful_session.hpp"
#include "test_helpers.hpp"
#include "tinytest.h"

static std::unique_ptr<StatefulSession> build_session(std::string const& drl) {
    ParsingResult result;
    auto kb = build_knowledge_base(drl, result);
    if (!result.success) throw_parse_failure(result);
    if (!kb) throw std::runtime_error("KnowledgeBase is null");
    auto session = kb->create_session();
    if (!session) throw std::runtime_error("Session is null");
    return session;
}

static Fact* find_fact_by_type(StatefulSession& session, std::string const& type) {
    for (int64_t i = 1; i <= session.get_next_fact_id(); ++i) {
        Fact* fact = session.get_fact_by_id(i);
        if (fact && fact->type == type) {
            return fact;
        }
    }
    return nullptr;
}

suite("RHS Control Flow") {

    group("else if") {
        it("selects correct branch") {
            auto session = build_session(R"(
                declare Item
                    price: int
                    tier: String
                end
                rule "Classify"
                when
                    $item : Item(tier == "")
                then
                    if $item.price > 100 {
                        update $item { tier = "premium" }
                    } else if $item.price > 50 {
                        update $item { tier = "standard" }
                    } else {
                        update $item { tier = "budget" }
                    }
                end
            )");

            auto f1 = std::make_shared<Fact>();
            f1->type = "Item";
            f1->fields["price"] = int64_t(200);
            f1->fields["tier"] = std::string("");
            session->add_fact(f1.get());

            auto f2 = std::make_shared<Fact>();
            f2->type = "Item";
            f2->fields["price"] = int64_t(75);
            f2->fields["tier"] = std::string("");
            session->add_fact(f2.get());

            auto f3 = std::make_shared<Fact>();
            f3->type = "Item";
            f3->fields["price"] = int64_t(20);
            f3->fields["tier"] = std::string("");
            session->add_fact(f3.get());

            session->fire_all_rules();

            check(std::get<std::string>(f1->fields["tier"]) == "premium");
            check(std::get<std::string>(f2->fields["tier"]) == "standard");
            check(std::get<std::string>(f3->fields["tier"]) == "budget");
        }
    }

    group("break") {
        it("exits for loop early") {
            auto session = build_session(R"(
                declare Item
                    value: int
                end
                declare Container
                    items: List<Item>
                end
                declare Result
                    value: int
                end
                rule "Break Test"
                when
                    $c : Container()
                then
                    for $item in $c.items {
                        if $item.value > 50 {
                            break
                        }
                        insert Result { value = $item.value }
                    }
                end
            )");

            auto i1 = std::make_shared<Fact>();
            i1->type = "Item"; i1->fields["value"] = int64_t(10);
            session->add_fact(i1.get());

            auto i2 = std::make_shared<Fact>();
            i2->type = "Item"; i2->fields["value"] = int64_t(20);
            session->add_fact(i2.get());

            auto i3 = std::make_shared<Fact>();
            i3->type = "Item"; i3->fields["value"] = int64_t(99);
            session->add_fact(i3.get());

            auto i4 = std::make_shared<Fact>();
            i4->type = "Item"; i4->fields["value"] = int64_t(30);
            session->add_fact(i4.get());

            auto container = std::make_shared<Fact>();
            container->type = "Container";
            container->fields["items"] = FactList{{i1.get(), i2.get(), i3.get(), i4.get()}};
            session->add_fact(container.get());

            session->fire_all_rules();

            // Container + 4 Items + 2 Results (10, 20 inserted before break at 99)
            check(session->get_fact_count() == 7);
        }
    }

    group("continue") {
        it("skips iteration") {
            auto session = build_session(R"(
                declare Item
                    value: int
                end
                declare Container
                    items: List<Item>
                end
                declare Result
                    value: int
                end
                rule "Continue Test"
                when
                    $c : Container()
                then
                    for $item in $c.items {
                        if $item.value > 50 {
                            continue
                        }
                        insert Result { value = $item.value }
                    }
                end
            )");

            auto i1 = std::make_shared<Fact>();
            i1->type = "Item"; i1->fields["value"] = int64_t(10);
            session->add_fact(i1.get());

            auto i2 = std::make_shared<Fact>();
            i2->type = "Item"; i2->fields["value"] = int64_t(99);
            session->add_fact(i2.get());

            auto i3 = std::make_shared<Fact>();
            i3->type = "Item"; i3->fields["value"] = int64_t(20);
            session->add_fact(i3.get());

            auto i4 = std::make_shared<Fact>();
            i4->type = "Item"; i4->fields["value"] = int64_t(88);
            session->add_fact(i4.get());

            auto i5 = std::make_shared<Fact>();
            i5->type = "Item"; i5->fields["value"] = int64_t(30);
            session->add_fact(i5.get());

            auto container = std::make_shared<Fact>();
            container->type = "Container";
            container->fields["items"] = FactList{{i1.get(), i2.get(), i3.get(), i4.get(), i5.get()}};
            session->add_fact(container.get());

            session->fire_all_rules();

            // Container + 5 Items + 3 Results (10, 20, 30 inserted; 99 and 88 skipped)
            check(session->get_fact_count() == 9);
        }

        it("does not skip top-level actions after a loop continue") {
            auto session = build_session(R"(
                declare Item
                    value: int
                end
                declare Container
                    items: List<Item>
                end
                declare Result
                    value: int
                end
                declare Done
                    value: int
                end
                rule "Continue Boundary Test"
                when
                    $c : Container()
                then
                    for $item in $c.items {
                        if $item.value > 50 {
                            continue
                        }
                        insert Result { value = $item.value }
                    }
                    insert Done { value = 1 }
                end
            )");

            auto i1 = std::make_shared<Fact>();
            i1->type = "Item"; i1->fields["value"] = int64_t(10);
            session->add_fact(i1.get());

            auto i2 = std::make_shared<Fact>();
            i2->type = "Item"; i2->fields["value"] = int64_t(99);
            session->add_fact(i2.get());

            auto i3 = std::make_shared<Fact>();
            i3->type = "Item"; i3->fields["value"] = int64_t(20);
            session->add_fact(i3.get());

            auto container = std::make_shared<Fact>();
            container->type = "Container";
            container->fields["items"] = FactList{{i1.get(), i2.get(), i3.get()}};
            session->add_fact(container.get());

            session->fire_all_rules();

            check(session->get_fact_count() == 7);
            Fact* done = find_fact_by_type(*session, "Done");
            check(done != nullptr);
            check(std::get<int64_t>(done->fields["value"]) == 1);
        }

        it("hard-fails when a for loop source field is missing") {
            auto session = build_session(R"(
                declare Container
                    items: List<Item>
                end
                declare Item
                    value: int
                end
                declare Done
                    value: int
                end
                rule "Missing For Source Field"
                when
                    $c : Container()
                then
                    for $item in $c.items {
                        insert Done { value = $item.value }
                    }
                    insert Done { value = 999 }
                end
            )");

            auto container = std::make_shared<Fact>();
            container->type = "Container";
            session->add_fact(container.get());

            bool threw = false;
            try {
                (void)session->fire_all_rules();
            } catch (std::runtime_error const& e) {
                threw = std::string(e.what()).find("RHS for field '$c.items' not found") != std::string::npos;
            }
            check(threw);
            check(find_fact_by_type(*session, "Done") == nullptr);
        }

        it("fails to build when a for loop value-list variable is undeclared") {
            ParsingResult result;
            auto kb = build_knowledge_base(R"(
                declare Item
                    value: int
                end
                declare Done
                    value: int
                end
                rule "Missing For Source List Variable"
                when
                    $item : Item()
                then
                    for $x in ($item, $missing) {
                        insert Done { value = $x.value }
                    }
                end
            )", result);

            check(!result.success);
            check(kb == nullptr);
            check(!result.errors.empty());
            check(result.errors.front().message.find("RHS uses undeclared variable '$missing'")
                  != std::string::npos);
        }
    }

    group("insert typing") {
        it("coerces insert expression result to declared int field") {
            auto session = build_session(R"(
                declare Seed
                    base: int
                end
                declare Result
                    count: int
                end
                rule "Insert Typed Result"
                when
                    $s : Seed()
                then
                    insert Result { count = $s.base + 1 }
                end
            )");

            auto seed = std::make_shared<Fact>();
            seed->type = "Seed";
            seed->fields["base"] = int64_t(41);
            session->add_fact(seed.get());

            session->fire_all_rules();

            Fact* result = find_fact_by_type(*session, "Result");
            check(result != nullptr);
            check(std::holds_alternative<int64_t>(result->fields["count"]));
            check(std::get<int64_t>(result->fields["count"]) == 42);
        }

        it("coerces insertLogical expression result to declared int field") {
            auto session = build_session(R"(
                declare Seed
                    base: int
                end
                declare Result
                    count: int
                end
                rule "Insert Logical Typed Result"
                when
                    $s : Seed()
                then
                    insertLogical Result { count = $s.base + 1 }
                end
            )");

            auto seed = std::make_shared<Fact>();
            seed->type = "Seed";
            seed->fields["base"] = int64_t(6);
            session->add_fact(seed.get());

            session->fire_all_rules();

            Fact* result = find_fact_by_type(*session, "Result");
            check(result != nullptr);
            check(std::holds_alternative<int64_t>(result->fields["count"]));
            check(std::get<int64_t>(result->fields["count"]) == 7);
        }

        it("hard-fails RHS expression when a bound fact field is missing") {
            auto session = build_session(R"(
                declare Seed
                    base: int
                end
                declare Result
                    count: int
                end
                rule "Missing RHS Field"
                when
                    $s : Seed()
                then
                    insert Result { count = $s.base + 1 }
                end
            )");

            auto seed = std::make_shared<Fact>();
            seed->type = "Seed";
            session->add_fact(seed.get());

            bool threw = false;
            try {
                (void)session->fire_all_rules();
            } catch (std::runtime_error const& e) {
                threw = std::string(e.what()).find("RHS variable field") != std::string::npos;
            }
            check(threw);
            check(find_fact_by_type(*session, "Result") == nullptr);
        }

        it("fails to build when RHS expression uses an undeclared variable") {
            ParsingResult result;
            auto kb = build_knowledge_base(R"(
                declare Seed
                    base: int
                end
                declare Result
                    count: int
                end
                rule "Missing RHS Global"
                when
                    $s : Seed()
                then
                    insert Result { count = $missing + 1 }
                end
            )", result);

            check(!result.success);
            check(kb == nullptr);
            check(!result.errors.empty());
            check(result.errors.front().message.find("RHS uses undeclared variable '$missing'")
                  != std::string::npos);
        }

        it("does not coerce compound global fields to zero in RHS expressions") {
            auto session = build_session(R"(
                global Map attrs

                declare Seed
                    base: int
                end
                declare Result
                    count: int
                end
                rule "Bad Global Field Expression"
                when
                    $s : Seed()
                then
                    insert Result { count = $attrs.label + 1 }
                end
            )");

            auto attrs = std::make_shared<ValueMap>();
            attrs->entries[std::string("label")] = make_typed_list({int64_t(10)});
            session->set_global("attrs", attrs);

            auto seed = std::make_shared<Fact>();
            seed->type = "Seed";
            seed->fields["base"] = int64_t(1);
            session->add_fact(seed.get());

            bool threw = false;
            try {
                (void)session->fire_all_rules();
            } catch (std::runtime_error const&) {
                threw = true;
            }
            check(threw);
            check(find_fact_by_type(*session, "Result") == nullptr);
        }

    }

    group("while") {
        it("loops until condition is false") {
            auto session = build_session(R"(
                declare Counter
                    count: int
                    limit: int
                end
                declare Result
                    count: int
                end
                rule "While Test"
                when
                    $c : Counter(count == 0)
                then
                    while $c.count < $c.limit {
                        update $c { count = $c.count + 1 }
                    }
                    insert Result { count = $c.count }
                end
            )");

            auto fact = std::make_shared<Fact>();
            fact->type = "Counter";
            fact->fields["count"] = int64_t(0);
            fact->fields["limit"] = int64_t(5);
            session->add_fact(fact.get());
            session->fire_all_rules();

            check(std::get<int64_t>(fact->fields["count"]) == 5);
            Fact* result = find_fact_by_type(*session, "Result");
            check(result != nullptr);
            check(std::get<int64_t>(result->fields["count"]) == 5);
        }

        it("fails and rolls back on infinite loop safety limit") {
            auto session = build_session(R"(
                declare Counter
                    count: int
                end
                rule "Infinite Loop Guard"
                    salience 1
                when
                    $c : Counter(count == 0)
                then
                    while 1 > 0 {
                        update $c { count = $c.count + 1 }
                    }
                end
            )");

            auto fact = std::make_shared<Fact>();
            fact->type = "Counter";
            fact->fields["count"] = int64_t(0);
            session->add_fact(fact.get());

            bool threw = false;
            try {
                (void)session->fire_all_rules();
            } catch (std::runtime_error const& e) {
                threw = true;
                check(std::string(e.what()).find("max iterations") != std::string::npos);
            }

            check(threw);
            check(std::get<int64_t>(fact->fields["count"]) == 0);
        }

        it("supports break in while") {
            auto session = build_session(R"(
                declare Counter
                    count: int
                end
                rule "While Break"
                when
                    $c : Counter()
                then
                    while $c.count < 100 {
                        if $c.count == 3 {
                            break
                        }
                        update $c { count = $c.count + 1 }
                    }
                end
            )");

            auto fact = std::make_shared<Fact>();
            fact->type = "Counter";
            fact->fields["count"] = int64_t(0);
            session->add_fact(fact.get());
            session->fire_all_rules();

            check(std::get<int64_t>(fact->fields["count"]) == 3);
        }
    }

    group("switch") {
        it("matches correct case") {
            auto session = build_session(R"(
                declare Order
                    level: int
                    discount: int
                end
                rule "Switch Test"
                when
                    $o : Order(discount == 0)
                then
                    switch $o.level {
                        case 1 {
                            update $o { discount = 10 }
                        }
                        case 2 {
                            update $o { discount = 20 }
                        }
                        case 3 {
                            update $o { discount = 30 }
                        }
                        default {
                            update $o { discount = 5 }
                        }
                    }
                end
            )");

            auto f1 = std::make_shared<Fact>();
            f1->type = "Order";
            f1->fields["level"] = int64_t(2);
            f1->fields["discount"] = int64_t(0);
            session->add_fact(f1.get());
            session->fire_all_rules();

            check(std::get<int64_t>(f1->fields["discount"]) == 20);
        }

        it("falls through to default") {
            auto session = build_session(R"(
                declare Order
                    level: int
                    discount: int
                end
                rule "Switch Default"
                when
                    $o : Order(discount == 0)
                then
                    switch $o.level {
                        case 1 {
                            update $o { discount = 10 }
                        }
                        default {
                            update $o { discount = 5 }
                        }
                    }
                end
            )");

            auto fact = std::make_shared<Fact>();
            fact->type = "Order";
            fact->fields["level"] = int64_t(99);
            fact->fields["discount"] = int64_t(0);
            session->add_fact(fact.get());
            session->fire_all_rules();

            check(std::get<int64_t>(fact->fields["discount"]) == 5);
        }
    }
}
