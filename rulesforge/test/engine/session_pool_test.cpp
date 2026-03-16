#include "engine/session_pool.hpp"
#include "rfl_parser.hpp"
#include "tinytest.h"

suite("Session Pool") {
    group("Isolation") {
        it("returns a clean session after release") {
            std::string drl = R"(
                declare Message
                    id: int
                end
            )";

            ParsingResult result;
            auto kb = build_knowledge_base(drl, result);
            check(result.success);
            check(kb != nullptr);

            rulesforge::SessionPool pool(kb, 1);

            {
                rulesforge::SessionGuard guard(pool);
                check(static_cast<bool>(guard));

                Fact* fact = guard->create_fact("Message");
                fact->fields["id"] = static_cast<int64_t>(1);
                guard->add_fact(fact);
                check(guard->get_fact_count() == 1);
            }

            {
                rulesforge::SessionGuard guard(pool);
                check(static_cast<bool>(guard));
                check(guard->get_fact_count() == 0);
                check(guard->fire_all_rules() == 0);
            }
        }
    }
}
