#include "js_semantic_analyzer.hpp"
#include "fmtlog.h"
#include <regex>
#include <set>
#include <sstream>

namespace {
// Helper function to strip JavaScript comments from code
std::string strip_js_comments(const std::string &code) {
  std::string result;
  result.reserve(code.size());

  bool in_single_line_comment = false;
  bool in_multi_line_comment = false;
  bool in_string = false;
  char string_char = 0;

  for (size_t i = 0; i < code.size(); ++i) {
    char c = code[i];
    char next = (i + 1 < code.size()) ? code[i + 1] : 0;

    // Handle string literals (don't strip "comments" inside strings)
    if (!in_single_line_comment && !in_multi_line_comment) {
      if (!in_string && (c == '"' || c == '\'' || c == '`')) {
        in_string = true;
        string_char = c;
        result += c;
        continue;
      } else if (in_string) {
        result += c;
        // Handle escape sequences
        if (c == '\\' && i + 1 < code.size()) {
          result += code[++i];
          continue;
        }
        if (c == string_char) {
          in_string = false;
        }
        continue;
      }
    }

    // Handle single-line comments
    if (!in_multi_line_comment && c == '/' && next == '/') {
      in_single_line_comment = true;
      ++i; // Skip the second /
      continue;
    }

    // Handle multi-line comments
    if (!in_single_line_comment && c == '/' && next == '*') {
      in_multi_line_comment = true;
      ++i; // Skip the *
      continue;
    }

    // End of single-line comment
    if (in_single_line_comment && (c == '\n' || c == '\r')) {
      in_single_line_comment = false;
      result += c; // Keep the newline
      continue;
    }

    // End of multi-line comment
    if (in_multi_line_comment && c == '*' && next == '/') {
      in_multi_line_comment = false;
      ++i;           // Skip the /
      result += ' '; // Replace comment with space to preserve token boundaries
      continue;
    }

    // Add character if not in a comment
    if (!in_single_line_comment && !in_multi_line_comment) {
      result += c;
    }
  }

  return result;
}
} // namespace

JSSemanticAnalyzer::JSSemanticAnalyzer(SymbolTable const &symbols, ParsedRule const &rule,
                                       SemanticAnalyzer &base_analyzer)
    : symbols_(symbols), rule_(rule), analyzer_(base_analyzer) {
  logd("JSSemanticAnalyzer: Initializing for rule '{}'", rule_.name);
}

bool JSSemanticAnalyzer::analyze_js_rhs(ParsedRule &rule) {
  if (rule.rhs_code.empty()) {
    return true; // Empty RHS is valid
  }

  logd("JSSemanticAnalyzer: Analyzing JavaScript RHS for rule '{}'", rule.name);
  logd("Original RHS code: {}", rule.rhs_code);

  size_t initial_error_count = analyzer_.get_errors().size();

  // Step 1: Validate JavaScript syntax
  std::string syntax_error;
  if (!validate_syntax(rule.rhs_code, syntax_error)) {
    analyzer_.add_error(rule.pos,
                        "JavaScript syntax error in rule '" + rule.name + "': " + syntax_error);
    return false;
  }

  // Step 2: Parse and analyze the JavaScript code
  auto ast = ast_builder_.parse(rule.rhs_code);
  if (!ast.is_valid) {
    analyzer_.add_error(rule.pos, "Failed to parse JavaScript in rule '" + rule.name +
                                      "': " + ast.error_message);
    return false;
  }

  // Step 3: Extract and validate function calls and variables
  // Strip comments to avoid false positives (e.g., "// gateway.call()")
  std::string code_without_comments = strip_js_comments(rule.rhs_code);
  auto function_calls = analyze_function_calls(code_without_comments);
  auto variables = analyze_variables(code_without_comments);
  auto local_vars = ast_builder_.extract_local_declarations(code_without_comments);

  if (!validate_function_calls(function_calls)) {
    return false; // Errors already added by validate_function_calls
  }

  if (!validate_variable_bindings(variables, local_vars)) {
    return false; // Errors already added by validate_variable_bindings
  }

  // Step 4: Resolve types and substitute variables
  std::string processed_code = resolve_and_substitute_types(rule.rhs_code);
  processed_code = substitute_variables(processed_code);

  // Check if any errors were added during variable substitution
  if (analyzer_.get_errors().size() > initial_error_count) {
    return false;
  }

  // Step 5: Update the rule with processed code
  rule.rhs_code = processed_code;

  logd("Processed RHS code: {}", rule.rhs_code);
  logd("JSSemanticAnalyzer: Successfully analyzed rule '{}'", rule.name);
  return true;
}

