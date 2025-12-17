#include <regex>
#include <set>
#include <sstream>
#include <variant>

#include "semantic_analyzer.hpp"

#include <magic_enum/magic_enum.hpp>
#include <tao/pegtl.hpp>
#include <tao/pegtl/position.hpp>

#include "drools_rete_defs.hpp"
#include "js_semantic_analyzer.hpp"
#include "fmtlog.h"
namespace pegtl = tao::pegtl;

namespace
{
int calculate_levenshtein_distance(std::string const& s1, std::string const& s2)
{
  logd("Calculating Levenshtein distance between '{}' and '{}'", s1, s2);
  int const n = s1.length();
  int const m = s2.length();
  std::vector<int> p(m + 1);
  std::vector<int> d(m + 1);
  for (int i = 0; i <= m; i++) {
    p[i] = i;
  }
  for (int j = 1; j <= n; j++) {
    d[0] = j;
    for (int i = 1; i <= m; i++) {
      int const cost = (s1[j - 1] == s2[i - 1]) ? 0 : 1;
      d[i] = std::min({p[i] + 1, d[i - 1] + 1, p[i - 1] + cost});
    }
    std::swap(p, d);
  }
  return p[m];
}
}  // anonymous namespace

void analyze_constraint_node_recursive(ConstraintNode* node,
                                       ParsedPattern const& pattern,
                                       int depth,
                                       SymbolTable const& existing_symbols,
                                       SymbolTable& new_symbols,
                                       SemanticAnalyzer& analyzer,
                                       ParsedRule const& rule)
{
  if (!node) {
    return;
  }
  logd(
      "Analyzing constraint node type {} for pattern '{}' in rule '{}' at "
      "depth {}",
      magic_enum::enum_name(node->type),
      pattern.binding,
      rule.name,
      depth);

  // Get context from the pattern object directly
  tao::pegtl::position const& pos = pattern.pos;
  std::string const& fact_type = pattern.fact_type;
  std::string const& fact_binding = pattern.binding;

  if (node->type == NodeType::LEAF) {
    auto& constraint = node->constraint;
    if (constraint.field_binding) {
      if (existing_symbols.count(*constraint.field_binding)
          || new_symbols.count(*constraint.field_binding))
      {
        analyzer.add_error(pos,
                           "In rule '" + rule.name
                               + "', duplicate inline binding '"
                               + *constraint.field_binding + "' is declared.");
      } else {
        logd("  -> Found new inline binding: '{}' for field '{}'",
                  *constraint.field_binding,
                  constraint.left_field);
        // This is the critical fix: use the provided pattern and depth
        new_symbols[*constraint.field_binding] = SymbolInfo {
            &pattern, depth, {{fact_binding, constraint.left_field}}};
      }
    }
    if (!fact_type.empty() && !constraint.left_field.empty()
        && constraint.left_field != "this")
    {
      if (analyzer.get_type_schemas().count(fact_type)) {
        if (!analyzer.get_type_schemas().at(fact_type).count(
                constraint.left_field))
        {
          analyzer.add_error(pos,
                             "In rule '" + rule.name + "', constraint field '"
                                 + constraint.left_field
                                 + "' not found on fact type '" + fact_type
                                 + "'.");
        }
      }
    }
    if (constraint.right_bound_field) {
      auto const& binding_name = constraint.right_bound_field->first;
      auto it = existing_symbols.find(binding_name);
      if (it == existing_symbols.end()) {
        it = new_symbols.find(binding_name);
        if (it == new_symbols.end()) {
          analyzer.add_error(pos,
                             "In rule '" + rule.name
                                 + "', constraint uses undeclared binding '"
                                 + binding_name + "'.");
        } else {
          // Handle symbols found in new_symbols
          SymbolInfo const& info = it->second;
          if (info.source_field_of_binding) {
            logd(
                "  -> Rewriting bound field from alias '{}' to source '{}.{}'",
                binding_name,
                info.source_field_of_binding->first,
                info.source_field_of_binding->second);
            constraint.right_bound_field = *info.source_field_of_binding;
          }
        }
      } else {
        // Handle symbols found in existing_symbols
        SymbolInfo const& info = it->second;
        if (info.source_field_of_binding) {
          logd(
              "  -> Rewriting bound field from alias '{}' to source '{}.{}'",
              binding_name,
              info.source_field_of_binding->first,
              info.source_field_of_binding->second);
          constraint.right_bound_field = *info.source_field_of_binding;
        }
      }
    } else if (constraint.right_literal
               && std::holds_alternative<std::string>(
                   *constraint.right_literal))
    {
      logd("analyze_constraint_node_recursive: Processing right_literal as potential binding");
      // This handles cases where a binding was parsed as a literal string,
      // e.g., "field == $p"
      auto const& potential_binding =
          std::get<std::string>(*constraint.right_literal);
      logd("analyze_constraint_node_recursive: potential_binding='{}'", potential_binding);
      if (potential_binding.rfind('$', 0) == 0) {
        logd("analyze_constraint_node_recursive: Found potential binding '{}'", potential_binding);
        auto it = existing_symbols.find(potential_binding);
        bool found_in_existing = (it != existing_symbols.end());

        if (!found_in_existing) {
          it = new_symbols.find(potential_binding);
        }

        if (found_in_existing || it != new_symbols.end()) {
          logd("analyze_constraint_node_recursive: Binding '{}' found, converting to bound_field", potential_binding);
          constraint.right_bound_field = {{potential_binding, "this"}};
          constraint.right_literal = std::nullopt;

          // Now that we've converted it, run the alias-check logic again.
          SymbolInfo const& info = it->second;
          if (info.source_field_of_binding) {
            constraint.right_bound_field = *info.source_field_of_binding;
          }
        } else {
          logd("analyze_constraint_node_recursive: Binding '{}' NOT FOUND, should add error", potential_binding);
          analyzer.add_error(pos,
                             "In rule '" + rule.name
                                 + "', constraint uses undeclared binding '"
                                 + potential_binding + "'.");
        }
      }
    }
  } else {
    for (auto const& child : node->children) {
      analyze_constraint_node_recursive(child.get(),
                                        pattern,
                                        depth,
                                        existing_symbols,
                                        new_symbols,
                                        analyzer,
                                        rule);
    }
  }
}

