#include "decision_table_compiler.hpp"
#include "decision_table_parser.hpp"
#include "tinytest.h"

suite("DecisionTableCompiler") {
    it("tracks direct compile stats") {
        DirectTableCompiler::reset_stats();

        std::string const csv_content =
            "PACKAGE,com.example.dt\n"
            ",CONDITION: Customer(balance > $1),\"ACTION: insert Offer { message = '$1' }\"\n"
            "Stats Offer,5000,Approved\n";

        ParsingResult parse_result;
        DecisionTable table =
            DecisionTableParser::parse_string(csv_content, "decision_table_compiler_test_stats", parse_result);
        check(parse_result.success);

        std::vector<StructuredError> errors;
        parser_state state =
            DirectTableCompiler::compile(table, "decision_table_compiler_test_stats", errors);

        check(errors.empty());
        check(!state.parsed_rules.empty());

        auto stats = DirectTableCompiler::get_stats();
        check(stats.direct_success >= 1);
        check(stats.fallback_success == 0);
    }

    it("compiles supported decision table directly into parser state") {
        std::string const csv_content =
            "PACKAGE,com.example.dt\n"
            "IMPORT,com.example.shared.*\n"
            "DECLARE,Customer,\"name: String, balance: double\"\n"
            "QUERY,find_customers,$c: Customer()\n"
            ",CONDITION: Customer(balance > $1),\"ACTION: insert Offer { message = '$1' }\"\n"
            "High Balance Offer,5000,Approved\n";

        ParsingResult parse_result;
        DecisionTable table =
            DecisionTableParser::parse_string(csv_content, "decision_table_compiler_test", parse_result);
        check(parse_result.success);

        std::vector<StructuredError> errors;
        parser_state state =
            DirectTableCompiler::compile(table, "decision_table_compiler_test", errors);

        check(errors.empty());
        check(state.package_name == "com.example.dt");
        check(state.parsed_imports.size() == 1);
        check(state.parsed_declarations.size() == 1);
        check(state.parsed_queries.size() == 1);
        check(!state.parsed_rules.empty());
        check(state.parsed_rules[0].name == "High Balance Offer");
        check(state.parsed_rules[0].condition_groups.size() == 1);
        check(state.parsed_rules[0].condition_groups[0].size() == 1);
    }

    it("falls back to parser path for unsupported condition syntax") {
        DirectTableCompiler::reset_stats();

        std::string const csv_content =
            "PACKAGE,com.example.dt\n"
            ",CONDITION: not (Customer(balance > $1)),\"ACTION: insert Offer { message = '$1' }\"\n"
            "High Balance Offer,5000,Approved\n";

        ParsingResult parse_result;
        DecisionTable table =
            DecisionTableParser::parse_string(csv_content, "decision_table_compiler_test_fallback", parse_result);
        check(parse_result.success);

        std::vector<StructuredError> errors;
        parser_state state =
            DirectTableCompiler::compile(table, "decision_table_compiler_test_fallback", errors);

        check(errors.empty());
        check(!state.parsed_rules.empty());

        auto stats = DirectTableCompiler::get_stats();
        check(stats.fallback_success >= 1);
        check(stats.fallback_condition_parse >= 1);
    }

    it("tracks fallback reason counters via table-driven cases") {
        auto make_base_table = []() {
            DecisionTable table;
            table.preamble_records = {{"PACKAGE", "com.example.dt"}};
            table.headers = {
                "",
                "CONDITION: Customer(balance > $1)",
                "ACTION: insert Offer { message = '$1' }",
            };
            table.data = {{"Base Offer", "5000", "Approved"}};
            return table;
        };

        struct FallbackCase {
            char const* source_name;
            DecisionTable table;
            std::size_t DecisionTableCompileStats::*reason_counter;
            bool expect_fallback_success;
        };

        std::vector<FallbackCase> cases;

        {
            auto table = make_base_table();
            table.preamble_records.push_back({"UNKNOWN", "foo"});
            cases.push_back({
                "decision_table_compiler_test_fallback_unknown_preamble",
                std::move(table),
                &DecisionTableCompileStats::fallback_unknown_preamble,
                true,
            });
        }
        {
            auto table = make_base_table();
            table.preamble_records.push_back({"DECLARE", "Customer", "name String"});
            cases.push_back({
                "decision_table_compiler_test_fallback_declare_parse",
                std::move(table),
                &DecisionTableCompileStats::fallback_declare_parse,
                false,
            });
        }
        {
            auto table = make_base_table();
            table.preamble_records.push_back({"QUERY", "find_customers", "$c Customer()"});
            cases.push_back({
                "decision_table_compiler_test_fallback_query_parse",
                std::move(table),
                &DecisionTableCompileStats::fallback_query_parse,
                false,
            });
        }
        {
            auto table = make_base_table();
            table.headers = {
                "",
                "Salience",
                "CONDITION: Customer(balance > $1)",
                "ACTION: insert Offer { message = '$1' }",
            };
            table.data = {{"Bad Salience Offer", "high", "5000", "Approved"}};
            cases.push_back({
                "decision_table_compiler_test_fallback_salience_parse",
                std::move(table),
                &DecisionTableCompileStats::fallback_salience_parse,
                false,
            });
        }
        {
            auto table = make_base_table();
            table.headers = {
                "",
                "CONDITION: not (Customer(balance > $1))",
                "ACTION: insert Offer { message = '$1' }",
            };
            table.data = {{"Unsupported Condition Offer", "5000", "Approved"}};
            cases.push_back({
                "decision_table_compiler_test_fallback_condition_parse",
                std::move(table),
                &DecisionTableCompileStats::fallback_condition_parse,
                true,
            });
        }

        for (auto const& tc : cases) {
            DirectTableCompiler::reset_stats();

            std::vector<StructuredError> errors;
            parser_state state = DirectTableCompiler::compile(tc.table, tc.source_name, errors);
            (void)state;

            auto stats = DirectTableCompiler::get_stats();
            check((stats.*(tc.reason_counter)) >= 1);
            check(stats.direct_success == 0);
            if (tc.expect_fallback_success) {
                check(errors.empty());
                check(stats.fallback_success >= 1);
            }
        }
    }

    it("directly parses binding and string operators") {
        std::string const csv_content =
            "PACKAGE,com.example.dt\n"
            ",\"CONDITION: $c: Customer(status contains \"\"$1\"\")\",\"ACTION: insert Offer { message = '$1' }\"\n"
            "Contains Offer,GOLD,Approved\n";

        ParsingResult parse_result;
        DecisionTable table =
            DecisionTableParser::parse_string(csv_content, "decision_table_compiler_test_binding", parse_result);
        check(parse_result.success);

        std::vector<StructuredError> errors;
        parser_state state =
            DirectTableCompiler::compile(table, "decision_table_compiler_test_binding", errors);

        check(errors.empty());
        check(state.parsed_rules.size() == 1);
        check(state.parsed_rules[0].condition_groups.size() == 1);
        check(state.parsed_rules[0].condition_groups[0].size() == 1);
        check(state.parsed_rules[0].condition_groups[0][0].binding == "$c");
        check(state.parsed_rules[0].condition_groups[0][0].constraint_root != nullptr);
    }

    it("directly parses top-level and constraints") {
        std::string const csv_content =
            "PACKAGE,com.example.dt\n"
            ",CONDITION: Customer(balance > $1 and balance < 1000),\"ACTION: insert Offer { message = '$1' }\"\n"
            "Range Offer,100,Approved\n";

        ParsingResult parse_result;
        DecisionTable table =
            DecisionTableParser::parse_string(csv_content, "decision_table_compiler_test_and", parse_result);
        check(parse_result.success);

        std::vector<StructuredError> errors;
        parser_state state =
            DirectTableCompiler::compile(table, "decision_table_compiler_test_and", errors);

        check(errors.empty());
        check(state.parsed_rules.size() == 1);
        auto const& p = state.parsed_rules[0].condition_groups[0][0];
        check(p.constraint_root != nullptr);
        check(p.constraint_root->type == NodeType::AND);
        check(p.constraint_root->children.size() == 2);
    }

    it("directly parses top-level or constraints") {
        std::string const csv_content =
            "PACKAGE,com.example.dt\n"
            ",\"CONDITION: Customer(status == \"\"$1\"\" or status == \"\"SILVER\"\")\",\"ACTION: insert Offer { message = '$1' }\"\n"
            "Status Offer,GOLD,Approved\n";

        ParsingResult parse_result;
        DecisionTable table =
            DecisionTableParser::parse_string(csv_content, "decision_table_compiler_test_or", parse_result);
        check(parse_result.success);

        std::vector<StructuredError> errors;
        parser_state state =
            DirectTableCompiler::compile(table, "decision_table_compiler_test_or", errors);

        check(errors.empty());
        check(state.parsed_rules.size() == 1);
        auto const& p = state.parsed_rules[0].condition_groups[0][0];
        check(p.constraint_root != nullptr);
        check(p.constraint_root->type == NodeType::OR);
        check(p.constraint_root->children.size() == 2);
    }

    it("directly parses in list into right_value_list") {
        std::string const csv_content =
            "PACKAGE,com.example.dt\n"
            ",\"CONDITION: Customer(status in (\"\"$1\"\", \"\"SILVER\"\", \"\"BRONZE\"\"))\",\"ACTION: insert Offer { message = '$1' }\"\n"
            "In List Offer,GOLD,Approved\n";

        ParsingResult parse_result;
        DecisionTable table =
            DecisionTableParser::parse_string(csv_content, "decision_table_compiler_test_in", parse_result);
        check(parse_result.success);

        std::vector<StructuredError> errors;
        parser_state state =
            DirectTableCompiler::compile(table, "decision_table_compiler_test_in", errors);

        check(errors.empty());
        check(state.parsed_rules.size() == 1);
        if (!errors.empty() || state.parsed_rules.empty()
            || state.parsed_rules[0].condition_groups.empty()
            || state.parsed_rules[0].condition_groups[0].empty()) {
            return;
        }
        auto const& p = state.parsed_rules[0].condition_groups[0][0];
        check(p.constraint_root != nullptr);
        if (!p.constraint_root) {
            return;
        }
        check(p.constraint_root->type == NodeType::LEAF);
        check(p.constraint_root->constraint.op == CompareOp::In);
        check(p.constraint_root->constraint.right_value_list.has_value());
        check(p.constraint_root->constraint.right_value_list->size() == 3);
    }

    it("directly parses not in list into right_value_list") {
        std::string const csv_content =
            "PACKAGE,com.example.dt\n"
            ",\"CONDITION: Customer(status not in (\"\"$1\"\", \"\"SILVER\"\"))\",\"ACTION: insert Offer { message = '$1' }\"\n"
            "Not In List Offer,GOLD,Approved\n";

        ParsingResult parse_result;
        DecisionTable table =
            DecisionTableParser::parse_string(csv_content, "decision_table_compiler_test_not_in", parse_result);
        check(parse_result.success);

        std::vector<StructuredError> errors;
        parser_state state =
            DirectTableCompiler::compile(table, "decision_table_compiler_test_not_in", errors);

        check(errors.empty());
        check(state.parsed_rules.size() == 1);
        if (!errors.empty() || state.parsed_rules.empty()
            || state.parsed_rules[0].condition_groups.empty()
            || state.parsed_rules[0].condition_groups[0].empty()) {
            return;
        }
        auto const& p = state.parsed_rules[0].condition_groups[0][0];
        check(p.constraint_root != nullptr);
        if (!p.constraint_root) {
            return;
        }
        check(p.constraint_root->type == NodeType::LEAF);
        check(p.constraint_root->constraint.op == CompareOp::NotIn);
        check(p.constraint_root->constraint.right_value_list.has_value());
        check(p.constraint_root->constraint.right_value_list->size() == 2);
    }

    it("directly parses inline field-binding constraint") {
        std::string const csv_content =
            "PACKAGE,com.example.dt\n"
            ",CONDITION: Customer($x: balance > $1),\"ACTION: insert Offer { message = '$1' }\"\n"
            "Binding Constraint Offer,100,Approved\n";

        ParsingResult parse_result;
        DecisionTable table =
            DecisionTableParser::parse_string(csv_content, "decision_table_compiler_test_inline_binding", parse_result);
        check(parse_result.success);

        std::vector<StructuredError> errors;
        parser_state state =
            DirectTableCompiler::compile(table, "decision_table_compiler_test_inline_binding", errors);

        check(errors.empty());
        check(state.parsed_rules.size() == 1);
        auto const& c = state.parsed_rules[0].condition_groups[0][0].constraint_root->constraint;
        check(c.field_binding.has_value());
        check(*c.field_binding == "$x");
        check(c.left_field == "balance");
    }

    it("directly parses bare field boolean check") {
        std::string const csv_content =
            "PACKAGE,com.example.dt\n"
            ",CONDITION: Customer(active),\"ACTION: insert Offer { message = '$1' }\"\n"
            "Bare Field Offer,ok,Approved\n";

        ParsingResult parse_result;
        DecisionTable table =
            DecisionTableParser::parse_string(csv_content, "decision_table_compiler_test_bare_field", parse_result);
        check(parse_result.success);

        std::vector<StructuredError> errors;
        parser_state state =
            DirectTableCompiler::compile(table, "decision_table_compiler_test_bare_field", errors);

        check(errors.empty());
        check(state.parsed_rules.size() == 1);
        auto const& c = state.parsed_rules[0].condition_groups[0][0].constraint_root->constraint;
        check(c.left_field == "active");
        check(c.op == CompareOp::EQ);
        check(c.right_literal.has_value());
    }

    it("directly parses parenthesized single constraint") {
        std::string const csv_content =
            "PACKAGE,com.example.dt\n"
            ",CONDITION: Customer((balance > $1)),\"ACTION: insert Offer { message = '$1' }\"\n"
            "Paren Single Offer,100,Approved\n";

        ParsingResult parse_result;
        DecisionTable table =
            DecisionTableParser::parse_string(csv_content, "decision_table_compiler_test_paren_single", parse_result);
        check(parse_result.success);

        std::vector<StructuredError> errors;
        parser_state state =
            DirectTableCompiler::compile(table, "decision_table_compiler_test_paren_single", errors);

        check(errors.empty());
        check(state.parsed_rules.size() == 1);
        if (!errors.empty() || state.parsed_rules.empty()
            || state.parsed_rules[0].condition_groups.empty()
            || state.parsed_rules[0].condition_groups[0].empty()) {
            return;
        }
        auto const& p = state.parsed_rules[0].condition_groups[0][0];
        check(p.constraint_root != nullptr);
        if (!p.constraint_root) {
            return;
        }
        check(p.constraint_root->type == NodeType::LEAF);
    }

    it("directly parses parenthesized and constraints") {
        std::string const csv_content =
            "PACKAGE,com.example.dt\n"
            ",CONDITION: Customer((balance > $1) and (balance < 1000)),\"ACTION: insert Offer { message = '$1' }\"\n"
            "Paren And Offer,100,Approved\n";

        ParsingResult parse_result;
        DecisionTable table =
            DecisionTableParser::parse_string(csv_content, "decision_table_compiler_test_paren_and", parse_result);
        check(parse_result.success);

        std::vector<StructuredError> errors;
        parser_state state =
            DirectTableCompiler::compile(table, "decision_table_compiler_test_paren_and", errors);

        check(errors.empty());
        check(state.parsed_rules.size() == 1);
        auto const& p = state.parsed_rules[0].condition_groups[0][0];
        check(p.constraint_root != nullptr);
        check(p.constraint_root->type == NodeType::AND);
        check(p.constraint_root->children.size() == 2);
    }

    it("directly parses mixed and/or with precedence") {
        std::string const csv_content =
            "PACKAGE,com.example.dt\n"
            ",\"CONDITION: Customer(status == \"\"$1\"\" or status == \"\"SILVER\"\" and balance > 100)\",\"ACTION: insert Offer { message = '$1' }\"\n"
            "Mixed Precedence Offer,GOLD,Approved\n";

        ParsingResult parse_result;
        DecisionTable table =
            DecisionTableParser::parse_string(csv_content, "decision_table_compiler_test_mixed_prec", parse_result);
        check(parse_result.success);

        std::vector<StructuredError> errors;
        parser_state state =
            DirectTableCompiler::compile(table, "decision_table_compiler_test_mixed_prec", errors);

        check(errors.empty());
        check(state.parsed_rules.size() == 1);
        auto const& root = state.parsed_rules[0].condition_groups[0][0].constraint_root;
        check(root != nullptr);
        check(root->type == NodeType::OR);
        check(root->children.size() == 2);
    }

    it("directly parses parenthesized or under and") {
        std::string const csv_content =
            "PACKAGE,com.example.dt\n"
            ",\"CONDITION: Customer(balance > 100 and (status == \"\"$1\"\" or status == \"\"SILVER\"\"))\",\"ACTION: insert Offer { message = '$1' }\"\n"
            "Nested Mixed Offer,GOLD,Approved\n";

        ParsingResult parse_result;
        DecisionTable table =
            DecisionTableParser::parse_string(csv_content, "decision_table_compiler_test_nested_mixed", parse_result);
        check(parse_result.success);

        std::vector<StructuredError> errors;
        parser_state state =
            DirectTableCompiler::compile(table, "decision_table_compiler_test_nested_mixed", errors);

        check(errors.empty());
        check(state.parsed_rules.size() == 1);
        auto const& root = state.parsed_rules[0].condition_groups[0][0].constraint_root;
        check(root != nullptr);
        check(root->type == NodeType::AND);
        check(root->children.size() == 2);
        check(root->children[1] != nullptr);
        check(root->children[1]->type == NodeType::OR);
    }

    it("directly parses comma-separated constraints as and") {
        std::string const csv_content =
            "PACKAGE,com.example.dt\n"
            ",\"CONDITION: Customer(balance > $1, balance < 1000)\",\"ACTION: insert Offer { message = '$1' }\"\n"
            "Comma And Offer,100,Approved\n";

        ParsingResult parse_result;
        DecisionTable table =
            DecisionTableParser::parse_string(csv_content, "decision_table_compiler_test_comma", parse_result);
        check(parse_result.success);

        std::vector<StructuredError> errors;
        parser_state state =
            DirectTableCompiler::compile(table, "decision_table_compiler_test_comma", errors);

        check(errors.empty());
        check(state.parsed_rules.size() == 1);
        auto const& root = state.parsed_rules[0].condition_groups[0][0].constraint_root;
        check(root != nullptr);
        check(root->type == NodeType::AND);
        check(root->children.size() == 2);
    }

    it("directly parses mixed comma and or") {
        std::string const csv_content =
            "PACKAGE,com.example.dt\n"
            ",\"CONDITION: Customer(balance > 100, status == \"\"$1\"\" or status == \"\"SILVER\"\")\",\"ACTION: insert Offer { message = '$1' }\"\n"
            "Comma Mixed Offer,GOLD,Approved\n";

        ParsingResult parse_result;
        DecisionTable table =
            DecisionTableParser::parse_string(csv_content, "decision_table_compiler_test_comma_mixed", parse_result);
        check(parse_result.success);

        std::vector<StructuredError> errors;
        parser_state state =
            DirectTableCompiler::compile(table, "decision_table_compiler_test_comma_mixed", errors);

        check(errors.empty());
        check(state.parsed_rules.size() == 1);
        auto const& root = state.parsed_rules[0].condition_groups[0][0].constraint_root;
        check(root != nullptr);
        check(root->type == NodeType::OR);
        check(root->children.size() == 2);
    }

    it("directly parses rhs binding reference into right_bound_field") {
        std::string const csv_content =
            "PACKAGE,com.example.dt\n"
            ",CONDITION: Customer(balance > $1),\"ACTION: insert Offer { message = '$1' }\"\n"
            "Bound Field Offer,$base,Approved\n";

        ParsingResult parse_result;
        DecisionTable table =
            DecisionTableParser::parse_string(csv_content, "decision_table_compiler_test_rhs_binding", parse_result);
        check(parse_result.success);

        std::vector<StructuredError> errors;
        parser_state state =
            DirectTableCompiler::compile(table, "decision_table_compiler_test_rhs_binding", errors);

        check(errors.empty());
        check(state.parsed_rules.size() == 1);
        auto const& c = state.parsed_rules[0].condition_groups[0][0].constraint_root->constraint;
        check(c.right_bound_field.has_value());
        check(c.right_bound_field->first == "$base");
        check(c.right_bound_field->second == "this");
    }

    it("directly parses rhs arithmetic expression into right_arith_expr") {
        std::string const csv_content =
            "PACKAGE,com.example.dt\n"
            ",CONDITION: Customer(balance > $1),\"ACTION: insert Offer { message = '$1' }\"\n"
            "Arith Expr Offer,$base * 1.2,Approved\n";

        ParsingResult parse_result;
        DecisionTable table =
            DecisionTableParser::parse_string(csv_content, "decision_table_compiler_test_rhs_arith", parse_result);
        check(parse_result.success);

        std::vector<StructuredError> errors;
        parser_state state =
            DirectTableCompiler::compile(table, "decision_table_compiler_test_rhs_arith", errors);

        check(errors.empty());
        check(state.parsed_rules.size() == 1);
        auto const& c = state.parsed_rules[0].condition_groups[0][0].constraint_root->constraint;
        check(c.right_arith_expr.has_value());
        check(*c.right_arith_expr == "$base * 1.2");
    }

    it("directly parses not pattern qualifier") {
        std::string const csv_content =
            "PACKAGE,com.example.dt\n"
            ",CONDITION: not Customer(active),\"ACTION: insert Offer { message = '$1' }\"\n"
            "Not Pattern Offer,ok,Approved\n";

        ParsingResult parse_result;
        DecisionTable table =
            DecisionTableParser::parse_string(csv_content, "decision_table_compiler_test_not_pattern", parse_result);
        check(parse_result.success);

        std::vector<StructuredError> errors;
        parser_state state =
            DirectTableCompiler::compile(table, "decision_table_compiler_test_not_pattern", errors);

        check(errors.empty());
        check(state.parsed_rules.size() == 1);
        check(state.parsed_rules[0].condition_groups.size() == 1);
        check(state.parsed_rules[0].condition_groups[0].size() == 1);
        check(state.parsed_rules[0].condition_groups[0][0].type == PatternType::NOT);
    }

    it("directly parses exists pattern qualifier") {
        std::string const csv_content =
            "PACKAGE,com.example.dt\n"
            ",CONDITION: exists Customer(active),\"ACTION: insert Offer { message = '$1' }\"\n"
            "Exists Pattern Offer,ok,Approved\n";

        ParsingResult parse_result;
        DecisionTable table =
            DecisionTableParser::parse_string(csv_content, "decision_table_compiler_test_exists_pattern", parse_result);
        check(parse_result.success);

        std::vector<StructuredError> errors;
        parser_state state =
            DirectTableCompiler::compile(table, "decision_table_compiler_test_exists_pattern", errors);

        check(errors.empty());
        check(state.parsed_rules.size() == 1);
        check(state.parsed_rules[0].condition_groups.size() == 1);
        check(state.parsed_rules[0].condition_groups[0].size() == 1);
        check(state.parsed_rules[0].condition_groups[0][0].type == PatternType::EXISTS);
    }

    it("directly parses uppercase logical and qualifier keywords") {
        std::string const csv_content =
            "PACKAGE,com.example.dt\n"
            ",\"CONDITION: NOT Customer((balance > 100) AND (status == \"\"$1\"\" OR status == \"\"SILVER\"\"))\",\"ACTION: insert Offer { message = '$1' }\"\n"
            "Uppercase Keywords Offer,GOLD,Approved\n";

        ParsingResult parse_result;
        DecisionTable table =
            DecisionTableParser::parse_string(csv_content, "decision_table_compiler_test_uppercase_keywords", parse_result);
        check(parse_result.success);

        std::vector<StructuredError> errors;
        parser_state state =
            DirectTableCompiler::compile(table, "decision_table_compiler_test_uppercase_keywords", errors);

        check(errors.empty());
        check(state.parsed_rules.size() == 1);
        check(state.parsed_rules[0].condition_groups.size() == 1);
        check(state.parsed_rules[0].condition_groups[0].size() == 1);
        auto const& p = state.parsed_rules[0].condition_groups[0][0];
        check(p.type == PatternType::NOT);
        check(p.constraint_root != nullptr);
        if (!p.constraint_root) {
            return;
        }
        check(p.constraint_root->type == NodeType::AND);
        check(p.constraint_root->children.size() == 2);
        check(p.constraint_root->children[1] != nullptr);
        if (!p.constraint_root->children[1]) {
            return;
        }
        check(p.constraint_root->children[1]->type == NodeType::OR);
    }

    it("directly parses uppercase textual operators and booleans") {
        std::string const csv_content =
            "PACKAGE,com.example.dt\n"
            ",\"CONDITION: Customer(active == TRUE and status CONTAINS \"\"$1\"\")\",\"ACTION: insert Offer { message = '$1' }\"\n"
            "Uppercase Op Offer,GOLD,Approved\n";

        ParsingResult parse_result;
        DecisionTable table =
            DecisionTableParser::parse_string(csv_content, "decision_table_compiler_test_uppercase_ops", parse_result);
        check(parse_result.success);

        std::vector<StructuredError> errors;
        parser_state state =
            DirectTableCompiler::compile(table, "decision_table_compiler_test_uppercase_ops", errors);

        check(errors.empty());
        check(state.parsed_rules.size() == 1);
        check(state.parsed_rules[0].condition_groups.size() == 1);
        check(state.parsed_rules[0].condition_groups[0].size() == 1);
        auto const& root = state.parsed_rules[0].condition_groups[0][0].constraint_root;
        check(root != nullptr);
        if (!root) {
            return;
        }
        check(root->type == NodeType::AND);
        check(root->children.size() == 2);
        check(root->children[1] != nullptr);
        if (!root->children[1]) {
            return;
        }
        check(root->children[1]->constraint.op == CompareOp::Contains);
    }

    it("directly parses negated bare field with bang prefix") {
        std::string const csv_content =
            "PACKAGE,com.example.dt\n"
            ",CONDITION: Customer(!active),\"ACTION: insert Offer { message = '$1' }\"\n"
            "Bang Negation Offer,ok,Approved\n";

        ParsingResult parse_result;
        DecisionTable table =
            DecisionTableParser::parse_string(csv_content, "decision_table_compiler_test_bang_negation", parse_result);
        check(parse_result.success);

        std::vector<StructuredError> errors;
        parser_state state =
            DirectTableCompiler::compile(table, "decision_table_compiler_test_bang_negation", errors);

        check(errors.empty());
        check(state.parsed_rules.size() == 1);
        check(state.parsed_rules[0].condition_groups.size() == 1);
        check(state.parsed_rules[0].condition_groups[0].size() == 1);
        auto const& root = state.parsed_rules[0].condition_groups[0][0].constraint_root;
        check(root != nullptr);
        if (!root) {
            return;
        }
        check(root->type == NodeType::LEAF);
        check(root->constraint.left_field == "active");
        check(root->constraint.op == CompareOp::EQ);
        check(root->constraint.right_literal.has_value());
    }

    it("directly parses case-insensitive decision table headers") {
        std::string const csv_content =
            "PACKAGE,com.example.dt\n"
            ",condition: Customer(balance > $1),ACTION: insert Offer { message = '$1' },salience,Agenda-Group\n"
            "Header Case Offer,5000,Approved,25,VIP\n";

        ParsingResult parse_result;
        DecisionTable table =
            DecisionTableParser::parse_string(csv_content, "decision_table_compiler_test_header_case", parse_result);
        check(parse_result.success);

        std::vector<StructuredError> errors;
        parser_state state =
            DirectTableCompiler::compile(table, "decision_table_compiler_test_header_case", errors);

        check(errors.empty());
        check(state.parsed_rules.size() == 1);
        check(state.parsed_rules[0].name == "Header Case Offer");
        check(state.parsed_rules[0].salience == 25);
        check(state.parsed_rules[0].agenda_group.has_value());
        check(*state.parsed_rules[0].agenda_group == "VIP");
        check(state.parsed_rules[0].condition_groups.size() == 1);
        check(state.parsed_rules[0].condition_groups[0].size() == 1);
    }
}
