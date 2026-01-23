#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <variant>
#include "tlog_helper.h"
#include "ast_builder.hpp"

#include <tao/pegtl/contrib/parse_tree.hpp>
#include <tao/pegtl/string_input.hpp>

#include "rfl_parser_state.hpp"
#include "logging_control.hpp"

namespace
{
// ... (helper functions are unchanged and correct) ...
template<typename T>
pegtl::parse_tree::node const* find_child(pegtl::parse_tree::node const& parent)
{
  for (auto const& child : parent.children) {
    if (child->is_type<T>()) {
      return child.get();
    }
  }
  return nullptr;
}

template<typename T>
pegtl::parse_tree::node const* find_descendant(
    pegtl::parse_tree::node const& parent)
{
  if (parent.is_type<T>()) {
    return &parent;
  }
  for (auto const& child : parent.children) {
    if (auto const* found = find_descendant<T>(*child)) {
      return found;
    }
  }
  return nullptr;
}

template<typename T>
void find_all_descendants_recursive(
    pegtl::parse_tree::node const& parent,
    std::vector<pegtl::parse_tree::node const*>& found)
{
  if (parent.is_type<T>()) {
    found.push_back(&parent);
  }
  for (auto const& child : parent.children) {
    find_all_descendants_recursive<T>(*child, found);
  }
}

template<typename T>
std::vector<pegtl::parse_tree::node const*> find_all_descendants(
    pegtl::parse_tree::node const& parent)
{
  std::vector<pegtl::parse_tree::node const*> found;
  find_all_descendants_recursive<T>(parent, found);
  return found;
}

long long parse_long_safe(std::string const& s, long long default_val = 0)
{
  try {
    return std::stoll(s);
  } catch (std::exception const&) {
    return default_val;
  }
}

double parse_double_safe(std::string const& s, double default_val = 0.0)
{
  try {
    return std::stod(s);
  } catch (std::exception const&) {
    return default_val;
  }
}

long long parse_duration_to_ms(std::string const& duration_str)
{
  if (duration_str.empty()) {
    return 0;
  }
  size_t split_pos = duration_str.find_first_not_of("0123456789-");
  if (split_pos == std::string::npos) {
    return parse_long_safe(duration_str);
  }
  long long value = parse_long_safe(duration_str.substr(0, split_pos));
  std::string unit = duration_str.substr(split_pos);
  if (unit == "h") {
    return value * 3600000;
  }
  if (unit == "m") {
    return value * 60000;
  }
  if (unit == "s") {
    return value * 1000;
  }
  return value;
}

// Helper function to extract field name from constraint_field or
// simple_name_part
std::string extract_field_name(pegtl::parse_tree::node const* primary_expr_node)
{
  if (!primary_expr_node) {
    return "";
  }

  // Check if it contains a constraint_field (compound field like $obj.field)
  if (auto* cf_node =
          find_descendant<grammar::constraint_field>(*primary_expr_node))
  {
    return cf_node->string();
  }

  // Check if it contains a simple_name_part (simple field like fieldname)
  if (auto* snp_node =
          find_descendant<grammar::simple_name_part>(*primary_expr_node))
  {
    return snp_node->string();
  }

  // Check for other primary expressions (this, variables, literals)
  if (find_descendant<grammar::keyword_this>(*primary_expr_node)) {
    return "this";
  }

  if (auto* vb_node =
          find_descendant<grammar::variable_binding>(*primary_expr_node))
  {
    return vb_node->string();
  }

  // Fallback to the original behavior
  return primary_expr_node->string();
}
}  // namespace

AstBuilder::AstBuilder(std::unique_ptr<pegtl::parse_tree::node> root,
                       std::string const& source_name)
    : root_(std::move(root))
    , source_name_(source_name)
{
    logi("AstBuilder created for source: {}", source_name_);
}

parser_state AstBuilder::build(std::vector<StructuredError>& out_errors)
{
  logd("AstBuilder::build starting for source: {}", source_name_);
  parser_state state;
  if (!root_) {
    logw("AstBuilder::build called with null root node for source: {}", source_name_);
    return state;
  }

  // First, find the package and all imports to establish the context for this
  // file.
  if (auto* pkg_node = find_descendant<grammar::package_statement>(*root_)) {
    build_package(*pkg_node, state);
  }
  auto import_nodes = find_all_descendants<grammar::import_statement>(*root_);
  for (auto const* import_node : import_nodes) {
    build_import(*import_node, state);
  }

  // Now, iterate and build the main definitions, stamping them with the context
  // we just found.
  auto statement_nodes = find_all_descendants<grammar::statement>(*root_);
  for (auto const* statement_node : statement_nodes) {
    if (statement_node->children.empty()) {
      continue;
    }
    auto const& content_node = *statement_node->children.front();
    if (content_node.is_type<grammar::rule>()) {
      ParsedRule rule = build_rule(content_node, out_errors);
      rule.source_package = state.package_name;
      rule.source_imports = state.parsed_imports;
      state.parsed_rules.push_back(std::move(rule));
    } else if (content_node.is_type<grammar::query_statement>()) {
      ParsedQuery query = build_query(content_node);
      query.source_package = state.package_name;
      query.source_imports = state.parsed_imports;
      state.parsed_queries.push_back(std::move(query));
    } else if (content_node.is_type<grammar::declaration_statement>()) {
      ParsedDeclaration decl = build_declaration(content_node);
      decl.source_package = state.package_name;  // Stamp the context
      state.parsed_declarations.push_back(std::move(decl));
    } else if (content_node.is_type<grammar::function_statement>()) {
      state.parsed_functions.push_back(build_function(content_node));
    } else if (content_node.is_type<grammar::global_statement>()) {
      state.parsed_globals.push_back(build_global(content_node));
    }
  }
  logi("AstBuilder::build finished. Parsed {} rules, {} queries, {} declarations.",
       state.parsed_rules.size(),
       state.parsed_queries.size(),
       state.parsed_declarations.size());
  return state;
}