bool JSSemanticAnalyzer::validate_syntax(const std::string &js_code, std::string &error_message) {
  return ast_builder_.validate_syntax(js_code, error_message);
}

std::vector<JSFunctionCall> JSSemanticAnalyzer::analyze_function_calls(const std::string &js_code) {
  return ast_builder_.extract_function_calls(js_code);
}

std::vector<JSVariableRef> JSSemanticAnalyzer::analyze_variables(const std::string &js_code) {
  return ast_builder_.extract_variables(js_code);
}

std::string JSSemanticAnalyzer::resolve_and_substitute_types(const std::string &js_code) {
  // This replicates the type resolution logic from the original analyze_rhs
  std::regex type_regex(
      R"(type\s*:\s*["']([^"']+)["'])"); // JavaScript object syntax: {type: "TypeName"}
  std::string original_code = js_code;
  std::string code_with_resolved_types;
  code_with_resolved_types.reserve(original_code.length());

  auto last_match_end = original_code.cbegin();
  for (auto i = std::sregex_iterator(original_code.begin(), original_code.end(), type_regex);
       i != std::sregex_iterator(); ++i) {
    std::smatch match = *i;

    // Append the part before this match
    code_with_resolved_types.append(last_match_end, match.prefix().second);

    std::string unqualified_type = match[1].str();
    if (auto resolved_type =
            analyzer_.resolve_type(unqualified_type, rule_.source_package, rule_.source_imports)) {
      // Append the resolved type (keep JavaScript object syntax)
      code_with_resolved_types += "type: \"" + *resolved_type + "\"";
    } else {
      // If not resolved, append the original matched string
      code_with_resolved_types += match.str();
    }
    last_match_end = match.suffix().first;
  }

  // Append the rest after the last match
  code_with_resolved_types.append(last_match_end, original_code.cend());

  logd("After type resolution: {}", code_with_resolved_types);
  return code_with_resolved_types;
}

std::string JSSemanticAnalyzer::substitute_variables(const std::string &js_code) {
  // This replicates the variable substitution logic from rhs_substitutor
  // Convert $variable to variable (strip the $ prefix)
  // NOTE: Only match the variable name, not property access like $p.name
  std::regex var_regex(R"(\$([a-zA-Z_][a-zA-Z0-9_]*))"); // Removed the optional property part
  std::string substituted_code;
  substituted_code.reserve(js_code.size());

  logd("substitute_variables: Processing code '{}'", js_code);
  logd("substitute_variables: Symbol table has {} entries", symbols_.size());

  bool has_unbound_variables = false;
  std::set<std::string> unbound_vars;

  auto last_match_end = js_code.cbegin();
  for (auto i = std::sregex_iterator(js_code.begin(), js_code.end(), var_regex);
       i != std::sregex_iterator(); ++i) {
    std::smatch match = *i;

    // Append the part before this match
    substituted_code.append(last_match_end, match.prefix().second);

    std::string full_binding = "$" + match[1].str(); // Reconstruct full binding for lookup
    std::string var_name = match[1].str();           // Variable name without $

    logd("substitute_variables: Found variable '{}', checking if '{}' exists in symbols",
         match.str(), full_binding);

    // Check if the binding exists in symbols
    if (symbols_.count(full_binding)) {
      // Valid binding - substitute by removing the $
      substituted_code += var_name;
      logd("Substituted {} -> {}", full_binding, var_name);
    } else {
      // Invalid binding - record for error reporting
      substituted_code += full_binding; // Keep original for error reporting
      unbound_vars.insert(full_binding);
      has_unbound_variables = true;
      logd("Binding '{}' not found in symbols, keeping original", full_binding);
    }

    last_match_end = match.suffix().first;
  }

  // Append the rest after the last match
  substituted_code.append(last_match_end, js_code.cend());

  // Report unbound variables as errors
  if (has_unbound_variables) {
    for (const auto &binding : unbound_vars) {
      std::string suggestion;
      int min_distance = 4;

      // Find similar variable names
      for (const auto &[valid_binding, info] : symbols_) {
        if (valid_binding.length() > 1 && binding.length() > 1) {
          if (std::abs(static_cast<int>(valid_binding.length()) -
                       static_cast<int>(binding.length())) <= 2) {
            suggestion = valid_binding;
            min_distance = 1;
          }
        }
      }

      std::string error_message =
          "In rule '" + rule_.name + "', RHS uses undeclared variable '" + binding + "'.";
      if (!suggestion.empty()) {
        error_message += " Did you mean '" + suggestion + "'?";
      }

      analyzer_.add_error(rule_.pos, error_message);
    }
  }

  logd("substitute_variables: Result: '{}'", substituted_code);
  return substituted_code;
}

