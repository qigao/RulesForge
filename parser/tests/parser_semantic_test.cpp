// parser_semantic_test.cpp
// Verifies the SemanticAnalyzer stage in isolation.
//
// The pipeline exercised here is:
//   rfl_parse_lemon  →  AstTransformer  →  SemanticAnalyzer
//
// No KnowledgeBase is built and no RETE network is touched.  Every test
// only checks that the semantic analyzer reports (or does not report) the
// expected errors on a given input string.

#include "rfl_parser_impl.hpp"    // rfl_parse_lemon
#include "ast_transformer.hpp"    // AstTransformer
#include "semantic_analyzer.hpp"  // SemanticAnalyzer
#include "core/rfl_parser_state.hpp"
#include "core/errors.hpp"
#include "tinytest.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

struct SemanticResult {
    bool        success;
    parser_state state;
    std::vector<StructuredError> errors;
};

// Run the full analysis pipeline up to (and including) SemanticAnalyzer
// without building a KnowledgeBase.
static SemanticResult run_semantic_analysis(std::string const& drl,
                                            std::string const& source = "test.rfl") {
    SemanticResult out;

    // 1. Lex + parse
    out.state = rfl_parse_lemon(drl, source, out.errors);
    if (!out.errors.empty()) {
        out.success = false;
        return out;
    }

    // 2. AST transformations (e.g. forall expansion)
    AstTransformer transformer(out.state);
    transformer.transform();

    // 3. Semantic analysis
    SemanticAnalyzer analyzer(out.state, source);
    bool decl_ok  = analyzer.build_and_analyze_declarations();
    bool rules_ok = analyzer.analyze_rules_and_queries();

    if (!decl_ok || !rules_ok) {
        out.success = false;
        out.errors  = analyzer.get_errors();
    } else {
        out.success = true;
    }
    return out;
}

// Expect the analysis to fail with exactly one error whose message contains
// the given substring.  Asserts via tinytest macros so failures appear inline.
static void expect_one_error(std::string const& drl, std::string const& expected_msg_part) {
    auto r = run_semantic_analysis(drl);

    if (r.success) {
         return;
    }
    if (r.errors.size() != 1) {
        std::string msg = "Expected exactly 1 error, got " + std::to_string(r.errors.size());
        if (!r.errors.empty()) {
            msg += "  First error: " + r.errors[0].message;
        }
        return;
    }
    bool found = r.errors[0].message.find(expected_msg_part) != std::string::npos;
    if (!found) {
        std::string msg =
            "Expected error containing \"" + expected_msg_part +
            "\", but got: \"" + r.errors[0].message + "\"";
     }
}

// ---------------------------------------------------------------------------
// Suite
// ---------------------------------------------------------------------------

suite("Semantic Analysis") {
    // ------------------------------------------------------------------
    group("Binding and Scope Errors") {
        it("detects unbound variable in a constraint") {
            expect_one_error(R"(
                declare Person end
                rule "x" when $p1: Person(this == $p2) then end
            )", "constraint uses undeclared binding '$p2'");
        }

        it("detects unbound variable in the RHS") {
            expect_one_error(R"(
                declare Person end
                rule "x" when $p: Person() then retract $p2 end
            )", "RHS uses undeclared variable '$p2'");
        }

        it("detects variable from 'not' clause used in RHS") {
            expect_one_error(R"(
                declare Person end
                rule "x" when not($p: Person()) then retract $p end
            )", "RHS uses undeclared variable '$p'");
        }

        it("detects duplicate binding name") {
            expect_one_error(R"(
                declare Person end
                rule "x" when $p: Person() $p: Person() then end
            )", "duplicate binding '$p' is declared");
        }

        it("detects duplicate inline binding name") {
            expect_one_error(R"(
                declare Person name:String end
                rule "x" when Person($n: name, $n: name) then end
            )", "duplicate inline binding '$n' is declared");
        }

        it("detects unbound variable in 'from accumulate' source") {
            expect_one_error(R"(
                declare Purchase value: double end
                declare Result result: double end
                rule "x"
                when
                    $r: Result() from accumulate($p: Purchase(value > $max_val), sum($p.value))
                then end
            )", "constraint uses undeclared binding '$max_val'");
        }

        it("detects unbound variable in 'from accumulate' function") {
            expect_one_error(R"(
                declare Purchase value: double end
                declare Result result: double end
                rule "x"
                when
                    $r: Result() from accumulate($p: Purchase(), sum($p2.value))
                then end
            )", "accumulate uses undeclared binding '$p2'");
        }
    }

    // ------------------------------------------------------------------
    group("Type and Field Errors") {
        it("detects undeclared fact type in a pattern") {
            expect_one_error(R"(
                rule "x" when $p: NonExistentType() then end
            )", "pattern uses undeclared or unresolvable fact type 'NonExistentType'");
        }

        it("detects undeclared field in a constraint") {
            expect_one_error(R"(
                declare Person name: String end
                rule "x" when Person(non_existent_field == "test") then end
            )", "constraint field 'non_existent_field' not found on fact type 'Person'");
        }

        it("detects undeclared field in 'accumulate' function") {
            expect_one_error(R"(
                declare Purchase value: double end
                declare Result result: double end
                rule "x" when
                    $r: Result() from accumulate($p: Purchase(), sum($p.non_existent_field))
                then end
            )", "accumulate field 'non_existent_field' not found on type 'Purchase'");
        }
    }

    // ------------------------------------------------------------------
    group("Rule Structure Errors") {
        it("detects rule extending non-existent parent") {
            expect_one_error(R"(
                rule "Child Rule" extends "NonExistentParent"
                when
                then
                end
            )", "Rule 'Child Rule' extends non-existent rule 'NonExistentParent'");
        }
    }

    // ------------------------------------------------------------------
    group("Valid Programs") {
        it("accepts a well-formed rule with known types and fields") {
            auto r = run_semantic_analysis(R"(
                declare Person name: String age: int end
                rule "adult check"
                when
                    $p: Person(age > 18)
                then
                end
            )");
            check(r.success);
            check(r.errors.empty());
        }

        it("accepts a rule with correctly bound cross-pattern variable") {
            auto r = run_semantic_analysis(R"(
                declare Order id: int end
                declare Item orderId: int end
                rule "join"
                when
                    $o: Order()
                    $i: Item(orderId == $o.id)
                then
                end
            )");
            check(r.success);
            check(r.errors.empty());
        }
    }
}
