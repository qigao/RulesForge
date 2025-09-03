#include "catch2/catch_all.hpp"
#include "drools_parser.hpp"
#include "knowledge_base.hpp"

ParsingResult parse_drl(std::string const& drl) {
    ParsingResult result;
    build_knowledge_base(drl, result, "string.drl");
    return result;
}

parser_state parse_drl_success(std::string const& drl) {
    ParsingResult result;
    auto kb = build_knowledge_base(drl, result);
    REQUIRE(result.success);
    for (auto const& err : result.errors) FAIL(err.to_string());
    REQUIRE(kb != nullptr);
    return kb->get_parser_state();
}

TEST_CASE("Parser: Basic Rule Parsing", "[parser]") {
    std::string drl = R"(
        declare Person age : int end
        declare Adult end
        rule "Simple Rule"
        when
            $p : Person(age > 30)
        then
            drools.insert({type: "Adult"});
        end
    )";
    auto state = parse_drl_success(drl);
    REQUIRE(state.parsed_rules.size() == 1);
    auto const& rule = state.parsed_rules[0];
    CHECK(rule.name == "Simple Rule");
    REQUIRE(rule.condition_groups.size() == 1);
    REQUIRE(rule.condition_groups[0].size() == 1);
    auto const& pattern = rule.condition_groups[0][0];
    CHECK(pattern.binding == "$p");
    CHECK(pattern.fact_type == "Person");
    REQUIRE(pattern.constraint_root != nullptr);
}

TEST_CASE("Parser: Rule Attributes", "[parser]") {
    std::string drl = R"(
        rule "Rule with Attributes"
        salience 100
        agenda-group "TEST_GROUP"
        when
            // The when block is now present, satisfying the grammar
        then
        end
    )";
    auto state = parse_drl_success(drl);
    REQUIRE(state.parsed_rules.size() == 1);
    auto const& rule = state.parsed_rules[0];
    CHECK(rule.name == "Rule with Attributes");
    CHECK(rule.salience == 100);
    REQUIRE(rule.agenda_group.has_value());
    CHECK(rule.agenda_group.value() == "TEST_GROUP");
}

TEST_CASE("Parser: Complex LHS with 'not' and 'exists'", "[parser]") {
    std::string drl = R"(
        declare Person end
        declare Account status : String end
        declare Order amount : int end
        rule "Complex LHS"
        when
            $p : Person()
            not (Account(status == "closed"))
            exists (Order(amount > 1000))
        then
        end
    )";
    auto state = parse_drl_success(drl);
    REQUIRE(state.parsed_rules.size() == 1);
    REQUIRE(state.parsed_rules[0].condition_groups.size() == 1);
    auto const& conditions = state.parsed_rules[0].condition_groups[0];
    REQUIRE(conditions.size() == 3);
    CHECK(conditions[0].type == PatternType::STANDARD);
    CHECK(conditions[0].binding == "$p");
    REQUIRE(conditions[1].type == PatternType::NOT);
    REQUIRE(!conditions[1].nested_patterns.empty());
    CHECK(conditions[1].nested_patterns[0].fact_type == "Account");
    REQUIRE(conditions[2].type == PatternType::EXISTS);
    REQUIRE(!conditions[2].nested_patterns.empty());
    CHECK(conditions[2].nested_patterns[0].fact_type == "Order");
}

TEST_CASE("Parser: 'from accumulate' and 'from unnest'", "[parser]") {
    std::string drl = R"(
        declare TotalValue result:double end
        declare Purchase value: double end
        declare Item end
        declare Order items: java.util.List end
        rule "Accumulate and Unnest"
        when
            $order : Order()
            $total : TotalValue() from accumulate (
                $p: Purchase(),
                sum($p.value)
            )
            $item: Item() from unnest( $order.items )
        then
        end
    )";
    auto state = parse_drl_success(drl);
    REQUIRE(state.parsed_rules.size() == 1);
    auto const& conditions = state.parsed_rules[0].condition_groups[0];
    REQUIRE(conditions.size() == 3);

    auto const& accumulate_pattern = conditions[1];
    CHECK(accumulate_pattern.binding == "$total");
    REQUIRE(std::holds_alternative<ParsedAccumulate>(accumulate_pattern.source));
    auto const& acc_info = std::get<ParsedAccumulate>(accumulate_pattern.source);
    CHECK(acc_info.function == "sum");
    CHECK(acc_info.field == "$p.value");
    // Check that the analyzer correctly extracted the field name from the binding
    CHECK(acc_info.accumulate_field_name == "value");

    auto const& unnest_pattern = conditions[2];
    CHECK(unnest_pattern.binding == "$item");
    REQUIRE(std::holds_alternative<ParsedUnnest>(unnest_pattern.source));
    auto const& unnest_info = std::get<ParsedUnnest>(unnest_pattern.source);
    CHECK(unnest_info.source_binding == "$order");
    CHECK(unnest_info.source_field == "items");
}

