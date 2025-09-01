#include "ast_builder.hpp"
#include "ast_transformer.hpp"
#include "drools_grammar.hpp"
#include "drools_parser.hpp"
#include "drools_parser_state.hpp"
#include "errors.hpp"
#include "knowledge_base.hpp"
#include "semantic_analyzer.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <magic_enum/magic_enum.hpp>
#include <memory>
#include <string>
#include <tao/pegtl.hpp>
#include <tao/pegtl/contrib/parse_tree.hpp>
#include <tao/pegtl/file_input.hpp>
#include <vector>

namespace FileUtils {
    std::string read_file_content(std::string const& path) {
        std::ifstream file_stream(path);
        if (!file_stream) { throw std::runtime_error("Could not open file: " + path); }
        std::stringstream buffer;
        buffer << file_stream.rdbuf();
        return buffer.str();
    }
}   // namespace FileUtils

namespace DebugUtils {
    void print_constraint_node(ConstraintNode const* node, int level);

    std::string indent(int level) { return std::string(level * 2, ' '); }

    void print_parsed_constraint(ParsedConstraint const& c, int level) {
        std::cout << indent(level) << "LEAF: ";
        if (c.field_binding) { std::cout << *c.field_binding << " : "; }
        std::cout << c.left_field << " " << c.op << " ";
        if (c.right_literal) {
            std::cout << ::to_string(*c.right_literal);
        } else if (c.right_bound_field) {
            std::cout << c.right_bound_field->first << "." << c.right_bound_field->second;
        }
        std::cout << std::endl;
    }

    void print_constraint_node(ConstraintNode const* node, int level) {
        if (!node) return;
        switch (node->type) {
            case NodeType::LEAF:
                print_parsed_constraint(node->constraint, level);
                break;
            case NodeType::AND:
                std::cout << indent(level) << "AND" << std::endl;
                for (auto const& child : node->children) print_constraint_node(child.get(), level + 1);
                break;
            case NodeType::OR:
                std::cout << indent(level) << "OR" << std::endl;
                for (auto const& child : node->children) print_constraint_node(child.get(), level + 1);
                break;
        }
    }

    void print_rule_ast(ParsedRule const& rule) {
        std::cout << "=================================================\n";
        std::cout << "RULE: \"" << rule.name << "\"\n";
        std::cout << "SALIENCE: " << rule.salience << "\n";
        std::cout << "--- WHEN ---\n";

        for (size_t i = 0; i < rule.condition_groups.size(); ++i) {
            if (i > 0) { std::cout << indent(1) << "--- OR ---\n"; }
            for (auto const& pattern : rule.condition_groups[i]) {
                std::cout << indent(1) << "PATTERN: " << std::string(magic_enum::enum_name(pattern.type));
                if (!pattern.fact_type.empty()) {
                    std::cout << " [ " << pattern.fact_type;
                    if (!pattern.binding.empty()) std::cout << " : " << pattern.binding;
                    std::cout << " ]";
                }
                std::cout << "\n";
                if (pattern.constraint_root) print_constraint_node(pattern.constraint_root.get(), 2);
            }
        }
        std::cout << "--- THEN ---\n";
        std::cout << indent(1) << rule.rhs_code << "\n";
        std::cout << "=================================================\n\n";
    }
}   // namespace DebugUtils

struct ProgramOptions {
    std::string filepath;
    bool dump_ast = false;
};

class Application {
public:
    Application(int argc, char* argv[]) { parse_arguments(argc, argv); }

    int run() {
        std::cout << "--- Analyzing DRL file: " << m_options.filepath << " ---\n\n";
        auto start_time = std::chrono::high_resolution_clock::now();

        // The entire compilation process is now encapsulated here.
        ParsingResult result;
        std::shared_ptr<KnowledgeBase> kb = build_knowledge_base(result, m_options.filepath);

        // Check the result of the compilation.
        if (!result.success) {
            print_results(result.errors, start_time);
            return 1;   // Failure
        }

        // If successful, print the success message.
        print_results(result.errors, start_time);

        // Optionally dump the AST from the successfully created KnowledgeBase.
        if (m_options.dump_ast && kb) {
            std::cout << "\n--- Abstract Syntax Trees (AST) ---\n";
            // The AST now lives inside the KnowledgeBase's parser_state.
            for (auto const& rule : kb->get_parser_state().parsed_rules) { DebugUtils::print_rule_ast(rule); }
        }
    }

private:
    ProgramOptions m_options;

    void parse_arguments(int argc, char* argv[]) {
        std::vector<std::string> args(argv + 1, argv + argc);
        for (auto const& arg : args) {
            if (arg == "--dump-ast") {
                m_options.dump_ast = true;
            } else if (arg.rfind("--", 0) != 0) {
                m_options.filepath = arg;
            }
        }

        if (m_options.filepath.empty()) {
            throw std::runtime_error("Usage: " + std::string(argv[0]) + " <rules_file.drl> [--dump-ast]");
        }
    }

    void print_results(std::vector<StructuredError> const& errors,
                       std::chrono::high_resolution_clock::time_point start_time) {
        auto end_time = std::chrono::high_resolution_clock::now();
        double duration_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

        std::cout << "\n----------------------------------------\n";
        if (errors.empty()) {
            std::cout << "Analysis finished successfully.\n";
        } else {
            std::cout << "Analysis finished with " << errors.size() << " error(s):\n";
            for (auto const& err : errors) { std::cout << "- " << err.to_string() << "\n"; }
        }
        std::cout << "Total time: " << duration_ms << " ms\n";
        std::cout << "----------------------------------------\n";
    }
};

int main(int argc, char* argv[]) {
    try {
        Application app(argc, argv);
        return app.run();
    } catch (std::exception const& e) {
        std::cerr << "\n[FATAL ERROR] " << e.what() << std::endl;
        return 1;
    }
}
