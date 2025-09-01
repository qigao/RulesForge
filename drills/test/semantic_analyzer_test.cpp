#include "catch2/catch_all.hpp"
#include "drools_parser.hpp"
#include "knowledge_base.hpp"
#include "stateful_session.hpp"

// A dedicated test fixture for semantic analysis tests.
struct SemanticTestFixture {
    /**
     * @brief A helper that builds a KnowledgeBase from DRL and asserts that it fails
     *        with a specific semantic error.
     *
     * This function encapsulates the entire test pattern for semantic validation,
     * making individual test cases clean and declarative.
     *
     * @param drl The DRL string containing the expected error.
     * @param expected_message_part A substring that must appear in the error message.
     */
    void expect_error(std::string const& drl, std::string const& expected_message_part) {
        ParsingResult result;
        // Use a consistent dummy filename for predictable error messages.
        auto kb = build_knowledge_base(drl, result, "test.drl");

        // The build MUST fail for a semantic error.
        INFO("DRL being tested:\n" << drl);
        REQUIRE(kb == nullptr);
        REQUIRE_FALSE(result.success);

        // We expect exactly one semantic error for these focused tests.
        REQUIRE(result.errors.size() == 1);

        // Provide the actual error message for easy debugging if the check fails.
        INFO("Actual error message: " << result.errors[0].message);
        REQUIRE(result.errors[0].message.find(expected_message_part) != std::string::npos);
    }
};

TEST_CASE_METHOD(SemanticTestFixture, "Semantic Analysis: Binding and Scope Errors", "[semantic][binding]") {

    SECTION("Using an unbound variable in a constraint") {
        std::string drl = R"(
            declare Person end
            rule "x" when $p1: Person(this == $p2) then end
        )";
        expect_error(drl, "constraint uses undeclared binding '$p2'");
    }

    SECTION("Using an unbound variable in the RHS") {
        std::string drl = R"(
            declare Person end
            rule "x" when $p: Person() then drools.retract($p2); end
        )";
        expect_error(drl, "RHS uses undeclared variable '$p2'");
    }

    SECTION("Using a variable from a `not` clause in the RHS") {
        std::string drl = R"(
            declare Person end
            rule "x" when not($p: Person()) then drools.retract($p); end
        )";
        expect_error(drl, "RHS uses undeclared variable '$p'");
    }

    SECTION("Duplicate binding name") {
        std::string drl = R"(
            declare Person end
            rule "x" when $p: Person() $p: Person() then end
        )";
        expect_error(drl, "duplicate binding '$p' is declared");
    }

    SECTION("Duplicate inline binding name") {
        std::string drl = R"(
            declare Person name:String end
            rule "x" when Person($n: name, $n: name) then end
        )";
        expect_error(drl, "duplicate inline binding '$n' is declared");
    }

    SECTION("Unbound variable in `from accumulate` source") {
        std::string drl = R"(
            declare Purchase value: double end
            declare Result result: double end
            rule "x"
            when
                $r: Result() from accumulate($p: Purchase(value > $max_val), sum($p.value))
            then end
        )";
        expect_error(drl, "accumulate uses undeclared binding '$p'");
    }

    SECTION("Unbound variable in `from accumulate` function") {
        std::string drl = R"(
            declare Purchase value: double end
            declare Result result: double end
            rule "x"
            when
                $r: Result() from accumulate($p: Purchase(), sum($p2.value))
            then end
        )";
        expect_error(drl, "accumulate uses undeclared binding '$p2'");
    }
}

TEST_CASE_METHOD(SemanticTestFixture, "Semantic Analysis: Type and Field Errors", "[semantic][type]") {

    SECTION("Using an undeclared fact type in a pattern") {
        std::string drl = R"(
            rule "x" when $p: NonExistentType() then end
        )";
        expect_error(drl, "pattern uses undeclared or unresolvable fact type 'NonExistentType'");
    }

    SECTION("Using an undeclared field in a constraint") {
        std::string drl = R"(
            declare Person name: String end
            rule "x" when Person(non_existent_field == 'test') then end
        )";
        expect_error(drl, "constraint field 'non_existent_field' not found on fact type 'Person'");
    }

    SECTION("Using an undeclared field in an `accumulate` function") {
        std::string drl = R"(
            declare Purchase value: double end
            declare Result result: double end
            rule "x" when
                $r: Result() from accumulate($p: Purchase(), sum($p.non_existent_field))
            then end
        )";
        expect_error(drl, "accumulate field 'non_existent_field' not found on type 'Purchase'");
    }
}

TEST_CASE_METHOD(SemanticTestFixture, "Semantic Analysis: Rule Structure Errors", "[semantic][rule]") {

    SECTION("Rule extends a non-existent parent rule") {
        std::string drl = R"(
            rule "Child Rule" extends "NonExistentParent"
            when
            then
            end
        )";
        expect_error(drl, "Rule 'Child Rule' extends non-existent rule 'NonExistentParent'");
    }
}