TEST_CASE("Parser: Declarations and Globals", "[parser]") {
    std::string drl = R"(
        package com.example;
        import com.example.model.Person;
        global java.util.List results;
        declare Person
            name : String
            age : int
        end
    )";
    auto state = parse_drl_success(drl);
    REQUIRE(state.parsed_declarations.size() == 1);
    CHECK(state.parsed_declarations[0].type_name == "com.example.Person");
    REQUIRE(state.parsed_declarations[0].fields.size() == 2);
    CHECK(state.parsed_declarations[0].fields[0].name == "name");
    REQUIRE(state.parsed_globals.size() == 1);
    CHECK(state.parsed_globals[0].type == "java.util.List");
    CHECK(state.parsed_globals[0].name == "results");
    REQUIRE(state.parsed_imports.size() == 1);
    CHECK(state.parsed_imports[0] == "com.example.model.Person");
}

TEST_CASE("Parser: Parameterized Query", "[parser]") {
    std::string drl = R"(
        declare NameHolder value:String end
        declare Person
            name : String
        end

        query findPerson(NameHolder $name)
            $p : Person(name == $name.value)
        end
    )";
    auto state = parse_drl_success(drl);
    REQUIRE(state.parsed_queries.size() == 1);
    auto const& query = state.parsed_queries[0];
    CHECK(query.name == "findPerson");
    CHECK(query.parameter_count == 1);
    REQUIRE(query.patterns.size() == 2);
    CHECK(query.patterns[0].fact_type == "NameHolder");
    CHECK(query.patterns[0].binding == "$name");
    CHECK(query.patterns[1].binding == "$p");
    CHECK(query.patterns[1].fact_type == "Person");
}

TEST_CASE("Parser: Native Temporal Operators 'after' and 'within'", "[parser][cep]") {
    // This DRL uses the exact syntax we want to support.
    std::string drl = R"(
        declare LoginEvent timestamp: long end

        rule "Native CEP Operators"
        when
            $e1: LoginEvent()
            $e2: LoginEvent(
                timestamp after $e1.timestamp,
                within 60s of $e1
            )
        then
        end
    )";

    // The parse_string helper will fail until the grammar is correct.
    auto state = parse_drl_success(drl);

    // After fixing the grammar, these checks will validate the AstBuilder.
    REQUIRE(state.parsed_rules.size() == 1);
    REQUIRE(state.parsed_rules[0].condition_groups[0].size() == 2);

    auto const& pattern2 = state.parsed_rules[0].condition_groups[0][1];
    REQUIRE(pattern2.constraint_root);
    REQUIRE(pattern2.constraint_root->children.size() == 2);

    // Check the 'after' constraint
    auto const& after_constraint_node = pattern2.constraint_root->children[0];
    REQUIRE(after_constraint_node->constraint.temporal_constraint.has_value());
    auto const& after_tc = *after_constraint_node->constraint.temporal_constraint;
    CHECK(after_tc.op == "after");
    CHECK(after_tc.lhs_field == "timestamp");
    CHECK(after_tc.rhs_binding_and_field.first == "$e1");
    CHECK(after_tc.rhs_binding_and_field.second == "timestamp");

    // Check the 'within' constraint
    auto const& within_constraint_node = pattern2.constraint_root->children[1];
    REQUIRE(within_constraint_node->constraint.temporal_constraint.has_value());
    auto const& within_tc = *within_constraint_node->constraint.temporal_constraint;
    CHECK(within_tc.op == "within");
    CHECK(within_tc.lhs_field == "timestamp");   // The builder hardcodes this for 'within'
    CHECK(within_tc.rhs_binding_and_field.first == "$e1");
    CHECK(within_tc.rhs_binding_and_field.second == "timestamp");   // The builder hardcodes this for 'within'
    CHECK(within_tc.window_ms == 60000);
}
