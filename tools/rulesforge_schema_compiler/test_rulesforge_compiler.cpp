#include "compiler_core.hpp"

#include "tinytest.h"

#include <cstdio>
#include <fstream>
#include <string>

using rulesforge_compiler::compile_source;
using rulesforge_compiler::emit_declarations_only;
using rulesforge_compiler::builtin_template_name;
using rulesforge_compiler::render_with_template_file;
using rulesforge_compiler::validate_databind_support;
using rulesforge_compiler::OutputLanguage;
using rulesforge_compiler::ValidationMode;

spec("rulesforge compiler") {
    it("emits package enums and declarations from mixed rule files") {
        std::string const source = R"(
            package demo.market

            enum Side<int>
                Buy,
                Sell
            end

            declare Order
                @role(event)
                orderId: long
                side: Side
                tags: List<String>
                attrs: Map<String, int>
            end

            rule "ignore me"
            when
                $o : Order()
            then
            end
        )";

        ParsingResult result;
        parser_state state;
        check(compile_source(source, "inline.rfl", result, state));

        std::string const output = emit_declarations_only(state);
        check_str_contains(output.c_str(), "package demo.market");
        check_str_contains(output.c_str(), "enum Side");
        check_str_contains(output.c_str(), "@role(event)");
        check_str_contains(output.c_str(), "declare Order");
        check_str_contains(output.c_str(), "orderId: long");
        check_str_contains(output.c_str(), "tags: List<String>");
        check_str_contains(output.c_str(), "attrs: Map<String, int>");
        check(!strstr(output.c_str(), "rule \"ignore me\""));
    }

    it("validates supported declarations against databind") {
        std::string const source = R"(
            package demo

            enum Side<int>
                Buy,
                Sell
            end

            declare Level
                price: long
                venue: String
            end

            declare Order
                orderId: long
                side: Side
                bestBid: Level
                tags: Set<String>
                attrs: Map<String, int>
            end
        )";

        ParsingResult result;
        parser_state state;
        std::string error;
        check(compile_source(source, "inline.rfl", result, state));
        check(validate_databind_support(state, ValidationMode::Both, error));
        check_str_eq(error.c_str(), "");
    }

    it("rejects declarations unsupported by databind early") {
        std::string const source = R"(
            declare Bad
                badMap: Map<int, int>
            end
        )";

        ParsingResult result;
        parser_state state;
        std::string error;
        check(compile_source(source, "inline.rfl", result, state));
        check(!validate_databind_support(state, ValidationMode::BinaryOnly, error));
        check_str_contains(error.c_str(), "declaration 'Bad'");
        check_str_contains(error.c_str(), "field 'Bad.badMap'");
    }

    it("supports format-specific validation") {
        std::string const source = R"(
            declare Good
                id: long
                tags: Set<String>
            end
        )";

        ParsingResult result;
        parser_state state;
        std::string error;
        check(compile_source(source, "inline.rfl", result, state));
        check(validate_databind_support(state, ValidationMode::JsonOnly, error));
        check_str_eq(error.c_str(), "");
        check(validate_databind_support(state, ValidationMode::BinaryOnly, error));
        check_str_eq(error.c_str(), "");
    }

    it("accepts int64 collections for both binary and JSON validation") {
        std::string const source = R"(
            declare Good
                ids: Set<long>
                attrs: Map<String, long>
            end
        )";

        ParsingResult result;
        parser_state state;
        std::string error;
        check(compile_source(source, "inline.rfl", result, state));

        check(validate_databind_support(state, ValidationMode::BinaryOnly, error));
        check_str_eq(error.c_str(), "");

        check(validate_databind_support(state, ValidationMode::JsonOnly, error));
        check_str_eq(error.c_str(), "");
    }

    it("renders custom template output for external usage") {
        std::string const source = R"(
            package demo.market

            enum Side<int>
                Buy,
                Sell
            end

            declare Order
                orderId: long
                side: Side
            end
        )";

        ParsingResult result;
        parser_state state;
        std::string error;
        check(compile_source(source, "inline.rfl", result, state));

        char const* template_path = "rulesforge_compiler_template_test.mustache";
        {
            std::ofstream out(template_path, std::ios::binary);
            out << "pkg={{package}}\n";
            out << "{{#enums}}enum={{name}}:{{underlying_type}}\n{{/enums}}";
            out << "{{#declarations}}decl={{name}}\n";
            out << "{{#fields}}field={{name}}:{{type}}\n{{/fields}}{{/declarations}}";
        }

        std::string rendered;
        check(render_with_template_file(state, template_path, rendered, error));
        check_str_contains(rendered.c_str(), "pkg=demo.market");
        check_str_contains(rendered.c_str(), "enum=Side:int");
        check_str_contains(rendered.c_str(), "decl=Order");
        check_str_contains(rendered.c_str(), "field=orderId:long");
        check_str_contains(rendered.c_str(), "field=side:Side");

        std::remove(template_path);
    }

    it("renders built-in language templates") {
        std::string const source = R"(
            package demo.market

            enum Side<int>
                Buy,
                Sell
            end

            declare Order
                orderId: long
                tags: List<String>
            end
        )";

        ParsingResult result;
        parser_state state;
        std::string error;
        check(compile_source(source, "inline.rfl", result, state));

        auto template_path = [&](OutputLanguage lang) {
            return std::string(RULESFORGE_COMPILER_TEMPLATE_DIR) + "/"
                   + builtin_template_name(lang);
        };

        std::string rendered;
        check(render_with_template_file(state, template_path(OutputLanguage::C), rendered, error));
        check_str_contains(rendered.c_str(), "typedef struct Order");
        check_str_contains(rendered.c_str(), "int64_t orderId;");

        check(render_with_template_file(state, template_path(OutputLanguage::Cpp), rendered, error));
        check_str_contains(rendered.c_str(), "struct Order");
        check_str_contains(rendered.c_str(), "std::int64_t orderId;");
        check_str_contains(rendered.c_str(), "std::vector<std::string> tags;");

        check(render_with_template_file(state, template_path(OutputLanguage::Go), rendered, error));
        check_str_contains(rendered.c_str(), "package market");
        check_str_contains(rendered.c_str(), "type Order struct");

        check(render_with_template_file(state, template_path(OutputLanguage::TypeScript), rendered, error));
        check_str_contains(rendered.c_str(), "export interface Order");
        check_str_contains(rendered.c_str(), "tags: string[];");

        check(render_with_template_file(state, template_path(OutputLanguage::Python), rendered, error));
        check_str_contains(rendered.c_str(), "class Order:");
        check_str_contains(rendered.c_str(), "orderId: int");
    }
}