bool JSSemanticAnalyzer::validate_variable_bindings(const std::vector<JSVariableRef> &variables,
                                                    const std::set<std::string> &local_vars) {
  bool all_valid = true;
  std::set<std::string> unbound_vars;

  // JavaScript built-in globals that should not be treated as Rules Forge Language bindings
  static const std::set<std::string> js_globals = {
      // RuleForge provided
      "rfl", "console", "type",
      // JavaScript built-in objects
      "Math", "Date", "JSON", "Array", "Object", "String", "Number", "Boolean", "RegExp", "Error",
      "Map", "Set", "WeakMap", "WeakSet", "Promise",
      // JavaScript built-in functions
      "parseInt", "parseFloat", "isNaN", "isFinite", "encodeURI", "decodeURI", "encodeURIComponent",
      "decodeURIComponent", "eval",
      // JavaScript built-in values
      "undefined", "NaN", "Infinity"};

  for (const auto &var : variables) {
    // Skip JavaScript built-in globals
    if (js_globals.count(var.name)) {
      continue;
    }

    // Skip locally declared variables (var, let, const)
    if (local_vars.count(var.name)) {
      continue;
    }

    // Check if this looks like a substituted variable (originally had $)
    std::string full_binding = "$" + var.name;

    if (symbols_.find(full_binding) == symbols_.end()) {
      unbound_vars.insert(full_binding);
    }
  }

  // Report unbound variables with suggestions
  for (const auto &binding : unbound_vars) {
    std::string suggestion;
    int min_distance = 4;

    // Find similar variable names (Levenshtein distance logic from original)
    for (const auto &[valid_binding, info] : symbols_) {
      // Simple similarity check - just check if they start similarly
      if (valid_binding.length() > 1 && binding.length() > 1) {
        if (std::abs(static_cast<int>(valid_binding.length()) -
                     static_cast<int>(binding.length())) <= 2) {
          suggestion = valid_binding;
          min_distance = 1; // Found a reasonable suggestion
        }
      }
    }

    std::string error_message =
        "In rule '" + rule_.name + "', JavaScript RHS uses undeclared variable '" + binding + "'.";
    if (!suggestion.empty()) {
      error_message += " Did you mean '" + suggestion + "'?";
    }

    analyzer_.add_error(rule_.pos, error_message);
    all_valid = false;
  }

  return all_valid;
}

bool JSSemanticAnalyzer::validate_function_calls(const std::vector<JSFunctionCall> &calls) {
  // For now, just validate that rfl function calls are known
  std::set<std::string> known_rfl_methods = {"insert", "insertLogical", "retract", "update",
                                                "setFocus"};

  for (const auto &call : calls) {
    if (call.object_name == "rfl") {
      if (known_rfl_methods.find(call.method_name) == known_rfl_methods.end()) {
        analyzer_.add_error(rule_.pos, "In rule '" + rule_.name +
                                           "', unknown rfl method: rfl." + call.method_name +
                                           "()");
        return false;
      }
    }
    // Other objects (like console) are allowed for now
  }

  return true;
}