ParsedRule AstBuilder::build_rule(pegtl::parse_tree::node const& n,
                                  std::vector<StructuredError>& out_errors)
{
  ParsedRule rule;
  rule.pos = n.begin();
  if (auto const* name_node = find_descendant<grammar::rule_name>(n)) {
    std::string name_str = name_node->string();
    if (name_str.length() >= 2) {
      rule.name = name_str.substr(1, name_str.length() - 2);
    }
  }

  if (auto const* annotations = find_descendant<grammar::annotation_list>(n)) {
    build_annotations(*annotations, rule);
  }
  if (auto const* attrs = find_descendant<grammar::attributes>(n)) {
    build_rule_attributes(*attrs, rule);
  }
  if (auto const* when_block = find_descendant<grammar::when_block>(n)) {
    if (auto const* lhs_node = find_descendant<grammar::lhs>(*when_block)) {
      build_lhs(*lhs_node, rule.condition_groups);
    }
  }
  if (rule.condition_groups.empty()) {
    rule.condition_groups.emplace_back();
  }
  if (auto const* then_block = find_descendant<grammar::then_block>(n)) {
    if (auto const* rhs_node = find_child<grammar::rhs>(*then_block)) {
      if (rhs_node->has_content() || !rhs_node->children.empty()) {
        rule.rhs_start_line = rhs_node->begin().line;
      }
      build_rhs(*rhs_node, rule);
    }
  }
  return rule;
}

void AstBuilder::build_rule_attributes(pegtl::parse_tree::node const& n,
                                       ParsedRule& rule)
{
  for (auto const& seq_node_ptr : n.children) {
    if (!seq_node_ptr || seq_node_ptr->children.empty()) {
      continue;
    }
    if (auto const* attr_sor_node =
            find_child<grammar::attribute>(*seq_node_ptr))
    {
      if (attr_sor_node->children.empty()) {
        continue;
      }
      auto const& attr_node = *attr_sor_node->children.front();
      if (attr_node.is_type<grammar::salience_attribute>()) {
        if (auto const* value_node =
                find_descendant<grammar::salience_value>(attr_node))
        {
          rule.salience = parse_long_safe(value_node->string());
          rule.salience_explicitly_set = true;
        }
      } else if (attr_node.is_type<grammar::extends_clause>()) {
        if (auto const* name_node =
                find_descendant<grammar::parent_rule_name>(attr_node))
        {
          std::string name_str = name_node->string();
          if (name_str.length() >= 2) {
            rule.parent_rule_name = name_str.substr(1, name_str.length() - 2);
          }
        }
      } else if (attr_node.is_type<grammar::agenda_group_attribute>()) {
        if (auto const* name_node =
                find_descendant<grammar::agenda_group_name>(attr_node))
        {
          std::string name_str = name_node->string();
          if (name_str.length() >= 2) {
            rule.agenda_group = name_str.substr(1, name_str.length() - 2);
          }
        }
      // P1 FIX: Parse activation-group attribute
      } else if (attr_node.is_type<grammar::activation_group_attribute>()) {
        if (auto const* name_node =
                find_descendant<grammar::activation_group_name>(attr_node))
        {
          std::string name_str = name_node->string();
          if (name_str.length() >= 2) {
            rule.activation_group = name_str.substr(1, name_str.length() - 2);
          }
        }
      } else if (attr_node.is_type<grammar::timer_attribute>()) {
        auto values = find_all_descendants<grammar::timer_value>(attr_node);
        if (!values.empty()) {
          rule.timer.emplace();
          rule.timer->initial_delay = parse_long_safe(values[0]->string());
          if (values.size() > 1) {
            rule.timer->repeat_interval = parse_long_safe(values[1]->string());
          }
        }
      } else if (attr_node.is_type<grammar::no_loop_attribute>()) {
        rule.no_loop = true;
      // P1 FIX: Parse lock-on-active attribute
      } else if (attr_node.is_type<grammar::lock_on_active_attribute>()) {
        rule.lock_on_active = true;
      } else if (attr_node.is_type<grammar::enabled_attribute>()) {
        auto enabled_value = attr_node.children[0].get();
        rule.enabled = enabled_value->is_type<grammar::keyword_true>();
      } else if (attr_node.is_type<grammar::auto_focus_attribute>()) {
        auto auto_focus_value = attr_node.children[0].get();
        rule.auto_focus = auto_focus_value->is_type<grammar::keyword_true>();
      } else if (attr_node.is_type<grammar::duration_attribute>()) {
        if (auto const* value_node =
                find_descendant<grammar::duration_value>(attr_node))
        {
          rule.duration = parse_long_safe(value_node->string());
        }
      }
    }
  }
}