SemanticAnalyzer::SemanticAnalyzer(parser_state& st,
                                   std::string const& source_name)
    : state_(st)
    , source_name_(source_name)
{
}

void SemanticAnalyzer::build_schema()
{
  logd("Building schema from {} declarations. Current package: '{}'",
            state_.parsed_declarations.size(),
            state_.package_name);
  type_schemas_.clear();

  // Add built-in Number type schema for accumulate results
  // Number supports intValue, doubleValue, longValue, floatValue accessors
  type_schemas_["Number"] = {"intValue", "doubleValue", "longValue", "floatValue", "value"};

  for (auto& decl : state_.parsed_declarations) {
    std::set<std::string> fields;
    for (auto const& field : decl.fields) {
      fields.insert(field.name);
    }

    if (!decl.source_package.empty()
        && decl.type_name.find('.') == std::string::npos)
    {
      decl.type_name = decl.source_package + "." + decl.type_name;
    }

    logd("  -> Schema for '{}': {} fields", decl.type_name, fields.size());
    type_schemas_[decl.type_name] = std::move(fields);
  }
}

std::optional<std::string> SemanticAnalyzer::resolve_type(
    std::string const& type_name,
    std::string const& package_ctx,
    std::vector<std::string> const& imports_ctx)
{
  if (type_schemas_.count(type_name)) {
    return type_name;
  }

  // Built-in types that don't need declaration
  // Number is used for accumulate results (count, sum, etc.)
  if (type_name == "String" || type_name == "int" || type_name == "long"
      || type_name == "double" || type_name == "boolean"
      || type_name == "List" || type_name == "Number")
  {
    return type_name;
  }

  if (type_name.find('.') == std::string::npos) {
    // 1. Check against specific imports from the rule's file
    for (auto const& import_path : imports_ctx) {
      if (import_path.ends_with("." + type_name)) {
        // This is a direct import, like `import com.example.model.Customer;`
        // The import_path IS the FQN. We must check if it exists in our schema.
        if (type_schemas_.count(import_path)) {
          return import_path;
        }
      }
    }

    // 2. Check relative to the current rule's package
    if (!package_ctx.empty()) {
      std::string fqn = package_ctx + "." + type_name;
      if (type_schemas_.count(fqn)) {
        return fqn;
      }
    }

    // 3. Check against wildcard imports
    for (auto const& import_path : imports_ctx) {
      if (import_path.ends_with(".*")) {
        std::string base_path = import_path.substr(0, import_path.length() - 1);
        std::string full_name = base_path + type_name;
        if (type_schemas_.count(full_name)) {
          return full_name;  // Found via wildcard
        }
      }
    }
  }
  return std::nullopt;  // It was a FQN but not found in the schema
}

