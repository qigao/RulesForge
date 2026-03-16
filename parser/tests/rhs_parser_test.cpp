#include "rhs_parser.hpp"
#include "tinytest.h"

using namespace rulesforge;

// Helper: standard LHS bindings for most tests
static std::map<std::string, int> make_bindings() {
    return {{"$user", 0}, {"$item", 1}, {"$order", 2}};
}

suite("RhsParser") {

    // ========================================================================
    // Basic Actions
    // ========================================================================

    group("update") {
        it("parses update with field assignments") {
            auto actions = RhsParser::parse(
                R"(update $user { name = "alice", age = 30 })",
                make_bindings());

            check(actions.size() == 1);
            check(actions[0].type == RhsActionType::UPDATE);
            check(actions[0].target_var == "$user");
            check(actions[0].assignments.size() == 2);
            check(actions[0].assignments[0].field_name == "name");
            check(actions[0].assignments[0].type == RhsValueType::STRING);
            check(actions[0].assignments[0].string_literal == "alice");
            check(actions[0].assignments[1].field_name == "age");
            check(actions[0].assignments[1].type == RhsValueType::NUMERIC);
        }

        it("parses update with empty braces") {
            auto actions = RhsParser::parse(
                R"(update $user {})", make_bindings());

            check(actions.size() == 1);
            check(actions[0].type == RhsActionType::UPDATE);
            check(actions[0].assignments.empty());
        }

        it("allows trailing comma") {
            auto actions = RhsParser::parse(
                R"(update $user { x = 1, })", make_bindings());

            check(actions.size() == 1);
            check(actions[0].assignments.size() == 1);
        }
    }

    group("insert") {
        it("parses insert with type name and fields") {
            auto actions = RhsParser::parse(
                R"(insert Order { total = 100, status = "new" })",
                make_bindings());

            check(actions.size() == 1);
            check(actions[0].type == RhsActionType::INSERT);
            check(actions[0].target_type == "Order");
            check(actions[0].assignments.size() == 2);
        }
    }

    group("insertLogical") {
        it("parses insertLogical") {
            auto actions = RhsParser::parse(
                R"(insertLogical Discount { rate = 0.1 })",
                make_bindings());

            check(actions.size() == 1);
            check(actions[0].type == RhsActionType::INSERT_LOGICAL);
            check(actions[0].target_type == "Discount");
        }
    }

    group("retract") {
        it("parses retract with declared variable") {
            auto actions = RhsParser::parse(
                R"(retract $item)", make_bindings());

            check(actions.size() == 1);
            check(actions[0].type == RhsActionType::RETRACT);
            check(actions[0].target_var == "$item");
        }

        it("fails on undeclared variable") {
            std::string err;
            auto actions = RhsParser::parse(
                R"(retract $unknown)", make_bindings(), &err);

            check(actions.empty());
            check(!err.empty());
        }
    }

    group("halt") {
        it("parses halt") {
            auto actions = RhsParser::parse(
                R"(halt)", make_bindings());

            check(actions.size() == 1);
            check(actions[0].type == RhsActionType::HALT);
        }
    }

    group("setFocus") {
        it("parses setFocus with string arg") {
            auto actions = RhsParser::parse(
                R"(setFocus("priority"))", make_bindings());

            check(actions.size() == 1);
            check(actions[0].type == RhsActionType::SET_FOCUS);
            check(actions[0].focus_group == "priority");
        }
    }

    group("invoke") {
        it("parses invoke with mixed args") {
            auto actions = RhsParser::parse(
                R"(invoke sendWebhook($user.id, "alert", true, 42))", make_bindings());

            check(actions.size() == 1);
            check(actions[0].type == RhsActionType::INVOKE);
            check(actions[0].invoke_function == "sendWebhook");
            check(actions[0].invoke_args.size() == 4);
            check(actions[0].invoke_args[0].type == RhsValueType::VAR_REF);
            check(actions[0].invoke_args[1].type == RhsValueType::STRING);
            check(actions[0].invoke_args[2].type == RhsValueType::BOOLEAN);
            check(actions[0].invoke_args[3].type == RhsValueType::NUMERIC);
        }

        it("parses native call in assignment") {
            auto actions = RhsParser::parse(
                R"(update $user { score = pluginMetric($item.price, 2) })", make_bindings());

            check(actions.size() == 1);
            check(actions[0].type == RhsActionType::UPDATE);
            check(actions[0].assignments.size() == 1);
            check(actions[0].assignments[0].type == RhsValueType::NATIVE_CALL);
            check(actions[0].assignments[0].native_call_name == "pluginMetric");
            check(actions[0].assignments[0].native_call_args.size() == 2);
        }
    }

    // ========================================================================
    // Field Assignment Value Types
    // ========================================================================

    group("value types") {
        it("parses string literal") {
            auto actions = RhsParser::parse(
                R"(update $user { name = "bob" })", make_bindings());

            auto const& a = actions[0].assignments[0];
            check(a.type == RhsValueType::STRING);
            check(a.string_literal == "bob");
        }

        it("parses boolean true") {
            auto actions = RhsParser::parse(
                R"(update $user { active = true })", make_bindings());

            auto const& a = actions[0].assignments[0];
            check(a.type == RhsValueType::BOOLEAN);
            check(a.string_literal == "true");
        }

        it("parses boolean false") {
            auto actions = RhsParser::parse(
                R"(update $user { active = false })", make_bindings());

            auto const& a = actions[0].assignments[0];
            check(a.type == RhsValueType::BOOLEAN);
            check(a.string_literal == "false");
        }

        it("parses variable reference") {
            auto actions = RhsParser::parse(
                R"(update $user { score = $item })", make_bindings());

            auto const& a = actions[0].assignments[0];
            check(a.type == RhsValueType::VAR_REF);
            check(a.var_ref == "$item");
        }

        it("parses variable field reference") {
            auto actions = RhsParser::parse(
                R"(update $user { score = $item.price })", make_bindings());

            auto const& a = actions[0].assignments[0];
            check(a.type == RhsValueType::VAR_REF);
            check(a.var_ref == "$item.price");
        }

        it("parses numeric expression") {
            auto actions = RhsParser::parse(
                R"(update $user { score = $item.price * 2 + 1 })",
                make_bindings());

            auto const& a = actions[0].assignments[0];
            check(a.type == RhsValueType::NUMERIC);
            check(a.numeric_expr != nullptr);
        }
    }

    // ========================================================================
    // Control Flow
    // ========================================================================

    group("if") {
        it("parses if with then block") {
            auto actions = RhsParser::parse(
                R"(if $user.age > 18 { update $user { status = "adult" } })",
                make_bindings());

            check(actions.size() == 1);
            check(actions[0].type == RhsActionType::IF);
            check(actions[0].condition != nullptr);
            check(actions[0].then_actions.size() == 1);
            check(actions[0].else_actions.empty());
        }

        it("parses if/else") {
            auto actions = RhsParser::parse(
                R"(if $user.age > 18 { update $user { status = "adult" } }
                   else { update $user { status = "minor" } })",
                make_bindings());

            check(actions.size() == 1);
            check(actions[0].then_actions.size() == 1);
            check(actions[0].else_actions.size() == 1);
        }

        it("parses else if chain") {
            auto actions = RhsParser::parse(
                R"(if $user.age > 60 { halt }
                   else if $user.age > 18 { halt }
                   else { halt })",
                make_bindings());

            check(actions.size() == 1);
            // else branch contains a nested IF
            check(actions[0].else_actions.size() == 1);
            check(actions[0].else_actions[0].type == RhsActionType::IF);
            check(actions[0].else_actions[0].else_actions.size() == 1);
        }
    }

    group("for") {
        it("parses for with variable iteration") {
            auto actions = RhsParser::parse(
                R"(for $x in $order { halt })",
                make_bindings());

            check(actions.size() == 1);
            check(actions[0].type == RhsActionType::FOR);
            check(actions[0].iter_var == "$x");
            check(actions[0].iter_source_var == "$order");
            check(actions[0].iter_source_field.empty());
            check(actions[0].body_actions.size() == 1);
        }

        it("parses for with field iteration") {
            auto actions = RhsParser::parse(
                R"(for $x in $order.items { halt })",
                make_bindings());

            check(actions.size() == 1);
            check(actions[0].type == RhsActionType::FOR);
            check(actions[0].iter_var == "$x");
            check(actions[0].iter_source_var == "$order");
            check(actions[0].iter_source_field == "items");
            check(actions[0].body_actions.size() == 1);
        }

        it("parses for with value list") {
            auto actions = RhsParser::parse(
                R"(for $x in ($user, $item, $order) { halt })",
                make_bindings());

            check(actions.size() == 1);
            check(actions[0].type == RhsActionType::FOR);
            check(actions[0].iter_source_list.size() == 3);
            check(actions[0].iter_source_list[0] == "$user");
            check(actions[0].iter_source_list[1] == "$item");
            check(actions[0].iter_source_list[2] == "$order");
        }
    }

    group("while") {
        it("parses while loop") {
            auto actions = RhsParser::parse(
                R"(while $user.count > 0 { update $user { count = $user.count - 1 } })",
                make_bindings());

            check(actions.size() == 1);
            check(actions[0].type == RhsActionType::WHILE);
            check(actions[0].condition != nullptr);
            check(actions[0].body_actions.size() == 1);
            check(actions[0].max_iterations == 1000);
        }
    }

    group("switch") {
        it("parses switch with cases and default") {
            auto actions = RhsParser::parse(
                R"(switch $user.level {
                       case 1 { halt }
                       case 2 { halt }
                       default { halt }
                   })",
                make_bindings());

            check(actions.size() == 1);
            check(actions[0].type == RhsActionType::SWITCH);
            check(actions[0].switch_expr != nullptr);
            check(actions[0].switch_cases.size() == 3);
            check(!actions[0].switch_cases[0].is_default);
            check(!actions[0].switch_cases[1].is_default);
            check(actions[0].switch_cases[2].is_default);
        }
    }

    group("break and continue") {
        it("parses break") {
            auto actions = RhsParser::parse(
                R"(for $x in $order.items { break })",
                make_bindings());

            check(actions[0].body_actions.size() == 1);
            check(actions[0].body_actions[0].type == RhsActionType::BREAK);
        }

        it("parses continue") {
            auto actions = RhsParser::parse(
                R"(for $x in $order.items { continue })",
                make_bindings());

            check(actions[0].body_actions.size() == 1);
            check(actions[0].body_actions[0].type == RhsActionType::CONTINUE);
        }
    }

    // ========================================================================
    // Multiple Actions
    // ========================================================================

    group("sequences") {
        it("parses multiple actions in sequence") {
            auto actions = RhsParser::parse(
                R"(update $user { age = 30 }
                   insert Log { msg = "updated" }
                   retract $item)",
                make_bindings());

            check(actions.size() == 3);
            check(actions[0].type == RhsActionType::UPDATE);
            check(actions[1].type == RhsActionType::INSERT);
            check(actions[2].type == RhsActionType::RETRACT);
        }
    }

    // ========================================================================
    // Error Cases
    // ========================================================================

    group("errors") {
        it("returns empty on invalid syntax") {
            std::string err;
            auto actions = RhsParser::parse(
                R"(gibberish stuff)", make_bindings(), &err);

            check(actions.empty());
            check(!err.empty());
        }

        it("reports missing brace") {
            std::string err;
            auto actions = RhsParser::parse(
                R"(update $user { x = 1)", make_bindings(), &err);

            check(actions.empty());
            check(!err.empty());
        }
    }
}