void AstBuilder::normalize_indentation(std::string& code)
{
  std::istringstream stream(code);
  std::string line;
  std::vector<std::string> lines;
  while (std::getline(stream, line)) {
    lines.push_back(line);
  }
  if (lines.empty()) {
    code.clear();
    return;
  }
  auto first_line_it = std::find_if(
      lines.begin(),
      lines.end(),
      [](auto const& s)
      { return s.find_first_not_of(" \t\r\n") != std::string::npos; });
  if (first_line_it == lines.end()) {
    code.clear();
    return;
  }
  auto last_line_it = std::find_if(
      lines.rbegin(),
      lines.rend(),
      [](auto const& s)
      { return s.find_first_not_of(" \t\r\n") != std::string::npos; });
  std::vector<std::string> trimmed_lines(first_line_it, last_line_it.base());
  size_t min_indent = std::string::npos;
  for (auto const& l : trimmed_lines) {
    size_t first_char = l.find_first_not_of(" \t");
    if (first_char != std::string::npos
        && (min_indent == std::string::npos || first_char < min_indent))
    {
      min_indent = first_char;
    }
  }
  if (min_indent == 0 || min_indent == std::string::npos) {
    std::ostringstream oss;
    for (size_t i = 0; i < trimmed_lines.size(); ++i) {
      oss << trimmed_lines[i] << (i == trimmed_lines.size() - 1 ? "" : "\n");
    }
    code = oss.str();
    return;
  }
  std::ostringstream oss;
  for (size_t i = 0; i < trimmed_lines.size(); ++i) {
    auto const& l = trimmed_lines[i];
    if (l.find_first_not_of(" \t") != std::string::npos
        && l.length() >= min_indent)
    {
      oss << l.substr(min_indent);
    } else {
      oss << l;
    }
    if (i < trimmed_lines.size() - 1) {
      oss << "\n";
    }
  }
  code = oss.str();
}

void AstBuilder::build_rhs(pegtl::parse_tree::node const& n, ParsedRule& rule)
{
  std::stringstream rhs_stream;
  for (auto const& element_ptr : n.children) {
    if (!element_ptr || !element_ptr->is_type<grammar::rhs_element>()
        || element_ptr->children.empty())
    {
      continue;
    }
    auto const& content_node = *element_ptr->children.front();
    if (content_node.is_type<grammar::code_chunk>()) {
      rhs_stream << content_node.string() << "\n";
    } else if (content_node.is_type<grammar::modify_statement>()) {
      rhs_stream << build_modify_statement(content_node) << "\n";
    }
  }
  std::string code = rhs_stream.str();
  normalize_indentation(code);
  rule.rhs_code = std::move(code);
  logd("AstBuilder::build_rhs for rule '{}' -> code:\n{}",
            rule.name,
            rule.rhs_code);
}

std::string AstBuilder::build_modify_statement(pegtl::parse_tree::node const& n)
{
  auto const* target_node = find_descendant<grammar::modify_target>(n);
  auto const* body_node = find_descendant<grammar::modify_body>(n);
  if (!target_node || !body_node) {
    return "";
  }
  std::stringstream ss;
  ss << "rfl.update(" << target_node->string() << ", {";
  bool first = true;
  for (auto const& setter_ptr : body_node->children) {
    if (!setter_ptr->is_type<grammar::modify_setter>()) {
      continue;
    }
    auto const* name_node =
        find_descendant<grammar::modify_setter_name>(*setter_ptr);
    auto const* args_node =
        find_descendant<grammar::modify_setter_args>(*setter_ptr);
    if (name_node && args_node) {
      std::string setter_name = name_node->string();
      std::string field_name = setter_name;
      if (setter_name.rfind("set", 0) == 0 && setter_name.length() > 3
          && std::isupper(setter_name[3]))
      {
        field_name = setter_name.substr(3);
        field_name[0] = std::tolower(field_name[0]);
      }
      std::string args_content = args_node->string();
      if (args_content.length() >= 2) {
        args_content = args_content.substr(1, args_content.length() - 2);
      }
      if (!first) {
        ss << ", ";
      }
      ss << field_name << ": " << args_content;
      first = false;
    }
  }
  ss << "})";
  return ss.str();
}