void SemanticAnalyzer::analyze_rule(ParsedRule& rule)
{
  logd("Analyzing rule: {}", rule.name);
  if (rule.parent_rule_name) {
    bool found = false;
    for (auto const& r : state_.parsed_rules) {
      if (r.name == *rule.parent_rule_name) {
        found = true;
        break;
      }
    }
    if (!found) {
      add_error(rule.pos,
                "Rule '" + rule.name + "' extends non-existent rule '"
                    + *rule.parent_rule_name + "'.");
    }
  }
  for (auto& group : rule.condition_groups) {
    SymbolTable symbols;
    int depth = 0;
    analyze_pattern_list(group, symbols, depth, rule);
    analyze_rhs(rule, symbols);
  }
}

void SemanticAnalyzer::analyze_query(ParsedQuery& query)
{
  logd("Analyzing query: {}", query.name);

  // First, resolve all type names within the query's patterns.
  // This is the missing piece.
  for (auto& pattern : query.patterns) {
    if (!pattern.fact_type.empty()) {
      if (auto resolved_type = resolve_type(
              pattern.fact_type, query.source_package, query.source_imports))
      {
        pattern.fact_type = *resolved_type;
      } else {
        add_error(pattern.pos,
                  "In query '" + query.name
                      + "', pattern uses undeclared or unresolvable fact type '"
                      + pattern.fact_type + "'.");
      }
    }
  }

  SymbolTable symbols;
  int depth = 0;
  ParsedRule dummy_context;
  dummy_context.name = "query \"" + query.name + "\"";
  dummy_context.pos = query.pos;
  analyze_pattern_list(query.patterns, symbols, depth, dummy_context);

  // This part for parameter types is now redundant if the main loop is correct,
  // but leaving it is harmless.
  for (auto& p_type : query.parameter_types) {
    if (auto resolved =
            resolve_type(p_type, query.source_package, query.source_imports))
    {
      p_type = *resolved;
    }
  }
}

void SemanticAnalyzer::analyze_pattern_list(
    std::vector<ParsedPattern>& patterns,
    SymbolTable& symbols,
    int& depth,
    ParsedRule const& rule)
{
  for (auto& pattern : patterns) {
    // A pattern adds a fact to the token if it's:
    // - A standard pattern with no source (monostate)
    // - A standard pattern with entry-point source (std::string)
    // - A pattern with accumulate source
    // - A pattern with unnest source
    bool adds_fact_to_token =
        (pattern.type == PatternType::STANDARD
         && (std::holds_alternative<std::monostate>(pattern.source)
             || std::holds_alternative<std::string>(pattern.source)))
        || std::holds_alternative<ParsedAccumulate>(pattern.source)
        || std::holds_alternative<ParsedUnnest>(pattern.source);

    if (adds_fact_to_token && !pattern.binding.empty()) {
      if (symbols.count(pattern.binding)) {
        add_error(pattern.pos,
                  "In rule '" + rule.name + "', duplicate binding '"
                      + pattern.binding + "' is declared.");
      } else {
        logd(
            "  -> Found new binding '{}' at depth {}", pattern.binding, depth);
        symbols[pattern.binding] = SymbolInfo {&pattern, depth, std::nullopt};
      }
    }
    analyze_pattern(pattern, symbols, rule, pattern.pos, depth);
    if (adds_fact_to_token) {
      depth++;
    }
  }
}

