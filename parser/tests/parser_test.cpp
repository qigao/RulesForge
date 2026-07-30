// parser_test.cpp
// Verifies the raw output of the Lemon parser (rfl_parse_lemon) — the AST
// stored in parser_state — WITHOUT running semantic analysis or building a
// KnowledgeBase.  Any test here should only observe what the parser itself
// produces: token recognition, grammar rules, and the shape of the resulting
// parsed_rules / parsed_declarations / parsed_queries / etc.

#include "rfl_parser_impl.hpp"    // rfl_parse_lemon
#include "core/rfl_parser_state.hpp"
#include "core/parsed_rule.hpp"
#include "core/errors.hpp"
#include "tinytest.h"

#include <cmath>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Parse a DRL string and expect success.  Returns the parser_state.
// Throws on any parse error so the test fails immediately with a useful message.
static parser_state parse_success(std::string const& drl,
                                  std::string const& source = "test.rfl") {
    std::vector<StructuredError> errors;
    parser_state state = rfl_parse_lemon(drl, source, errors);
    if (!errors.empty()) {
        std::string msg = "Parse failed:\n";
        for (auto const& e : errors) msg += "  " + e.to_string() + "\n";
        throw std::runtime_error(msg);
    }
    return state;
}

// Parse a DRL string and expect at least one error.
static std::vector<StructuredError> parse_expect_errors(std::string const& drl,
                                                        std::string const& source = "test.rfl") {
    std::vector<StructuredError> errors;
    rfl_parse_lemon(drl, source, errors);
    return errors;
}

// ---------------------------------------------------------------------------
// Suite
// ---------------------------------------------------------------------------

