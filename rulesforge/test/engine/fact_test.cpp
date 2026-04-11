#include "core/fact.hpp"
#include "core/value_types.hpp"
#include "tinytest.h"

#include <set>

suite("Fact") {
    group("FactList value semantics") {
        it("treats FactList with same fact ids as equal") {
            Fact lhs_a;
            lhs_a.id = 7;
            Fact rhs_a;
            rhs_a.id = 7;
            Fact rhs_b;
            rhs_b.id = 8;

            FactList lhs{{&lhs_a}};
            FactList rhs_same{{&rhs_a}};
            FactList rhs_diff{{&rhs_b}};

            check(lhs == rhs_same);
            check(lhs != rhs_diff);

            std::set<ConstraintValue, ConstraintValueCompare> values;
            values.insert(lhs);
            values.insert(rhs_same);
            values.insert(rhs_diff);
            check(values.size() == 2);
        }
    }

    group("Path lookup") {
        it("rejects malformed list index instead of reading element zero") {
            Fact item0;
            item0.id = 1;
            item0.type = "Item";
            item0.fields["value"] = int64_t(10);

            Fact item1;
            item1.id = 2;
            item1.type = "Item";
            item1.fields["value"] = int64_t(20);

            Fact container;
            container.type = "Container";
            container.fields["items"] = FactList{{&item0, &item1}};

            auto malformed = container.get_field("items[foo].value");
            check(!malformed.has_value());

            auto explicit_index = container.get_field("items[1].value");
            check(explicit_index.has_value());
            check(std::get<int64_t>(*explicit_index) == 20);
        }

        it("does not silently collapse multi-fact paths to the first fact") {
            Fact child1;
            child1.id = 11;
            child1.type = "Child";
            child1.fields["value"] = int64_t(10);

            Fact child2;
            child2.id = 12;
            child2.type = "Child";
            child2.fields["value"] = int64_t(20);

            Fact parent;
            parent.type = "Parent";
            parent.fields["children"] = FactList{{&child1, &child2}};

            auto implicit = parent.get_field("children.value");
            check(!implicit.has_value());

            auto indexed = parent.get_field("children[0].value");
            check(indexed.has_value());
            check(std::get<int64_t>(*indexed) == 10);
        }
    }
}