void SemanticAnalyzer::analyze_pattern(ParsedPattern& pattern,
                                       SymbolTable& symbols,
                                       ParsedRule const& rule,
                                       tao::pegtl::position const& pattern_pos,
                                       int depth)
{
  logd("Analyzing pattern in rule '{}': type={}, fact_type={}, binding={}",
            rule.name,
            magic_enum::enum_name(pattern.type),
            pattern.fact_type,
            pattern.binding);
  if (pattern.type == PatternType::NOT || pattern.type == PatternType::EXISTS) {
    SymbolTable nested_symbols = symbols;
    if (!pattern.nested_patterns.empty()) {
      int nested_depth = 0;
      logd("  -> Analyzing nested patterns for NOT/EXISTS");
      analyze_pattern_list(
          pattern.nested_patterns, nested_symbols, nested_depth, rule);
    }
    return;
  }

  if (!pattern.fact_type.empty()) {
    if (auto resolved_type_opt = resolve_type(
            pattern.fact_type, rule.source_package, rule.source_imports))
    {
      pattern.fact_type = *resolved_type_opt;
    } else {
      add_error(pattern_pos,
                "In rule '" + rule.name
                    + "', pattern uses undeclared or unresolvable fact type '"
                    + pattern.fact_type + "'.");
    }
  }

  std::visit(
      [&](auto&& arg)
      {
        using T = std::decay_t<decltype(arg)>;

        if constexpr (std::is_same_v<T, ParsedAccumulate>) {
          logd("  -> Analyzing 'accumulate' source pattern");
          if (!arg.source_pattern) {
            return;
          }

          // Resolve the source pattern's fact type
          if (!arg.source_pattern->fact_type.empty()) {
            if (auto resolved = resolve_type(arg.source_pattern->fact_type,
                                             rule.source_package,
                                             rule.source_imports)) {
              arg.source_pattern->fact_type = *resolved;
            }
          }

          SymbolTable source_symbols = symbols;
          if (!arg.source_pattern->binding.empty()) {
            source_symbols[arg.source_pattern->binding] =
                SymbolInfo {arg.source_pattern.get(), 0, std::nullopt};
          }

          SymbolTable source_new_symbols;
          if (arg.source_pattern->constraint_root) {
            analyze_constraint_node_recursive(
                arg.source_pattern->constraint_root.get(),
                *arg.source_pattern,
                0,
                source_symbols,
                source_new_symbols,
                *this,
                rule);
          }
          SymbolTable combined_source_scope = source_symbols;
          combined_source_scope.insert(source_new_symbols.begin(),
                                       source_new_symbols.end());

          if (arg.field.empty()) {
            return;
          }

          std::string field_to_accumulate;
          std::string type_to_check_against = arg.source_pattern->fact_type;
          bool is_arithmetic_expr = arg.field.find_first_of("+-*/") != std::string::npos;

          if (is_arithmetic_expr) {
            // For arithmetic expressions like "$avail - $reserved", validate all bindings
            // and keep the expression as-is for code generation
            std::regex binding_regex(R"(\$[a-zA-Z_][a-zA-Z0-9_]*)");
            std::sregex_iterator iter(arg.field.begin(), arg.field.end(), binding_regex);
            std::sregex_iterator end;

            bool all_bindings_valid = true;
            while (iter != end) {
              std::string binding_name = iter->str();
              auto it = combined_source_scope.find(binding_name);
              if (it == combined_source_scope.end()) {
                add_error(pattern_pos,
                          "In rule '" + rule.name
                              + "', accumulate expression uses undeclared binding '"
                              + binding_name + "'.");
                all_bindings_valid = false;
              } else {
                auto const& symbol_info = it->second;
                if (symbol_info.source_field_of_binding) {
                  arg.inline_binding_to_field[binding_name] = symbol_info.source_field_of_binding->second;
                }
              }
              ++iter;
            }
            if (!all_bindings_valid) {
              return;
            }
            // Keep the arithmetic expression as the field to accumulate
            field_to_accumulate = arg.field;
            type_to_check_against = arg.source_pattern->fact_type;

          } else {
            size_t dot_pos = arg.field.find('.');

            if (dot_pos != std::string::npos && arg.field[0] == '$') {
              std::string binding_name = arg.field.substr(0, dot_pos);
              field_to_accumulate = arg.field.substr(dot_pos + 1);
              auto it = combined_source_scope.find(binding_name);
              if (it == combined_source_scope.end()) {
                add_error(pattern_pos,
                          "In rule '" + rule.name
                              + "', accumulate uses undeclared binding '"
                              + binding_name + "'.");
                return;
              }
              type_to_check_against = it->second.pattern->fact_type;

            } else if (arg.field[0] == '$') {
              auto it = combined_source_scope.find(arg.field);
              if (it == combined_source_scope.end()) {
                add_error(pattern_pos,
                          "In rule '" + rule.name
                              + "', accumulate function uses undeclared binding '"
                              + arg.field + "'.");
                return;
              }

              auto const& symbol_info = it->second;
              if (symbol_info.source_field_of_binding) {
                field_to_accumulate = symbol_info.source_field_of_binding->second;
              } else {
                field_to_accumulate = "this";
              }
              type_to_check_against = arg.source_pattern->fact_type;

            } else {
              // Check if it's a numeric literal like "1" in count(1)
              // In this case, treat it as counting all matches (field = "this")
              bool is_numeric = !arg.field.empty() &&
                  std::all_of(arg.field.begin(), arg.field.end(), ::isdigit);
              if (is_numeric) {
                field_to_accumulate = "this";
              } else {
                field_to_accumulate = arg.field;
              }
              type_to_check_against = arg.source_pattern->fact_type;
            }
          }

          // Skip schema validation for arithmetic expressions - the bindings were already validated
          if (field_to_accumulate != "this" && !is_arithmetic_expr) {
            auto schema_it = get_type_schemas().find(type_to_check_against);
            if (schema_it == get_type_schemas().end()
                || !schema_it->second.count(field_to_accumulate))
            {
              add_error(pattern_pos,
                        "In rule '" + rule.name + "', accumulate field '"
                            + field_to_accumulate + "' not found on type '"
                            + type_to_check_against + "'.");
            }
          }
          logd("    -> Accumulate field resolved to '{}'",
                    field_to_accumulate);
          arg.accumulate_field_name = field_to_accumulate;
        }
      },
      pattern.source);

  if (pattern.constraint_root) {
    SymbolTable newly_declared_in_this_pattern;
    analyze_constraint_node_recursive(pattern.constraint_root.get(),
                                      pattern,
                                      depth,
                                      symbols,
                                      newly_declared_in_this_pattern,
                                      *this,
                                      rule);
    symbols.insert(newly_declared_in_this_pattern.begin(),
                   newly_declared_in_this_pattern.end());
  }
  if (pattern.type == PatternType::EVAL && pattern.eval_expression.has_value())
  {
    std::string& code = *pattern.eval_expression;
    if (code.empty()) {
      return;
    }
    logd("  -> Analyzing 'eval' expression: {}", code);

    // Use JavaScript semantic analyzer for eval expressions too
    JSSemanticAnalyzer js_analyzer(symbols, rule, *this);
    std::string syntax_error;

    if (!js_analyzer.validate_syntax(code, syntax_error)) {
      add_error(pattern_pos, "JavaScript syntax error in eval expression: " + syntax_error);
      return;
    }

    // For eval expressions, we need to ensure they return a boolean
    // For now, just do basic variable substitution
    std::string substituted_code = code;

    // Simple variable substitution for eval (similar to RHS processing)
    std::regex var_regex(R"(\$([a-zA-Z_][a-zA-Z0-9_]*(?:\.[a-zA-Z_][a-zA-Z0-9_]*)*))");
    std::vector<std::string> unbound_variables;

    auto words_begin = std::sregex_iterator(code.begin(), code.end(), var_regex);
    auto words_end = std::sregex_iterator();

    for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
      std::smatch match = *i;
      std::string captured = match[1].str();  // e.g., "profile.avgTransactionAmount"

      // Extract just the base binding (before the first dot)
      size_t dot_pos = captured.find('.');
      std::string base_binding = "$" + (dot_pos != std::string::npos ? captured.substr(0, dot_pos) : captured);

      if (symbols.find(base_binding) == symbols.end()) {
        unbound_variables.push_back(base_binding);
      } else {
        // Replace $var.field with var.field (just remove the $)
        substituted_code = std::regex_replace(substituted_code,
          std::regex("\\$" + captured),
          (dot_pos != std::string::npos ? captured : captured));
      }
    }

    if (!unbound_variables.empty()) {
      std::set<std::string> unique_unbound(unbound_variables.begin(), unbound_variables.end());
      for (auto const& binding : unique_unbound) {
        std::string suggestion;
        int min_distance = 4;
        for (auto const& [valid_binding, info] : symbols) {
          int distance = calculate_levenshtein_distance(binding, valid_binding);
          if (distance < min_distance) {
            min_distance = distance;
            suggestion = valid_binding;
          }
        }
        std::string error_message = "In " + rule.name
            + ", eval() uses undeclared variable '" + binding + "'.";
        if (!suggestion.empty()) {
          error_message += " Did you mean '" + suggestion + "'?";
        }

        add_error(pattern_pos, error_message);
      }
    }

    if (errors_.empty()) {
      code = substituted_code;
      if (code.rfind("return ", 0) != 0) {
        code = "return " + code;
      }
      logd("    -> Substituted eval code: {}", code);
    }
  }
}