suite("Parser") {
    // ------------------------------------------------------------------
    group("Basic Rule Parsing") {
        it("parses a simple rule") {
            std::string drl = R"(
                declare Person age : int end
                declare Adult end
                rule "Simple Rule"
                when
                    $p : Person(age > 30)
                then
                    insert Adult { }
                end
            )";
            auto state = parse_success(drl);
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

        it("preserves greater-than constraints inside a single pattern") {
            std::string drl = R"(
                declare Order
                    quantity : int
                    finalPrice : double
                end
                rule "Round Down Payment"
                when
                    $order : Order(quantity > 2, finalPrice > 0.01)
                then
                end
            )";

            auto state = parse_success(drl);
            check(state.parsed_rules.size() == 1);

            auto const& pattern = state.parsed_rules[0].condition_groups[0][0];
            check(pattern.constraint_root != nullptr);
            check(pattern.constraint_root->type == NodeType::AND);
            check(pattern.constraint_root->children.size() == 2);

            auto const& quantity_node = pattern.constraint_root->children[0];
            auto const& final_price_node = pattern.constraint_root->children[1];
            check(quantity_node->type == NodeType::LEAF);
            check(final_price_node->type == NodeType::LEAF);

            check(quantity_node->constraint.left_field == "quantity");
            check(quantity_node->constraint.op == CompareOp::GT);
            check(quantity_node->constraint.right_literal.has_value());
            check(std::holds_alternative<int64_t>(*quantity_node->constraint.right_literal));
            check(std::get<int64_t>(*quantity_node->constraint.right_literal) == 2);

            check(final_price_node->constraint.left_field == "finalPrice");
            check(final_price_node->constraint.op == CompareOp::GT);
            check(final_price_node->constraint.right_literal.has_value());
            check(std::holds_alternative<double>(*final_price_node->constraint.right_literal));
            check(std::abs(std::get<double>(*final_price_node->constraint.right_literal) - 0.01) < 1e-12);
        }

        it("preserves boolean constraint literals as booleans") {
            auto state = parse_success(R"(
                declare Feature active : bool end
                rule "Enabled Feature"
                when
                    Feature(active == true)
                then
                end
            )");

            auto const& constraint =
                state.parsed_rules[0].condition_groups[0][0].constraint_root->constraint;
            check(constraint.right_literal.has_value());
            check(std::holds_alternative<bool>(*constraint.right_literal));
            check(std::get<bool>(*constraint.right_literal));
        }
    }

    // ------------------------------------------------------------------
    group("Rule Attributes") {
        it("parses salience and agenda-group attributes") {
            std::string drl = R"(
                rule "Rule with Attributes"
                salience 100
                agenda-group "TEST_GROUP"
                when
                then
                end
            )";
            auto state = parse_success(drl);
            check(state.parsed_rules.size() == 1);
            auto const& rule = state.parsed_rules[0];
            check(rule.name == "Rule with Attributes");
            check(rule.salience == 100);
            check(rule.agenda_group.has_value());
            check(rule.agenda_group.value() == "TEST_GROUP");
        }
    }

    // ------------------------------------------------------------------
    group("Complex LHS with 'not' and 'exists'") {
        it("parses NOT and EXISTS wrapper patterns") {
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
            auto state = parse_success(drl);
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

    // ------------------------------------------------------------------
    group("'from accumulate' and 'from unnest'") {
        it("parses accumulate and unnest patterns") {
            std::string drl = R"(
                declare TotalValue result:double end
                declare Purchase value: double end
                declare Item end
                declare Order items: List end
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
            auto state = parse_success(drl);
            check(state.parsed_rules.size() == 1);
            auto const& conditions = state.parsed_rules[0].condition_groups[0];
            check(conditions.size() == 3);

            auto const& accumulate_pattern = conditions[1];
            check(accumulate_pattern.binding == "$total");
            check(std::holds_alternative<ParsedAccumulate>(accumulate_pattern.source));
            auto const& acc_info = std::get<ParsedAccumulate>(accumulate_pattern.source);
            check(acc_info.function == "sum");
            check(acc_info.field == "$p.value");
            // acc_info.accumulate_field_name is resolved by SemanticAnalyzer, not the parser.
            // It is empty at this stage — verified by the semantic test suite instead.

            auto const& unnest_pattern = conditions[2];
            check(unnest_pattern.binding == "$item");
            check(std::holds_alternative<ParsedUnnest>(unnest_pattern.source));
            auto const& unnest_info = std::get<ParsedUnnest>(unnest_pattern.source);
            check(unnest_info.source_binding == "$order");
            check(unnest_info.source_field == "items");
        }
        
        it("parses accumulate pattern with window declarations") {
            std::string drl = R"(
                rule "Accumulate Window"
                when
                    $c : CountResult() from accumulate(
                        Event() over window:length(5),
                        count()
                    )
                    $d : CountResult() from accumulate(
                        Event() over window:time(60s),
                        count()
                    )
                then
                end
            )";
            auto state = parse_success(drl);
            check(state.parsed_rules.size() == 1);
            auto const& conditions = state.parsed_rules[0].condition_groups[0];
            check(conditions.size() == 2);
            
            auto const& acc1 = std::get<ParsedAccumulate>(conditions[0].source);
            check(acc1.source_pattern != nullptr);
            check(acc1.source_pattern->fact_type == "Event");
            check(acc1.source_pattern->window_info.has_value());
            check(acc1.source_pattern->window_info->type == WindowType::LENGTH);
            check(acc1.source_pattern->window_info->size == 5);
            
            auto const& acc2 = std::get<ParsedAccumulate>(conditions[1].source);
            check(acc2.source_pattern != nullptr);
            check(acc2.source_pattern->fact_type == "Event");
            check(acc2.source_pattern->window_info.has_value());
            check(acc2.source_pattern->window_info->type == WindowType::TIME);
            check(acc2.source_pattern->window_info->size == 60000);
        }

        it("parses time window with bare millisecond integer and keyword in package name") {
            std::string drl = R"(
                package test.sliding.time;
                rule "Accumulate Time Window"
                when
                    $c : CountResult() from accumulate(
                        Event() over window:time(50),
                        count()
                    )
                then
                end
            )";
            auto state = parse_success(drl);
            check(state.package_name == "test.sliding.time");
            check(state.parsed_rules.size() == 1);
            auto const& acc = std::get<ParsedAccumulate>(state.parsed_rules[0].condition_groups[0][0].source);
            check(acc.source_pattern != nullptr);
            check(acc.source_pattern->window_info.has_value());
            check(acc.source_pattern->window_info->type == WindowType::TIME);
            check(acc.source_pattern->window_info->size == 50);
        }
    }



    // ------------------------------------------------------------------
    group("Declarations and Globals") {
        it("parses declarations, globals, package and imports") {
            std::string drl = R"(
                package com.example;
                import com.example.model.Person;
                global List results;
                declare Person
                    name : String
                    age : int
                end
            )";
            auto state = parse_success(drl);
            check(state.package_name == "com.example");
            check(state.parsed_declarations.size() == 1);
            // The raw parser stores the simple name; the package prefix is
            // applied later by semantic analysis / AstTransformer, so we only
            // check that the raw name was captured.
            check(state.parsed_declarations[0].type_name == "Person");
            check(state.parsed_declarations[0].fields.size() == 2);
            check(state.parsed_declarations[0].fields[0].name == "name");
            check(state.parsed_globals.size() == 1);
            check(state.parsed_globals[0].type == "List");
            check(state.parsed_globals[0].name == "results");
            check(state.parsed_imports.size() == 1);
            check(state.parsed_imports[0] == "com.example.model.Person");
        }

        it("parses implicit schema imports") {
            std::string drl = R"(
                import "market.schema";
                rule "schema import only"
                when
                then
                end
            )";

            auto state = parse_success(drl, "rules.rfl");
            check(state.schema_imports.size() == 1);
            check(state.schema_imports[0].path == "market.schema");
            check(state.schema_imports[0].source_name == "rules.rfl");
        }

        it("parses explicit schema imports") {
            std::string drl = R"(
                import schema "market.schema";
                import schema "orders.schema";
                rule "schema import only"
                when
                then
                end
            )";

            auto state = parse_success(drl, "rules.rfl");
            check(state.schema_imports.size() == 2);
            check(state.schema_imports[0].path == "market.schema");
            check(state.schema_imports[0].source_name == "rules.rfl");
            check(state.schema_imports[1].path == "orders.schema");
            check(state.schema_imports[1].source_name == "rules.rfl");
        }

        it("rejects tbe schema imports") {
            std::string drl = R"(
                import "market.tbe";
                rule "schema import only"
                when
                then
                end
            )";

            auto errors = parse_expect_errors(drl, "rules.rfl");
            check(!errors.empty());
            check_str_contains(errors.front().message.c_str(), ".schema");
        }

        it("rejects implementation-specific schema import qualifiers") {
            std::string drl = R"(
                import databind "market.schema";
                rule "schema import only"
                when
                then
                end
            )";

            auto errors = parse_expect_errors(drl, "rules.rfl");
            check(!errors.empty());
            check_str_contains(errors.front().message.c_str(), "Expected 'schema'");
        }

        it("rejects string imports that are not schema files") {
            std::string drl = R"(
                import "market.json";
                rule "bad string import"
                when
                then
                end
            )";

            auto errors = parse_expect_errors(drl, "rules.rfl");
            check(!errors.empty());
            check_str_contains(errors.front().message.c_str(), ".schema");
        }

        it("parses generic container field types in declarations") {
            std::string drl = R"(
                declare Basket
                    tags: List<String>
                    scores: Set<int>
                    attrs: Map<String, double>
                    owner: Customer
                end
            )";

            auto state = parse_success(drl);
            check(state.parsed_declarations.size() == 1);
            auto const& fields = state.parsed_declarations[0].fields;
            check(fields.size() == 4);

            check(fields[0].name == "tags");
            check(fields[0].type == FT_List);
            check(fields[0].type_params.size() == 1);
            check(fields[0].type_params[0].base_type == FT_String);

            check(fields[1].name == "scores");
            check(fields[1].type == FT_Set);
            check(fields[1].type_params.size() == 1);
            check(fields[1].type_params[0].base_type == FT_Int);

            check(fields[2].name == "attrs");
            check(fields[2].type == FT_Map);
            check(fields[2].type_params.size() == 2);
            check(fields[2].type_params[0].base_type == FT_String);
            check(fields[2].type_params[1].base_type == FT_Double);

            check(fields[3].name == "owner");
            check(fields[3].type == FT_Object);
            check(fields[3].type_params.size() == 1);
            check(fields[3].type_params[0].base_type == FT_Object);
            check(fields[3].type_params[0].custom_type == "Customer");
        }

        it("preserves extended scalar field types in declarations") {
            std::string drl = R"(
                declare ScalarFact
                    counter: UInt64
                    raw: Bytes
                    observed: DateTime
                    day: Date
                    clock: Time
                    latency: Duration
                    price: Decimal
                    sequence: BigInt
                    total: Money
                end
            )";

            auto state = parse_success(drl);
            check_size_eq(state.parsed_declarations.size(), 1);
            auto const& fields = state.parsed_declarations[0].fields;
            check_size_eq(fields.size(), 9);
            check(fields[0].type == FT_UInt64);
            check(fields[1].type == FT_Bytes);
            check(fields[2].type == FT_DateTime);
            check(fields[3].type == FT_Date);
            check(fields[4].type == FT_Time);
            check(fields[5].type == FT_Duration);
            check(fields[6].type == FT_Decimal);
            check(fields[7].type == FT_BigInt);
            check(fields[8].type == FT_Money);
        }

        it("parses List of custom object type") {
            std::string drl = R"(
                declare Basket
                    id: int
                end

                declare BasketBatch
                    baskets: List<Basket>
                end
            )";

            auto state = parse_success(drl);
            check(state.parsed_declarations.size() == 2);

            auto const& batch_decl = state.parsed_declarations[1];
            check(batch_decl.type_name == "BasketBatch");
            check(batch_decl.fields.size() == 1);

            auto const& baskets_field = batch_decl.fields[0];
            check(baskets_field.name == "baskets");
            check(baskets_field.type == FT_List);
            check(baskets_field.type_params.size() == 1);
            check(baskets_field.type_params[0].base_type == FT_Object);
            check(baskets_field.type_params[0].custom_type == "Basket");
        }
    }

    // ------------------------------------------------------------------
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
            auto state = parse_success(drl);
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

    // ------------------------------------------------------------------
    group("Native Temporal Operators 'after' and 'within'") {
        it("parses CEP temporal operators into constraint nodes") {
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

            auto state = parse_success(drl);

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

    // ------------------------------------------------------------------
    group("Parse Error Reporting") {
        it("reports an error for completely invalid syntax") {
            auto errors = parse_expect_errors("this is not valid rfl at all @@@@");
            check(!errors.empty());
        }
    }
}
