#include "script/script_catalog.hpp"
#include "rhs_parser.hpp"
#include <tinytest.hpp>

using namespace rulesforge;
suite("Script CALL syntax and lexical scopes") {
  it("rejects a statically mistyped literal before a Program can publish") {
    ScriptCatalog catalog;
    ScriptBinding binding;
    binding.name = "price";
    ScriptShape number;
    number.kind = ScriptKind::Number;
    binding.parameters.push_back(number);
    catalog.bindings.push_back(binding);
    std::string error;
    auto integer = RhsParser::parse("call \"price\"(1) into $value", {}, &error);
    check(error.empty());
    check_throws_as(catalog.bind(integer), std::runtime_error);
    auto decimal = RhsParser::parse("call \"price\"(1.0) into $value", {}, &error);
    check(error.empty());
    check_nothrow(catalog.bind(decimal));
  }
  it("parses typed references and keeps CALL contextual") {
    std::string error;
    auto actions = RhsParser::parse("call \"price\"($order.amount, 7, true) into $price update "
                                    "$order { into = $price.total, call = 1 }",
                                    {{"$order", 0}}, &error);
    info("%s", error.c_str());
    check(error.empty());
    check_equal(actions.size(), size_t(2));
    check(actions[0].type == RhsActionType::CALL);
    check_equal(actions[0].script_arguments.size(), size_t(3));
  }
  it("rejects result shadowing and treating a result as a Fact") {
    for (auto text :
         {"call \"price\"(1) into $order", "call \"price\"(1) into $price retract $price",
          "call \"price\"(1) into $price call \"price\"(2) into $price"}) {
      std::string error;
      auto actions = RhsParser::parse(text, {{"$order", 0}}, &error);
      check(!error.empty());
      check(actions.empty());
    }
  }
  it("rejects whole Facts and undeclared arguments") {
    for (auto text :
         {"call \"price\"($order) into $price", "call \"price\"($missing.amount) into $price"}) {
      std::string error;
      RhsParser::parse(text, {{"$order", 0}}, &error);
      check(!error.empty());
    }
  }
  it("does not let branch or loop results escape") {
    for (auto text :
         {"if true {call \"price\"(1) into $price} update $order {total=$price.total}",
          "while false {call \"price\"(1) into $price} update $order {total=$price.total}",
          "if true {call \"price\"(1) into $price} else {update $order {total=$price.total}}",
          "switch 1 {case 1 {call \"price\"(1) into $price} default {update $order "
          "{total=$price.total}}}"}) {
      std::string error;
      RhsParser::parse(text, {{"$order", 0}}, &error);
      check(!error.empty());
    }
  }
}
