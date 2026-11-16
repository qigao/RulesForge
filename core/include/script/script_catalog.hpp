#pragma once
#include "model/rhs_actions.hpp"
#include "script/script_types.h"
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace rulesforge {
enum class ScriptKind {
  Null,
  Boolean,
  Integer,
  Unsigned,
  Number,
  String,
  Uuid,
  Decimal,
  Money,
  Array,
  Record
};
struct ScriptShape {
  ScriptKind kind = ScriptKind::Null;
  bool nullable = false;
  std::vector<std::pair<std::string, ScriptShape>> fields;
  std::shared_ptr<ScriptShape const> element;
};
struct ScriptBinding {
  std::string name;
  size_t module = 0;
  std::string export_name;
  std::vector<ScriptShape> parameters;
  ScriptShape result;
};
struct ScriptSource {
  std::string name, source;
};
struct ScriptCatalog {
  std::string provider_path;
  std::string rule_source, rule_path;
  rf_script_limits limits{};
  size_t max_sessions = 0;
  std::vector<ScriptSource> modules;
  std::vector<std::string> native_modules;
  std::vector<ScriptBinding> bindings;
  void bind(std::vector<CompiledAction> &actions) const {
    for (auto &action : actions) {
      if (action.type == RhsActionType::CALL) {
        size_t index = 0;
        while (index < bindings.size() && bindings[index].name != action.script_binding)
          ++index;
        if (index == bindings.size())
          throw std::runtime_error("Unknown script binding: " + action.script_binding);
        if (bindings[index].parameters.size() != action.script_arguments.size())
          throw std::runtime_error("Script argument count mismatch: " + action.script_binding);
        for (size_t i = 0; i < action.script_arguments.size(); ++i) {
          auto const &argument = action.script_arguments[i];
          if (!argument.has_precomputed_literal)
            continue;
          auto const &value = argument.precomputed_literal;
          auto kind = bindings[index].parameters[i].kind;
          bool matches = (kind == ScriptKind::Boolean && std::holds_alternative<bool>(value)) ||
                         (kind == ScriptKind::Integer && std::holds_alternative<int64_t>(value)) ||
                         (kind == ScriptKind::Number && std::holds_alternative<double>(value)) ||
                         (kind == ScriptKind::String && std::holds_alternative<std::string>(value));
          if (!matches)
            throw std::runtime_error("Script literal type mismatch: " + action.script_binding);
        }
        action.script_binding_index = index;
      }
      bind(action.then_actions);
      bind(action.else_actions);
      bind(action.body_actions);
      for (auto &c : action.switch_cases)
        bind(c.actions);
    }
  }
};
std::shared_ptr<ScriptCatalog const> load_script_catalog(std::string const &path);
} // namespace rulesforge
