#include "catch2/catch_test_macros.hpp"
#include "js_ast_builder.hpp"
#include <iostream>

TEST_CASE("JSAstBuilder: Basic Functionality", "[js_parser]") {
    JSAstBuilder builder;
    
    SECTION("Syntax validation") {
        std::string error_message;
        
        // Valid JavaScript
        CHECK(builder.validate_syntax("var x = 1; console.log(x);", error_message));
        CHECK(error_message.empty());
        
        // Invalid JavaScript
        CHECK_FALSE(builder.validate_syntax("var x = ;", error_message));
        CHECK_FALSE(error_message.empty());
        std::cout << "Expected error: " << error_message << std::endl;
    }
    
    SECTION("Function call extraction") {
        std::string js_code = R"(
            rfl.insert({type: "Adult", name: p.name});
            rfl.retract(oldFact);
            console.log("test");
        )";
        
        auto calls = builder.extract_function_calls(js_code);
        
        REQUIRE(calls.size() >= 2); // At least rfl.insert and rfl.retract
        
        bool found_insert = false, found_retract = false;
        for (const auto& call : calls) {
            if (call.object_name == "rfl" && call.method_name == "insert") {
                found_insert = true;
                CHECK_FALSE(call.arguments.empty());
                std::cout << "Found rfl.insert with args: " << call.arguments[0] << std::endl;
            }
            if (call.object_name == "rfl" && call.method_name == "retract") {
                found_retract = true;
            }
        }
        
        CHECK(found_insert);
        CHECK(found_retract);
    }
    
    SECTION("Variable extraction") {
        std::string js_code = "rfl.insert({type: 'Adult', name: p.name, age: p.age});";
        
        auto vars = builder.extract_variables(js_code);
        
        // Should find p.name and p.age
        bool found_p_name = false, found_p_age = false;
        for (const auto& var : vars) {
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
        
        CHECK(found_p_name);
        CHECK(found_p_age);
    }
    
    SECTION("Full AST parsing") {
        std::string js_code = "rfl.insert({type: 'Adult', name: p.name});";
        
        auto ast = builder.parse(js_code);
        
        CHECK(ast.is_valid);
        CHECK(ast.error_message.empty());
        CHECK_FALSE(ast.statements.empty());
        
        std::cout << "AST contains " << ast.statements.size() << " statements" << std::endl;
    }
}

