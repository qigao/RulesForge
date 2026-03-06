#pragma once

#include "core/constraint_types.hpp"
#include "core/rfl_strings.hpp"
#include "core/rhs_actions.hpp"
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

enum class PatternType { STANDARD, NOT, EXISTS, FORALL, EVAL, QUERY_CALL };

struct ParsedPattern; // Forward declaration
struct ConstraintNode;

struct ParsedAccumulate {
  std::unique_ptr<ParsedPattern> source_pattern;
  std::string function;
  std::string field;
  std::string accumulate_field_name;
  std::shared_ptr<rulesforge::ExpressionEvaluator> compiled_expr;
  std::map<std::string, std::string> inline_binding_to_field;
  ParsedAccumulate();
  ParsedAccumulate(ParsedAccumulate const &other);
  ParsedAccumulate(ParsedAccumulate &&) = default;
  ParsedAccumulate &operator=(ParsedAccumulate const &other);
  ParsedAccumulate &operator=(ParsedAccumulate &&) = default;
  ~ParsedAccumulate() = default;
};

struct ParsedUnnest {
  std::string source_binding;
  std::string source_field;
};

struct ParsedJmesPath {
  enum class EngineKind { JmesPath, DsvFilter };
  enum class InputKind { JsonString, JsonFile };
  EngineKind engine_kind = EngineKind::JmesPath;
  InputKind input_kind = InputKind::JsonString;
  std::string input_value;
  std::string expression;
};

struct ParsedQueryCall {
  std::string query_name;
  std::vector<std::string> arguments;
};

struct ParsedForall {
  std::vector<ParsedPattern> patterns;
};

using PatternSource = std::variant<std::monostate, ParsedAccumulate, ParsedUnnest, ParsedQueryCall,
                                   ParsedJmesPath, std::string>;

struct ParsedPattern {
  PatternType type = PatternType::STANDARD;
  SourcePosition pos;
  std::string binding;
  std::string fact_type;
  std::unique_ptr<ConstraintNode> constraint_root;
  std::vector<ParsedPattern> nested_patterns;
  std::optional<std::string> eval_expression;
  std::optional<ParsedForall> forall_info;
  PatternSource source;
  ParsedPattern();
  ParsedPattern(ParsedPattern const &other);
  ParsedPattern(ParsedPattern &&) = default;
  ParsedPattern &operator=(ParsedPattern const &other);
  ParsedPattern &operator=(ParsedPattern &&) = default;
  ~ParsedPattern() = default;
};

struct ParsedQuery {
  std::string name;
  std::vector<ParsedPattern> patterns;
  SourcePosition pos;
  std::vector<std::string> parameter_types;
  int parameter_count = 0;
  std::string source_package;
  std::vector<std::string> source_imports;
  ParsedQuery();
};

struct ParsedFunction {
  std::string name;
  std::string return_type;
  std::string body;
  std::string parameter_list;
};

struct ParsedGlobal {
  std::string type;
  std::string name;
};

struct ParsedTimer {
  int64_t initial_delay;
  int64_t repeat_interval = -1;
};

struct ParsedRule {
  SourcePosition pos;
  std::string name;
  std::map<std::string, std::string> annotations;
  int salience = 0;
  bool salience_explicitly_set = false;
  bool no_loop = false;
  bool lock_on_active = false; // P1 FIX: lock-on-active attribute
  bool enabled = true;
  bool auto_focus = false;
  int64_t duration = 0;
  std::optional<std::string> parent_rule_name;
  std::optional<std::string> agenda_group;
  std::optional<std::string> activation_group; // P1 FIX: activation-group attribute
  std::optional<ParsedTimer> timer;
  std::vector<std::vector<ParsedPattern>> condition_groups;
  std::string rhs_code;
  size_t rhs_start_line = 0;
  std::string source_package;
  std::vector<std::string> source_imports;
  // Native RHS execution
  std::vector<CompiledAction> compiled_actions;
  ParsedRule();
};
