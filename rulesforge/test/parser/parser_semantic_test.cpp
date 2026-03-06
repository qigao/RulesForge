#include "parser/rfl_parser.hpp"
#include "engine/knowledge_base.hpp"
#include "engine/stateful_session.hpp"
#include "tinytest.h"

struct SemanticTestHelper {
    static void expect_error(std::string const& drl, std::string const& expected_message_part) {
        ParsingResult result;
        auto kb = build_knowledge_base(drl, result, "test.drl", false);

        if (kb != nullptr) { throw std::runtime_error("Expected parsing to fail, but it succeeded"); }
        if (result.success) { throw std::runtime_error("Expected result.success to be false"); }
        if (result.errors.size() != 1) {
            throw std::runtime_error("Expected exactly 1 error, got " + std::to_string(result.errors.size()));
        }
        if (result.errors[0].message.find(expected_message_part) == std::string::npos) {
            throw std::runtime_error("Expected error containing '" + expected_message_part + "', got: " + result.errors[0].message);
        }
    }
};

suite("Semantic Analysis") {
    group("Binding and Scope Errors") {
        it("detects unbound variable in a constraint") {
            std::string drl = R"(
                declare Person end
                rule "x" when $p1: Person(this == $p2) then end
            )";
            SemanticTestHelper::expect_error(drl, "constraint uses undeclared binding '$p2'");
        }

        it("detects unbound variable in the RHS") {
            std::string drl = R"(
                declare Person end
                rule "x" when $p: Person() then retract $p2 end
            )";
            SemanticTestHelper::expect_error(drl, "RHS uses undeclared variable '$p2'");
        }

        it("detects variable from 'not' clause used in RHS") {
            std::string drl = R"(
                declare Person end
                rule "x" when not($p: Person()) then retract $p end
            )";
            SemanticTestHelper::expect_error(drl, "RHS uses undeclared variable '$p'");
        }

        it("detects duplicate binding name") {
            std::string drl = R"(
                declare Person end
                rule "x" when $p: Person() $p: Person() then end
            )";
            SemanticTestHelper::expect_error(drl, "duplicate binding '$p' is declared");
        }

        it("detects duplicate inline binding name") {
            std::string drl = R"(
                declare Person name:String end
                rule "x" when Person($n: name, $n: name) then end
            )";
            SemanticTestHelper::expect_error(drl, "duplicate inline binding '$n' is declared");
        }

        it("detects unbound variable in 'from accumulate' source") {
            std::string drl = R"(
                declare Purchase value: double end
                declare Result result: double end
                rule "x"
                when
                    $r: Result() from accumulate($p: Purchase(value > $max_val), sum($p.value))
                then end
            )";
            SemanticTestHelper::expect_error(drl, "constraint uses undeclared binding '$max_val'");
        }

        it("detects unbound variable in 'from accumulate' function") {
            std::string drl = R"(
                declare Purchase value: double end
                declare Result result: double end
                rule "x"
                when
                    $r: Result() from accumulate($p: Purchase(), sum($p2.value))
                then end
            )";
            SemanticTestHelper::expect_error(drl, "accumulate uses undeclared binding '$p2'");
        }
    }

    group("Type and Field Errors") {
        it("detects undeclared fact type in a pattern") {
            std::string drl = R"(
                rule "x" when $p: NonExistentType() then end
            )";
            SemanticTestHelper::expect_error(drl, "pattern uses undeclared or unresolvable fact type 'NonExistentType'");
        }

        it("detects undeclared field in a constraint") {
            std::string drl = R"(
                declare Person name: String end
                rule "x" when Person(non_existent_field == "test") then end
            )";
            SemanticTestHelper::expect_error(drl, "constraint field 'non_existent_field' not found on fact type 'Person'");
        }

        it("detects undeclared field in 'accumulate' function") {
            std::string drl = R"(
                declare Purchase value: double end
                declare Result result: double end
                rule "x" when
                    $r: Result() from accumulate($p: Purchase(), sum($p.non_existent_field))
                then end
            )";
            SemanticTestHelper::expect_error(drl, "accumulate field 'non_existent_field' not found on type 'Purchase'");
        }
    }

    group("Rule Structure Errors") {
        it("detects rule extending non-existent parent") {
            std::string drl = R"(
                rule "Child Rule" extends "NonExistentParent"
                when
                then
                end
            )";
            SemanticTestHelper::expect_error(drl, "Rule 'Child Rule' extends non-existent rule 'NonExistentParent'");
        }
    }
}