ParsedPattern AstBuilder::build_pattern(pegtl::parse_tree::node const& n)
{
  ParsedPattern pattern;
  pattern.pos = n.begin();
  if (auto const* binding_node = find_descendant<grammar::variable_binding>(n))
  {
    bool is_outermost_binding = true;
    for (auto const& child : n.children) {
      if (find_descendant<grammar::standard_pattern_body>(*child)
          || find_descendant<grammar::not_pattern_body>(*child)
          || find_descendant<grammar::exists_pattern_body>(*child))
      {
        if (binding_node->begin().byte > child->begin().byte) {
          is_outermost_binding = false;
          break;
        }
      }
    }
    if (is_outermost_binding) {
      pattern.binding = binding_node->string();
    }
  }
  if (auto const* not_body = find_descendant<grammar::not_pattern_body>(n)) {
    pattern.type = PatternType::NOT;
    // Try parenthesized form first: not (pattern)
    if (auto const* nested_node = find_descendant<grammar::pattern>(*not_body))
    {
      pattern.nested_patterns.push_back(build_pattern(*nested_node));
    }
    // Fallback to bare form: not Pattern(...)
    else if (auto const* std_body = find_descendant<grammar::standard_pattern_body>(*not_body))
    {
      ParsedPattern nested;
      nested.pos = std_body->begin();
      nested.type = PatternType::STANDARD;
      if (auto const* fact_type_node = find_descendant<grammar::fact_type_name>(*std_body)) {
        nested.fact_type = fact_type_node->string();
      }
      if (auto const* constraints = find_descendant<grammar::pattern_constraints>(*std_body)) {
        if (auto const* expr = find_descendant<grammar::expression>(*constraints)) {
          nested.constraint_root = build_constraint_expression(*expr);
        }
      }
      pattern.nested_patterns.push_back(std::move(nested));
    }
  } else if (auto const* exists_body =
                 find_descendant<grammar::exists_pattern_body>(n))
  {
    pattern.type = PatternType::EXISTS;
    // Try parenthesized form first: exists (pattern)
    if (auto const* nested_node =
            find_descendant<grammar::pattern>(*exists_body))
    {
      pattern.nested_patterns.push_back(build_pattern(*nested_node));
    }
    // Fallback to bare form: exists Pattern(...)
    else if (auto const* std_body = find_descendant<grammar::standard_pattern_body>(*exists_body))
    {
      ParsedPattern nested;
      nested.pos = std_body->begin();
      nested.type = PatternType::STANDARD;
      if (auto const* fact_type_node = find_descendant<grammar::fact_type_name>(*std_body)) {
        nested.fact_type = fact_type_node->string();
      }
      if (auto const* constraints = find_descendant<grammar::pattern_constraints>(*std_body)) {
        if (auto const* expr = find_descendant<grammar::expression>(*constraints)) {
          nested.constraint_root = build_constraint_expression(*expr);
        }
      }
      pattern.nested_patterns.push_back(std::move(nested));
    }
  } else if (auto const* forall_body =
                 find_descendant<grammar::forall_pattern_body>(n))
  {
    pattern.type = PatternType::FORALL;
    pattern.forall_info.emplace();
    if (auto const* list_node =
            find_descendant<grammar::forall_pattern_list>(*forall_body))
    {
      for (auto const& nested_node : list_node->children) {
        pattern.forall_info->patterns.push_back(build_pattern(*nested_node));
      }
    }
  } else if (auto const* query_body =
                 find_descendant<grammar::query_call_pattern_body>(n))
  {
    pattern.type = PatternType::QUERY_CALL;
    ParsedQueryCall call;
    if (auto const* name_node =
            find_descendant<grammar::query_call_name>(*query_body))
    {
      std::string name = name_node->string();
      if (name.size() >= 2 && name.front() == '"' && name.back() == '"') {
        name = name.substr(1, name.size() - 2);
      }
      call.query_name = name;
    }
    auto args = find_all_descendants<grammar::query_call_arg>(*query_body);
    for (auto const* arg_node : args) {
      call.arguments.push_back(arg_node->string());
    }
    pattern.source = std::move(call);
  } else if (auto const* body =
                 find_descendant<grammar::standard_pattern_body>(n))
  {
    pattern.type = PatternType::STANDARD;
    if (auto const* fact_type_node =
            find_descendant<grammar::fact_type_name>(*body))
    {
      pattern.fact_type = fact_type_node->string();
    }

    // Debug: Log children of standard_pattern_body
    logd("build_pattern: standard_pattern_body for '{}' has {} direct children",
         pattern.fact_type, body->children.size());
    for (size_t i = 0; i < body->children.size(); ++i) {
      logd("  child[{}]: type={}", i, tlog::format(body->children[i]->type));
    }

    // Try find_child first (direct child), then fallback to find_descendant
    // but only if there's no from_clause (to avoid picking up accumulate source pattern's constraints)
    pegtl::parse_tree::node const* constraints = find_child<grammar::pattern_constraints>(*body);
    logd("build_pattern: find_child<pattern_constraints> = {}", constraints ? "FOUND" : "NOT FOUND");
    if (!constraints) {
      // Fallback: check if pattern_constraints is a descendant but NOT inside from_clause
      auto const* from_node_check = find_descendant<grammar::from_clause>(*body);
      auto const* desc_constraints = find_descendant<grammar::pattern_constraints>(*body);
      logd("build_pattern: fallback - from_node_check={}, desc_constraints={}",
           from_node_check ? "FOUND" : "NOT FOUND",
           desc_constraints ? "FOUND" : "NOT FOUND");
      if (desc_constraints && (!from_node_check || !find_descendant<grammar::pattern_constraints>(*from_node_check))) {
        constraints = desc_constraints;
        logd("build_pattern: using fallback desc_constraints");
      }
    }
    if (constraints) {
      if (auto const* expr = find_descendant<grammar::expression>(*constraints))
      {
        pattern.constraint_root = build_constraint_expression(*expr);
      }
    }
    // Try find_child first, then fallback to find_descendant for from_clause
    auto const* from_node = find_child<grammar::from_clause>(*body);
    logd("build_pattern: find_child<from_clause> = {}", from_node ? "FOUND" : "NOT FOUND");
    if (!from_node) {
      from_node = find_descendant<grammar::from_clause>(*body);
      logd("build_pattern: find_descendant<from_clause> = {}", from_node ? "FOUND" : "NOT FOUND");
    }
    if (from_node) {
      logd("build_pattern: calling build_from_clause");
      build_from_clause(*from_node, pattern);
    } else {
      logd("build_pattern: NO from_clause found!");
    }
  }
  logd(
      "AstBuilder::build_pattern -> Created pattern type {}, fact_type '{}', "
      "binding '{}'",
      (int)pattern.type,
      pattern.fact_type,
      pattern.binding);
  return pattern;
}

