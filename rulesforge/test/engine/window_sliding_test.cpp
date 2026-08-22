#include "rfl_parser.hpp"
#include "engine/knowledge_base.hpp"
#include "engine/stateful_session.hpp"
#include "test_helpers.hpp"
#include "tinytest.hpp"

#include <chrono>
#include <thread>

using namespace rulesforge;

struct WindowTestFixture {
    std::shared_ptr<KnowledgeBase> kb;

    WindowTestFixture(std::string const& rules) {
        ParsingResult result;
        kb = build_knowledge_base(rules, result);
        if (!result.success) throw_parse_failure(result);
        if (!kb) { throw std::runtime_error("KnowledgeBase is null"); }
    }

    std::shared_ptr<Fact> create_event(int64_t id, int64_t value, int64_t timestamp = 0) {
        auto event = std::make_shared<Fact>();
        event->type = "Event";
        event->fields["id"] = id;
        event->fields["value"] = value;
        if (timestamp > 0) {
            event->fields["timestamp"] = timestamp;
        }
        return event;
    }
};

suite("Engine Sliding Window") {
    group("Length Window") {
        it("evicts oldest facts when length is exceeded") {
            std::string const drl = R"(
                package test.sliding.len;
                declare CountResult result: double end
                declare Result count: double end
                rule "Test Length Window"
                when
                    $c : CountResult() from accumulate( Event() over window:length(3), count() )
                then
                    insert Result { count = $c.result }
                end

                query "GetResults"
                    $r : Result()
                end
            )";
            WindowTestFixture fixture(drl);
            auto session = fixture.kb->create_session();
            
            // Insert 5 events. The window length is 3. 
            // So only the last 3 should remain in the window.
            session->add_fact(fixture.create_event(1, 10));
            session->add_fact(fixture.create_event(2, 20));
            session->add_fact(fixture.create_event(3, 30));
            session->add_fact(fixture.create_event(4, 40));
            session->add_fact(fixture.create_event(5, 50));

            int fired = session->fire_all_rules();
            
            // The constraint $c : count($e) ... will recalculate count properties on every insert/retract
            // So Result might be inserted multiple times or we can just query the resulting facts
            // Better to check if there is a Result fact with count == 3
            
            bool found_3 = false;
            auto results = session->execute_query("GetResults");
            for (auto const& row : results) {
                auto cnt = row.getFieldAs<int64_t>("r", "count");
                if (cnt && *cnt == 3) {
                    found_3 = true;
                }
            }
            check(found_3);
            check(fired >= 1);
        }
    }
    
    group("Time Window") {
        it("evicts old facts based on embedded timestamp field") {
            std::string const drl = R"(
                package test.sliding.time;
                declare CountResult result: double end
                declare Result count: double end
                rule "Test Time Window"
                when
                    $c : CountResult() from accumulate( Event() over window:time(50), count() )
                then
                    insert Result { count = $c.result }
                end

                query "GetResults"
                    $r : Result()
                end
            )";
            WindowTestFixture fixture(drl);
            auto session = fixture.kb->create_session();
            
            int64_t current_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            
            // Add initial facts with current timestamp
            session->add_fact(fixture.create_event(1, 10, current_time));
            session->add_fact(fixture.create_event(2, 20, current_time));
            
            session->fire_all_rules();
            
            // We wait to make sure time advances enough (say 100ms)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            int64_t new_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
                
            // Inserting a new fact triggers the evaluate_expiration which uses system clock
            session->add_fact(fixture.create_event(3, 30, new_time));
            session->fire_all_rules();
            
            bool found_1 = false;
            auto results = session->execute_query("GetResults");
            for (auto const& row : results) {
                auto cnt = row.getFieldAs<int64_t>("r", "count");
                if (cnt && *cnt == 1) {
                    found_1 = true;
                }
            }
            check(found_1);
        }
    }
}
