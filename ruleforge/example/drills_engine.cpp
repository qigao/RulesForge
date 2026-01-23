#include "rfl_parser.hpp"
#include "errors.hpp"
#include "knowledge_base.hpp"
#include "logging_control.hpp"

#include <chrono>
#include <iostream>

#include <string>
#include <vector>

namespace DebugUtils {
std::string indent(int level) { return std::string(level * 2, ' '); }

void print_constraint_node(ConstraintNode const *node, int level) {
  if (!node)
    return;
  switch (node->type) {
  case NodeType::LEAF: {
    auto const &c = node->constraint;
    std::cout << indent(level) << "LEAF: ";
    if (c.field_binding)
      std::cout << *c.field_binding << " : ";
    std::cout << c.left_field << " " << c.op << " ";
    if (c.right_literal) {
      std::cout << ::to_string(*c.right_literal);
    } else if (c.right_bound_field) {
      std::cout << c.right_bound_field->first << "." << c.right_bound_field->second;
    }
    std::cout << std::endl;
    break;
  }
  case NodeType::AND:
    std::cout << indent(level) << "AND" << std::endl;
    for (auto const &child : node->children)
      print_constraint_node(child.get(), level + 1);
    break;
  case NodeType::OR:
    std::cout << indent(level) << "OR" << std::endl;
    for (auto const &child : node->children)
      print_constraint_node(child.get(), level + 1);
    break;
  }
}

void print_rule_ast(ParsedRule const &rule) {
  std::cout << "=================================================\n";
  std::cout << "RULE: \"" << rule.name << "\"\n";
  std::cout << "SALIENCE: " << rule.salience << "\n";
  std::cout << "--- WHEN ---\n";

  for (size_t i = 0; i < rule.condition_groups.size(); ++i) {
    if (i > 0)
      std::cout << indent(1) << "--- OR ---\n";
    for (auto const &pattern : rule.condition_groups[i]) {
      std::cout << indent(1) << "PATTERN: " << std::string(ENUM_NAME(pattern.type));
      if (!pattern.fact_type.empty()) {
        std::cout << " [ " << pattern.fact_type;
        if (!pattern.binding.empty())
          std::cout << " : " << pattern.binding;
        std::cout << " ]";
      }
      std::cout << "\n";
      if (pattern.constraint_root)
        print_constraint_node(pattern.constraint_root.get(), 2);
    }
  }
  std::cout << "--- THEN ---\n";
  std::cout << indent(1) << rule.rhs_code << "\n";
  std::cout << "=================================================\n\n";
}
} // namespace DebugUtils

struct ProgramOptions {
  std::string filepath;
  bool dump_ast = false;
};

class Application {
public:
  Application(int argc, char *argv[]) { parse_arguments(argc, argv); }

  int run() {
    std::cout << "--- Analyzing RFL file: " << m_options.filepath << " ---\n\n";
    auto start = std::chrono::high_resolution_clock::now();

    ParsingResult result;
    auto kb = build_knowledge_base(result, m_options.filepath);

    print_results(result, start);

    if (result.success && m_options.dump_ast && kb) {
      std::cout << "\n--- Abstract Syntax Trees (AST) ---\n";
      for (auto const &rule : kb->get_parser_state().parsed_rules) {
        DebugUtils::print_rule_ast(rule);
      }
    }

    return result.success ? 0 : 1;
  }

private:
  ProgramOptions m_options;

  void parse_arguments(int argc, char *argv[]) {
    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--dump-ast") {
        m_options.dump_ast = true;
      } else if (arg[0] != '-') {
        m_options.filepath = arg;
      }
    }
    if (m_options.filepath.empty()) {
      throw std::runtime_error("Usage: " + std::string(argv[0]) + " <rules_file.drl> [--dump-ast]");
    }
  }

  void print_results(ParsingResult const &result,
                     std::chrono::high_resolution_clock::time_point start) {
    auto duration =
        std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - start)
            .count();

    std::cout << "\n----------------------------------------\n";
    if (result.errors.empty()) {
      std::cout << "Analysis finished successfully.\n";
    } else {
      std::cout << "Analysis finished with " << result.errors.size() << " error(s):\n";
      for (auto const &err : result.errors)
        std::cout << "- " << err.to_string() << "\n";
    }
    std::cout << "Total time: " << duration << " ms\n";
    std::cout << "----------------------------------------\n";
  }
};

int main(int argc, char *argv[]) {
  // Initialize fmtlog with TSCNS timing system
  fmtlogWrapper<>::impl.init();
  fmtlog::setLogLevel(fmtlog::INF);

  logi("Drills Engine starting up with fmtlog logging system");
  logi("Arguments: {}", argc);

  try {
    Application app(argc, argv);
    int result = app.run();
    logi("Drills Engine finishing with exit code: {}", result);
    return result;
  } catch (std::exception const &e) {
    loge("Fatal error in Drills Engine: {}", e.what());
    std::cerr << "\n[FATAL ERROR] " << e.what() << std::endl;
    return 1;
  }

  // Poll and flush any remaining log messages
  fmtlog::poll(true);
}