void AstBuilder::build_from_clause(pegtl::parse_tree::node const& n,
                                   ParsedPattern& pattern)
{
  logd("build_from_clause: node type={}, has {} children", tlog::format(n.type), n.children.size());
  if (n.children.empty()) {
    logd("build_from_clause: returning early - no children");
    return;
  }
  auto const* from_body = n.children.front().get();
  if (!from_body) {
    logd("build_from_clause: returning early - null from_body");
    return;
  }
  logd("build_from_clause: from_body type={}", tlog::format(from_body->type));
  if (from_body->is_type<grammar::from_accumulate_clause>()) {
    logd("build_from_clause: detected from_accumulate_clause");
    ParsedAccumulate acc_info;
    if (auto const* source_pattern_node =
            find_descendant<grammar::accumulate_source_pattern>(*from_body))
    {
      acc_info.source_pattern = std::make_unique<ParsedPattern>(
          build_source_pattern(*source_pattern_node));
    }
    if (auto const* func_node =
            find_descendant<grammar::accumulate_function>(*from_body))
    {
      if (auto const* name_node =
              find_descendant<grammar::accumulate_function_name>(*func_node))
      {
        acc_info.function = name_node->string();
      }
      if (auto const* field_node =
              find_descendant<grammar::accumulate_source_ref>(*func_node))
      {
        acc_info.field = field_node->string();
        if (acc_info.field.find_first_of("+-*/") != std::string::npos) {
          acc_info.accumulate_expr_ast = parse_arith_expr_string(acc_info.field);
        }
      }
    }
    pattern.source = std::move(acc_info);
  } else if (from_body->is_type<grammar::from_collect_clause>()) {
    ParsedAccumulate acc_info;
    acc_info.function = "collect";
    if (auto const* source_pattern_node =
            find_descendant<grammar::pattern>(*from_body))
    {
      acc_info.source_pattern =
          std::make_unique<ParsedPattern>(build_pattern(*source_pattern_node));
      if (!acc_info.source_pattern->binding.empty()) {
        acc_info.field = acc_info.source_pattern->binding;
      }
    }
    pattern.source = std::move(acc_info);
  } else if (from_body->is_type<grammar::from_unnest_clause>()) {
    ParsedUnnest unnest_info;
    if (auto const* source_node =
            find_descendant<grammar::unnest_source>(*from_body))
    {
      std::string src = source_node->string();
      size_t first_dot = src.find('.');
      if (first_dot != std::string::npos && src[0] == '$') {
        unnest_info.source_binding = src.substr(0, first_dot);
        unnest_info.source_field = src.substr(first_dot + 1);
      }
    }
    pattern.source = std::move(unnest_info);
  } else if (from_body->is_type<grammar::from_entry_point_clause>()) {
    if (auto const* entry_point_name_node =
            find_descendant<grammar::entry_point_name>(*from_body))
    {
      std::string name_str = entry_point_name_node->string();
      if (name_str.length() >= 2) {
        pattern.source = name_str.substr(1, name_str.length() - 2);
      }
    }
  }
}

std::unique_ptr<ConstraintNode> AstBuilder::build_constraint_expression(
    pegtl::parse_tree::node const& n)
{
  logd("AstBuilder::build_constraint_expression");
  return build_logical_op_node(n, NodeType::OR);
}

std::unique_ptr<ConstraintNode> AstBuilder::build_logical_op_node(
    pegtl::parse_tree::node const& n, NodeType type)
{
  logd("AstBuilder::build_logical_op_node (type: {})", (int)type);
  if (n.children.empty()) {
    return nullptr;
  }
  if (n.children.size() == 1) {
    auto const& child = *n.children[0];
    if (type == NodeType::OR) {
      return build_logical_op_node(child, NodeType::AND);
    } else {
      return build_constraint_item(child);
    }
  }
  auto op_node = std::make_unique<ConstraintNode>(type);
  for (auto const& child_ptr : n.children) {
    std::unique_ptr<ConstraintNode> child_node;
    if (type == NodeType::OR) {
      child_node = build_logical_op_node(*child_ptr, NodeType::AND);
    } else {
      child_node = build_constraint_item(*child_ptr);
    }
    if (child_node) {
      op_node->children.push_back(std::move(child_node));
    }
  }
  if (op_node->children.empty()) {
    return nullptr;
  }
  return op_node;
}

