#include "tinytest.h"
#include "rfl_parser.hpp"
#include "knowledge_base.hpp"

ParsingResult parse_drl(std::string const& drl) {
    ParsingResult result;
    build_knowledge_base(drl, result, "string.rfl");
    return result;
}

parser_state parse_drl_success(std::string const& drl) {
    ParsingResult result;
    auto kb = build_knowledge_base(drl, result);
    if (!result.success) {
        for (auto const& err : result.errors) {
            throw std::runtime_error("RFL parsing failed: " + err.to_string());
        }
        throw std::runtime_error("RFL parsing failed: Unknown error");
    }
    if (!kb) { throw std::runtime_error("KnowledgeBase is null"); }
    return kb->get_parser_state();
}

suite("Parser") {
    group("Basic Rule Parsing") {
        it("parses a simple rule") {
            std::string drl = R"(
                declare Person age : int end
                declare Adult end
                rule "Simple Rule"
                when
                    $p : Person(age > 30)
                then
                    rfl.insert({type: "Adult"});
                end
            )";
            auto state = parse_drl_success(drl);
            check(state.parsed_rules.size() == 1);
            auto const& rule = state.parsed_rules[0];
            check(rule.name == "Simple Rule");
            check(rule.condition_groups.size() == 1);
            check(rule.condition_groups[0].size() == 1);
            auto const& pattern = rule.condition_groups[0][0];
            check(pattern.binding == "$p");
            check(pattern.fact_type == "Person");
            check(pattern.constraint_root != nullptr);
        }
    }

    group("Rule Attributes") {
        it("parses rule attributes") {
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
            check(state.parsed_rules.size() == 1);
            auto const& rule = state.parsed_rules[0];
            check(rule.name == "Rule with Attributes");
            check(rule.salience == 100);
            check(rule.agenda_group.has_value());
            check(rule.agenda_group.value() == "TEST_GROUP");
        }
    }

    group("Complex LHS with 'not' and 'exists'") {
        it("parses complex LHS patterns") {
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
            check(state.parsed_rules.size() == 1);
            check(state.parsed_rules[0].condition_groups.size() == 1);
            auto const& conditions = state.parsed_rules[0].condition_groups[0];
            check(conditions.size() == 3);
            check(conditions[0].type == PatternType::STANDARD);
            check(conditions[0].binding == "$p");
            check(conditions[1].type == PatternType::NOT);
            check(!conditions[1].nested_patterns.empty());
            check(conditions[1].nested_patterns[0].fact_type == "Account");
            check(conditions[2].type == PatternType::EXISTS);
            check(!conditions[2].nested_patterns.empty());
            check(conditions[2].nested_patterns[0].fact_type == "Order");
        }
    }

    group("'from accumulate' and 'from unnest'") {
        it("parses accumulate and unnest patterns") {
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
            check(state.parsed_rules.size() == 1);
            auto const& conditions = state.parsed_rules[0].condition_groups[0];
            check(conditions.size() == 3);

            auto const& accumulate_pattern = conditions[1];
            check(accumulate_pattern.binding == "$total");
            check(std::holds_alternative<ParsedAccumulate>(accumulate_pattern.source));
            auto const& acc_info = std::get<ParsedAccumulate>(accumulate_pattern.source);
            check(acc_info.function == "sum");
            check(acc_info.field == "$p.value");
            check(acc_info.accumulate_field_name == "value");

            auto const& unnest_pattern = conditions[2];
            check(unnest_pattern.binding == "$item");
            check(std::holds_alternative<ParsedUnnest>(unnest_pattern.source));
            auto const& unnest_info = std::get<ParsedUnnest>(unnest_pattern.source);
            check(unnest_info.source_binding == "$order");
            check(unnest_info.source_field == "items");
        }
    }

    group("Declarations and Globals") {
        it("parses declarations and globals") {
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
            check(state.parsed_declarations.size() == 1);
            check(state.parsed_declarations[0].type_name == "com.example.Person");
            check(state.parsed_declarations[0].fields.size() == 2);
            check(state.parsed_declarations[0].fields[0].name == "name");
            check(state.parsed_globals.size() == 1);
            check(state.parsed_globals[0].type == "java.util.List");
            check(state.parsed_globals[0].name == "results");
            check(state.parsed_imports.size() == 1);
            check(state.parsed_imports[0] == "com.example.model.Person");
        }
    }

    group("Parameterized Query") {
        it("parses parameterized queries") {
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
            check(state.parsed_queries.size() == 1);
            auto const& query = state.parsed_queries[0];
            check(query.name == "findPerson");
            check(query.parameter_count == 1);
            check(query.patterns.size() == 2);
            check(query.patterns[0].fact_type == "NameHolder");
            check(query.patterns[0].binding == "$name");
            check(query.patterns[1].binding == "$p");
            check(query.patterns[1].fact_type == "Person");
        }
    }

    group("Native Temporal Operators 'after' and 'within'") {
        it("parses CEP temporal operators") {
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

            auto state = parse_drl_success(drl);

            check(state.parsed_rules.size() == 1);
            check(state.parsed_rules[0].condition_groups[0].size() == 2);

            auto const& pattern2 = state.parsed_rules[0].condition_groups[0][1];
            check(pattern2.constraint_root != nullptr);
            check(pattern2.constraint_root->children.size() == 2);

            auto const& after_constraint_node = pattern2.constraint_root->children[0];
            check(after_constraint_node->constraint.temporal_constraint.has_value());
            auto const& after_tc = *after_constraint_node->constraint.temporal_constraint;
            check(after_tc.op == TemporalOp::After);
            check(after_tc.lhs_field == "timestamp");
            check(after_tc.rhs_binding_and_field.first == "$e1");
            check(after_tc.rhs_binding_and_field.second == "timestamp");

            auto const& within_constraint_node = pattern2.constraint_root->children[1];
            check(within_constraint_node->constraint.temporal_constraint.has_value());
            auto const& within_tc = *within_constraint_node->constraint.temporal_constraint;
            check(within_tc.op == TemporalOp::Within);
            check(within_tc.lhs_field == "timestamp");
            check(within_tc.rhs_binding_and_field.first == "$e1");
            check(within_tc.rhs_binding_and_field.second == "timestamp");
            check(within_tc.window_ms == 60000);
        }
    }
}