void SemanticAnalyzer::analyze_rhs(ParsedRule& rule, SymbolTable const& symbols)
{
  if (rule.rhs_code.empty()) {
    return;
  }

  logd("Analyzing RHS of rule '{}' with JavaScript analyzer", rule.name);

  // Use the new JavaScript semantic analyzer
  JSSemanticAnalyzer js_analyzer(symbols, rule, *this);

  if (!js_analyzer.analyze_js_rhs(rule)) {
    // Errors were already added by the JS analyzer
    logd("JavaScript RHS analysis failed for rule '{}'", rule.name);
    return;
  }

  logd("JavaScript RHS analysis completed successfully for rule '{}'",
            rule.name);
}

void SemanticAnalyzer::add_error(tao::pegtl::position const& pos,
                                 std::string const& message)
{
  logd("Semantic Error Added: file={}, line={}, col={}, message='{}'",
           source_name_,
           pos.line,
           pos.column,
           message);
  errors_.push_back({.file_name = source_name_,
                     .line = pos.line,
                     .column = pos.column,
                     .message = message});
}

bool SemanticAnalyzer::build_and_analyze_declarations()
{
  logd("Semantic Analysis - Phase 1: Building schema...");
  errors_.clear();
  build_schema();
  // In the future, you could add validation for declarations here.
  logd("Semantic Analysis - Phase 1 finished. Found {} errors.",
            errors_.size());
  return errors_.empty();
}

bool SemanticAnalyzer::analyze_rules_and_queries()
{
  logd("Semantic Analysis - Phase 2: Analyzing rules and queries...");
  // Note: errors_ is NOT cleared here, to accumulate errors from both phases.
  for (auto& rule : state_.parsed_rules) {
    analyze_rule(rule);
  }
  for (auto& query : state_.parsed_queries) {
    analyze_query(query);
  }
  logd("Semantic Analysis - Phase 2 finished. Total errors: {}.",
            errors_.size());
  return errors_.empty();
}