ConstraintValue AstBuilder::build_literal(pegtl::parse_tree::node const& n)
{
  if (find_descendant<grammar::integer>(n)) {
    return parse_long_safe(n.string());
  }
  if (find_descendant<grammar::double_>(n)) {
    return parse_double_safe(n.string());
  }
  if (auto* s = find_descendant<grammar::string_literal>(n)) {
    std::string str = s->string();
    return str.length() >= 2 ? str.substr(1, str.length() - 2) : "";
  }
  if (find_descendant<grammar::keyword_true>(n)) {
    return (int64_t)1;
  }
  if (find_descendant<grammar::keyword_false>(n)) {
    return (int64_t)0;
  }
  if (find_descendant<grammar::keyword_nil>(n)) {
    return NilValue {};
  }

  // Fallback for anything else that might have been parsed as a primary
  // expression but isn't one of the above. This can happen with single-word
  // identifiers that aren't bound variables. We treat them as strings.
  return n.string();
}
// This parses expressions like "$avail - $reserved" or "$price * $qty"
ArithExprValue AstBuilder::parse_arith_expr_string(std::string const& expr_str) {
    // Tokenize the expression
    std::vector<std::string> tokens;
    std::string current_token;

    for (size_t i = 0; i < expr_str.size(); ++i) {
        char c = expr_str[i];
        if (c == ' ' || c == '\t' || c == '\n') {
            if (!current_token.empty()) {
                tokens.push_back(current_token);
                current_token.clear();
            }
        } else if (c == '+' || c == '-' || c == '*' || c == '/') {
            if (!current_token.empty()) {
                tokens.push_back(current_token);
                current_token.clear();
            }
            tokens.push_back(std::string(1, c));
        } else {
            current_token += c;
        }
    }
    if (!current_token.empty()) {
        tokens.push_back(current_token);
    }

    if (tokens.empty()) {
        return 0.0;
    }

    // Simple recursive descent parser for binary expressions
    // Handles: term ((+|-|*|/) term)*
    // For now, evaluate left-to-right (no operator precedence within this simple parser)

    auto parse_term = [](std::string const& t) -> ArithExprValue {
        // Check if it's a number
        bool is_number = true;
        bool has_dot = false;
        for (size_t i = 0; i < t.size(); ++i) {
            char c = t[i];
            if (c == '-' && i == 0) continue;
            if (c == '.' && !has_dot) { has_dot = true; continue; }
            if (!std::isdigit(c)) { is_number = false; break; }
        }
        if (is_number && !t.empty()) {
            return std::stod(t);
        }
        // It's a variable reference or field name
        return t;
    };

    if (tokens.size() == 1) {
        return parse_term(tokens[0]);
    }

    // Build AST from left to right
    ArithExprValue result = parse_term(tokens[0]);

    for (size_t i = 1; i + 1 < tokens.size(); i += 2) {
        std::string const& op_str = tokens[i];
        std::string const& right_str = tokens[i + 1];

        ArithOp op;
        if (op_str == "+") op = ArithOp::ADD;
        else if (op_str == "-") op = ArithOp::SUB;
        else if (op_str == "*") op = ArithOp::MUL;
        else if (op_str == "/") op = ArithOp::DIV;
        else continue;  // Unknown operator, skip

        ArithExprValue right_val = parse_term(right_str);

        auto node = std::make_unique<ArithExprNode>(op, std::move(result), std::move(right_val));
        result = std::move(node);
    }

    return result;
}

std::vector<ConstraintValue> AstBuilder::build_value_list(
    pegtl::parse_tree::node const& n)
{
  std::vector<ConstraintValue> values;
  // Find the value_list_content which contains the comma-separated primary_expr nodes
  if (auto const* content_node = find_descendant<grammar::value_list_content>(n)) {
    for (auto const& child : content_node->children) {
      if (child->is_type<grammar::primary_expr>()) {
        values.push_back(build_literal(*child));
      }
    }
  }
  logd("AstBuilder::build_value_list -> Extracted {} values", values.size());
  return values;
}

ParsedDeclaration AstBuilder::build_declaration(
    pegtl::parse_tree::node const& n)
{
  ParsedDeclaration decl;
  if (auto const* name_node = find_child<grammar::declared_type_name>(n)) {
    decl.type_name = name_node->string();
  }
  logd(
      "AstBuilder::build_declaration -> Building declaration for type '{}'",
      decl.type_name);
  if (auto const* body_node = find_child<grammar::declaration_body>(n)) {
    for (auto const& seq_child_ptr : body_node->children) {
      if (auto const* field_def_node =
              find_child<grammar::field_definition>(*seq_child_ptr))
      {
        if (auto const* name_node =
                find_child<grammar::field_name>(*field_def_node))
        {
          if (auto const* type_node =
                  find_child<grammar::field_type>(*field_def_node))
          {
            decl.fields.push_back({name_node->string(), type_node->string()});
          }
        }
      }
    }
  }
  return decl;
}

