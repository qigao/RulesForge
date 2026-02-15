#include "tinytest.h"
#include "js_ast_builder.hpp"
#include <iostream>

suite("JSAstBuilder") {
    static JSAstBuilder builder;

    group("Syntax validation") {
        it("validates correct JavaScript") {
            std::string error_message;
            check(builder.validate_syntax("var x = 1; console.log(x);", error_message));
            check(error_message.empty());
        }

        it("rejects invalid JavaScript") {
            std::string error_message;
            check_false(builder.validate_syntax("var x = ;", error_message));
            check_false(error_message.empty());
            std::cout << "Expected error: " << error_message << std::endl;
        }
    }

    group("Function call extraction") {
        it("extracts rfl method calls") {
            std::string js_code = R"(
                rfl.insert({type: "Adult", name: p.name});
                rfl.retract(oldFact);
                console.log("test");
            )";

            auto calls = builder.extract_function_calls(js_code);
            check_size_ge(calls.size(), 2);

            bool found_insert = false, found_retract = false;
            for (auto const& call : calls) {
                if (call.object_name == "rfl" && call.method_name == "insert") {
                    found_insert = true;
                    check_false(call.arguments.empty());
                    std::cout << "Found rfl.insert with args: " << call.arguments[0] << std::endl;
                }
                if (call.object_name == "rfl" && call.method_name == "retract") {
                    found_retract = true;
                }
            }

            check(found_insert);
            check(found_retract);
        }
    }

    group("Variable extraction") {
        it("extracts member expressions") {
            std::string js_code = "rfl.insert({type: 'Adult', name: p.name, age: p.age});";

            auto vars = builder.extract_variables(js_code);

            bool found_p_name = false, found_p_age = false;
            for (auto const& var : vars) {
                std::cout << "Found variable: " << var.name;
                if (var.field) std::cout << "." << *var.field;
                std::cout << std::endl;

                if (var.name == "p" && var.field && *var.field == "name") {
                    found_p_name = true;
                }
                if (var.name == "p" && var.field && *var.field == "age") {
                    found_p_age = true;
                }
            }

            check(found_p_name);
            check(found_p_age);
        }
    }

    group("Full AST parsing") {
        it("parses valid JavaScript into AST") {
            std::string js_code = "rfl.insert({type: 'Adult', name: p.name});";

            auto ast = builder.parse(js_code);

            check(ast.is_valid);
            check(ast.error_message.empty());
            check_false(ast.statements.empty());

            std::cout << "AST contains " << ast.statements.size() << " statements" << std::endl;
        }
    }
}