std::unique_ptr<ConstraintNode> AstBuilder::build_constraint_item(
    pegtl::parse_tree::node const& n)
{
  logd("AstBuilder::build_constraint_item");
  auto leaf_node = std::make_unique<ConstraintNode>(NodeType::LEAF);

  // --- 1. Check for specific, high-priority temporal constraints first ---

  // Handles: `within 60s of $e1`
  if (auto const* window_node =
          find_descendant<grammar::temporal_window_clause>(n))
  {
    ParsedTemporalConstraint tc;
    tc.op = "within";
    tc.lhs_field = "timestamp";  // 'within' implies a check against the fact's
                                 // own timestamp
    std::string anchor_binding =
        find_descendant<grammar::variable_binding>(*window_node)->string();
    tc.rhs_binding_and_field = {
        anchor_binding,
        "timestamp"};  // The anchor point is the other event's timestamp
    auto duration_str =
        find_descendant<grammar::duration_literal>(*window_node)->string();
    tc.window_ms = parse_duration_to_ms(duration_str);
    leaf_node->constraint.temporal_constraint = tc;
    return leaf_node;
  }

  // Handles: `timestamp after $e1.timestamp`
  if (auto const* seq_node = find_descendant<grammar::temporal_seq_clause>(n)) {
    ParsedTemporalConstraint tc;
    tc.op = find_descendant<pegtl::sor<grammar::op_after, grammar::op_before, grammar::op_coincides, grammar::op_during>>(
                *seq_node)
                ->string();
    tc.lhs_field = extract_field_name(
        find_all_descendants<grammar::primary_expr>(*seq_node)[0]);
    std::string rhs = extract_field_name(
        find_all_descendants<grammar::primary_expr>(*seq_node)[1]);
    size_t pos = rhs.find('.');
    if (pos != std::string::npos && rhs[0] == '$') {
      tc.rhs_binding_and_field = {rhs.substr(0, pos), rhs.substr(pos + 1)};
    }
    leaf_node->constraint.temporal_constraint = tc;
    return leaf_node;
  }
  // --- 2. If not temporal, parse it as a standard relational expression ---

  auto const* binding_node = find_descendant<grammar::inline_binding>(n);
  auto const* primary_expr_node = find_descendant<grammar::primary_expr>(n);
  auto const* clause_node = find_descendant<grammar::cmp_clause>(n);
  auto const* in_clause_node = find_descendant<grammar::in_clause>(n);

  // Handle inline bindings like `$v : field`
  if (binding_node) {
    leaf_node->constraint.field_binding =
        find_descendant<grammar::variable_binding>(*binding_node)->string();
  }
  if (!primary_expr_node) {
    return nullptr;  // Should not happen in a valid parse
  }

  // The LHS of the constraint is always the first primary expression
  leaf_node->constraint.left_field = extract_field_name(primary_expr_node);

  // Check if there is an IN clause (e.g., `status in ("Gold", "Platinum")`)
  if (in_clause_node) {
    // Determine if it's "in" or "not in"
    if (find_descendant<grammar::not_in_op>(*in_clause_node)) {
      leaf_node->constraint.op = "not in";
    } else {
      leaf_node->constraint.op = "in";
    }
    // Extract the value list
    if (auto const* value_list_node = find_descendant<grammar::value_list>(*in_clause_node)) {
      leaf_node->constraint.right_value_list = build_value_list(*value_list_node);
    }
  }
  // Check if there is a comparison clause (e.g., `> 18`)
  else if (clause_node) {
    leaf_node->constraint.op =
        find_descendant<grammar::cmp_op>(*clause_node)->string();
    auto* rhs_node = find_descendant<grammar::primary_expr>(*clause_node);
    if (rhs_node) {
      // This must come before arith_expr check because arith_expr greedily matches single terms
      if (auto* cf_node = find_descendant<grammar::constraint_field>(*rhs_node))
      {
        std::string rhs_full_name = cf_node->string();
        if (!rhs_full_name.empty() && rhs_full_name[0] == '$') {
          // Check if this is actually an arithmetic expression (contains operators)
          if (auto* arith_node = find_descendant<grammar::arith_expr>(*rhs_node)) {
            std::string arith_str = arith_node->string();
            // Only treat as arithmetic if it contains operators
            if (arith_str.find_first_of("+-*/") != std::string::npos) {
              leaf_node->constraint.right_arith_expr = arith_str;
              leaf_node->constraint.right_arith_ast = parse_arith_expr_string(arith_str);
            } else {
              // It's a simple variable reference
              size_t pos = rhs_full_name.find('.');
              if (pos != std::string::npos) {
                leaf_node->constraint.right_bound_field = {
                    {rhs_full_name.substr(0, pos), rhs_full_name.substr(pos + 1)}};
              } else {
                // A bare variable like '$p' refers to the whole fact
                leaf_node->constraint.right_bound_field = {{rhs_full_name, "this"}};
              }
            }
          } else {
            size_t pos = rhs_full_name.find('.');
            if (pos != std::string::npos) {
              leaf_node->constraint.right_bound_field = {
                  {rhs_full_name.substr(0, pos), rhs_full_name.substr(pos + 1)}};
            } else {
              // A bare variable like '$p' refers to the whole fact
              leaf_node->constraint.right_bound_field = {{rhs_full_name, "this"}};
            }
          }
        } else {
          leaf_node->constraint.right_literal = build_literal(*rhs_node);
        }
      }
      // Check if the RHS contains an arithmetic expression (e.g., `$ts - 60000`)
      else if (auto* arith_node = find_descendant<grammar::arith_expr>(*rhs_node)) {
        std::string arith_str = arith_node->string();
        // Only treat as arithmetic if it contains operators
        if (arith_str.find_first_of("+-*/") != std::string::npos) {
          leaf_node->constraint.right_arith_expr = arith_str;
          leaf_node->constraint.right_arith_ast = parse_arith_expr_string(arith_str);
        } else {
          leaf_node->constraint.right_literal = build_literal(*rhs_node);
        }
      } else {
        leaf_node->constraint.right_literal = build_literal(*rhs_node);
      }
    }
  } else {
    // A constraint with no operator is ONLY a check if there is no inline
    // binding. e.g., Person(name) implies name == true BUT Person($n : name) is
    // ONLY a binding, not a check.
    if (!binding_node) {
      leaf_node->constraint.op = "==";
      leaf_node->constraint.right_literal = (int64_t)1;
    }
    // If there IS a binding_node but NO clause_node, we do nothing.
    // The constraint is just for binding, not for filtering.
  }

  // If we only have a binding and nothing else, it's not a filter. Return
  // nullptr so it doesn't become an AlphaNode.
  if (leaf_node->constraint.op.empty() && binding_node && !clause_node) {
    // This is a pure binding like `($id : id)`. It should not produce a filter
    // node. However, the binding info is already captured in the ParsedPattern.
    // We need to return the leaf_node so the semantic analyzer can see the
    // binding. The AlphaNode check needs to be smarter.
  }
  return leaf_node;
}

ParsedQuery AstBuilder::build_query(pegtl::parse_tree::node const& n)
{
  ParsedQuery query;
  query.pos = n.begin();
  if (auto const* name_node = find_descendant<grammar::query_name>(n)) {
    if (find_descendant<grammar::string_literal>(*name_node)) {
      std::string name_str = name_node->string();
      if (name_str.length() >= 2) {
        query.name = name_str.substr(1, name_str.length() - 2);
      }
    } else {
      query.name = name_node->string();
    }
  }
  logd("AstBuilder::build_query -> Building query '{}'", query.name);
  auto param_nodes = find_all_descendants<grammar::query_parameter>(n);
  query.parameter_count = param_nodes.size();
  for (auto const* param_node_ptr : param_nodes) {
    ParsedPattern p;
    p.pos = param_node_ptr->begin();
    p.type = PatternType::STANDARD;
    if (auto const* type_node =
            find_child<grammar::fact_type_name>(*param_node_ptr))
    {
      p.fact_type = type_node->string();
      query.parameter_types.push_back(p.fact_type);
    }
    if (auto const* binding_node =
            find_child<grammar::variable_binding>(*param_node_ptr))
    {
      p.binding = binding_node->string();
    }
    query.patterns.push_back(std::move(p));
  }
  if (auto const* lhs_node = find_descendant<grammar::lhs>(n)) {
    std::vector<std::vector<ParsedPattern>> condition_groups;
    build_lhs(*lhs_node, condition_groups);
    if (!condition_groups.empty()) {
      for (auto& p : condition_groups.front()) {
        query.patterns.push_back(std::move(p));
      }
    }
  }
  return query;
}

ParsedFunction AstBuilder::build_function(pegtl::parse_tree::node const& n)
{
  return {};
}

ParsedGlobal AstBuilder::build_global(pegtl::parse_tree::node const& n)
{
  ParsedGlobal global;
  if (auto const* type_node = find_child<grammar::global_type>(n)) {
    global.type = type_node->string();
  }
  if (auto const* name_node = find_child<grammar::global_name>(n)) {
    global.name = name_node->string();
  }
  return global;
}

void AstBuilder::build_lhs(
    pegtl::parse_tree::node const& n,
    std::vector<std::vector<ParsedPattern>>& condition_groups)
{
  condition_groups.clear();
  for (auto const& list_child : n.children) {
    if (!list_child->is_type<grammar::lhs_and_block>()) {
      continue;
    }
    pegtl::parse_tree::node const* and_block =
        find_descendant<grammar::single_and_block>(*list_child);
    if (!and_block) {
      continue;
    }
    std::vector<ParsedPattern> current_group;
    for (auto const& seq_node_ptr : and_block->children) {
      if (auto const* pattern_node =
              find_child<grammar::pattern>(*seq_node_ptr))
      {
        current_group.push_back(build_pattern(*pattern_node));
      }
    }
    if (!current_group.empty()) {
      condition_groups.push_back(std::move(current_group));
    }
  }
}

void AstBuilder::build_import(pegtl::parse_tree::node const& n,
                              parser_state& state)
{
  if (auto const* name_node = find_descendant<grammar::import_name>(n)) {
    state.parsed_imports.push_back(name_node->string());
  }
}

void AstBuilder::build_annotations(pegtl::parse_tree::node const& n,
                                   ParsedRule& rule)
{
  for (auto const& annotation_node_ptr : n.children) {
    if (!annotation_node_ptr
        || !annotation_node_ptr->is_type<grammar::annotation>())
    {
      continue;
    }
    auto const& annotation_node = *annotation_node_ptr;
    auto const* name_node =
        find_child<grammar::annotation_name>(annotation_node);
    if (!name_node) {
      continue;
    }
    std::string name = name_node->string();
    std::string value = "true";
    if (auto const* value_node =
            find_child<grammar::annotation_value>(annotation_node))
    {
      std::string full_value_str = value_node->string();
      if (full_value_str.length() >= 2) {
        value = full_value_str.substr(1, full_value_str.length() - 2);
      }
    }
    rule.annotations[name] = value;
  }
}

void AstBuilder::build_package(pegtl::parse_tree::node const& n,
                               parser_state& state)
{
  if (auto const* name_node = find_descendant<grammar::package_name>(n)) {
    state.package_name = name_node->string();
    logd("AstBuilder::build_package -> Set package name to '{}'",
              state.package_name);
  }
}

ParsedPattern AstBuilder::build_source_pattern(pegtl::parse_tree::node const& n)
{
  ParsedPattern p;
  p.pos = n.begin();
  p.type = PatternType::STANDARD;
  pegtl::parse_tree::node const* binding_node = nullptr;
  pegtl::parse_tree::node const* body_node = nullptr;
  for (auto const& child : n.children) {
    if (auto* found_binding =
            find_descendant<grammar::variable_binding>(*child))
    {
      binding_node = found_binding;
    }
    if (child->is_type<grammar::standard_pattern_body_no_from>()) {
      body_node = child.get();
    }
  }
  if (binding_node) {
    p.binding = binding_node->string();
  }
  if (body_node) {
    if (auto const* fact_type_node =
            find_descendant<grammar::fact_type_name>(*body_node))
    {
      p.fact_type = fact_type_node->string();
    }
    if (auto const* constraints =
            find_descendant<grammar::pattern_constraints>(*body_node))
    {
      if (auto const* expr = find_descendant<grammar::expression>(*constraints))
      {
        p.constraint_root = build_constraint_expression(*expr);
      }
    }
  }
  // Handle nested from entry-point clause (for CEP within accumulate)
  if (auto const* entry_point_clause = find_descendant<grammar::from_entry_point_clause>(n)) {
    if (auto const* entry_point_name_node = find_descendant<grammar::entry_point_name>(*entry_point_clause)) {
      std::string name_str = entry_point_name_node->string();
      if (name_str.length() >= 2) {
        p.source = name_str.substr(1, name_str.length() - 2);  // Remove quotes
      }
    }
  }
  return p;
}


