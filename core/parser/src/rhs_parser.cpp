#include "rhs_parser.hpp"
#include "diagnostics/logging_control.hpp"
#include "expression_descriptor.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <regex>
#include <unordered_set>
#include <string_view>
#include "rhs_builtins.hpp"

namespace rulesforge {


// ============================================================================
// Parser Implementation
// ============================================================================

RhsParser::Parser::Parser(std::string const &input,
                          std::map<std::string, int> const &bindings,
                          std::unordered_set<std::string> const &globals)
    : lexer_(input), bindings_(bindings), globals_(globals) {
  current_ = lexer_.next();
}

bool RhsParser::Parser::match(Token::Type type) {
  if (check(type)) {
    advance();
    return true;
  }
  return false;
}

bool RhsParser::Parser::check(Token::Type type) { return current_.type == type; }

RhsParser::Token RhsParser::Parser::advance() {
  Token prev = current_;
  current_ = lexer_.next();
  return prev;
}

RhsParser::Token RhsParser::Parser::consume(Token::Type type, std::string const &message) {
  if (check(type))
    return advance();
  error(message);
  return {Token::TOK_ERROR, "", current_.line, current_.column};
}

void RhsParser::Parser::error(std::string const &message) {
  if (!had_error_) {
    error_ = "Line " + std::to_string(current_.line) + ", column " +
             std::to_string(current_.column) + ": " +
             (current_.type == Token::TOK_ERROR ? std::string(current_.text) : message);
    had_error_ = true;
  }
}

bool RhsParser::Parser::is_fact_binding_available(std::string const& name) const {
  return bindings_.find(name) != bindings_.end()
      || local_bindings_.find(name) != local_bindings_.end();
}

bool RhsParser::Parser::is_value_source_available(std::string const& name) const {
  return is_fact_binding_available(name)
      || result_bindings_.contains(name)
      || globals_.find(name) != globals_.end();
}

bool RhsParser::Parser::validate_value_source_ref(std::string const& ref) {
  if (ref.empty() || ref[0] != '$') {
    return true;
  }

  size_t const dot_pos = ref.find('.');
  std::string const base = dot_pos == std::string::npos ? ref : ref.substr(0, dot_pos);
  if (!is_value_source_available(base)) {
    error("RHS uses undeclared variable '" + base + "'");
    return false;
  }
  return true;
}

bool RhsParser::Parser::validate_expression_sources(rulesforge::ExpressionDescriptor const& expression) {
  for (auto const& variable : expression.variables()) {
    if (!validate_value_source_ref(variable)) {
      return false;
    }
  }
  return true;
}

std::vector<CompiledAction> RhsParser::Parser::parse() {
  std::vector<CompiledAction> actions;

  while (!check(Token::TOK_END) && !had_error_) {
    actions.push_back(parse_action());
  }

  if (had_error_) {
    return {};
  }
  return actions;
}

CompiledAction RhsParser::Parser::parse_action() {
  switch (current_.type) {
  case Token::TOK_UPDATE:
    return parse_update();
  case Token::TOK_INSERT:
    return parse_insert();
  case Token::TOK_INSERT_LOGICAL:
    return parse_insert_logical();
  case Token::TOK_RETRACT:
    return parse_retract();
  case Token::TOK_HALT:
    return parse_halt();
  case Token::TOK_SET_FOCUS:
    return parse_set_focus();
  case Token::TOK_IF:
    return parse_if();
  case Token::TOK_FOR:
    return parse_for();
  case Token::TOK_WHILE:
    return parse_while();
  case Token::TOK_SWITCH:
    return parse_switch();
  case Token::TOK_BREAK:
    return parse_break();
  case Token::TOK_CONTINUE:
    return parse_continue();
  default:
    if (current_.type == Token::TOK_IDENTIFIER && current_.text == "call") return parse_call();
    error("Expected action keyword (update, insert, retract, halt, setFocus, if, for, "
          "while, switch, break, continue)");
    return {};
  }
}

std::vector<CompiledAction> RhsParser::Parser::parse_scoped_actions() {
  auto outer = result_bindings_;
  std::vector<CompiledAction> actions;
  while (!check(Token::TOK_RBRACE) && !check(Token::TOK_END) && !had_error_)
    actions.push_back(parse_action());
  result_bindings_ = std::move(outer);
  return actions;
}

CompiledAction RhsParser::Parser::parse_call() {
  CompiledAction action;
  action.type = RhsActionType::CALL;
  advance();
  action.script_binding = consume(Token::TOK_STRING, "Expected quoted script binding after call").text;
  if (action.script_binding.empty()) error("Script binding cannot be empty");
  consume(Token::TOK_LPAREN, "Expected '(' after script binding");
  if (!check(Token::TOK_RPAREN)) {
    do {
      auto value = parse_value();
      if (value.numeric_expr) error("CALL arguments must be literals or typed value references");
      if (value.type == RhsValueType::VAR_REF && value.var_ref.find('.') == std::string::npos
          && is_fact_binding_available(value.var_ref)) error("CALL requires explicit fact fields");
      action.script_arguments.push_back(std::move(value));
    } while (!had_error_ && match(Token::TOK_COMMA));
  }
  consume(Token::TOK_RPAREN, "Expected ')' after CALL arguments");
  auto into = consume(Token::TOK_IDENTIFIER, "Expected 'into' after CALL arguments");
  if (into.text != "into") error("Expected 'into' after CALL arguments");
  action.script_result_var = consume(Token::TOK_VARIABLE, "Expected result variable after into").text;
  if (is_value_source_available(action.script_result_var)) error("CALL result shadows an existing binding");
  result_bindings_.insert(action.script_result_var);
  return action;
}

CompiledAction RhsParser::Parser::parse_update() {
  CompiledAction action;
  action.type = RhsActionType::UPDATE;

  advance(); // consume 'update'
  Token var = consume(Token::TOK_VARIABLE, "Expected variable after 'update'");
  action.target_var = var.text;
  if (!var.text.empty() && !is_fact_binding_available(action.target_var)) {
    error("RHS uses undeclared variable '" + action.target_var + "'");
  }

  consume(Token::TOK_LBRACE, "Expected '{' after variable");
  action.assignments = parse_field_assignments();
  consume(Token::TOK_RBRACE, "Expected '}' after field assignments");

  return action;
}

CompiledAction RhsParser::Parser::parse_insert() {
  CompiledAction action;
  action.type = RhsActionType::INSERT;

  advance(); // consume 'insert'
  Token type_name = consume(Token::TOK_IDENTIFIER, "Expected type name after 'insert'");
  action.target_type = type_name.text;

  consume(Token::TOK_LBRACE, "Expected '{' after type name");
  action.assignments = parse_field_assignments();
  consume(Token::TOK_RBRACE, "Expected '}' after field assignments");

  return action;
}

CompiledAction RhsParser::Parser::parse_insert_logical() {
  CompiledAction action;
  action.type = RhsActionType::INSERT_LOGICAL;

  advance(); // consume 'insertLogical'
  Token type_name = consume(Token::TOK_IDENTIFIER, "Expected type name after 'insertLogical'");
  action.target_type = type_name.text;

  consume(Token::TOK_LBRACE, "Expected '{' after type name");
  action.assignments = parse_field_assignments();
  consume(Token::TOK_RBRACE, "Expected '}' after field assignments");

  return action;
}

CompiledAction RhsParser::Parser::parse_retract() {
  CompiledAction action;
  action.type = RhsActionType::RETRACT;

  advance(); // consume 'retract'
  Token var = consume(Token::TOK_VARIABLE, "Expected variable after 'retract'");
  action.target_var = var.text;

  // Validate that the variable is declared in LHS bindings
  if (!var.text.empty() && !is_fact_binding_available(action.target_var)) {
    error("RHS uses undeclared variable '" + action.target_var + "'");
  }

  return action;
}

CompiledAction RhsParser::Parser::parse_halt() {
  CompiledAction action;
  action.type = RhsActionType::HALT;
  advance(); // consume 'halt'
  return action;
}

CompiledAction RhsParser::Parser::parse_set_focus() {
  CompiledAction action;
  action.type = RhsActionType::SET_FOCUS;

  advance(); // consume 'setFocus'
  consume(Token::TOK_LPAREN, "Expected '(' after 'setFocus'");
  Token group = consume(Token::TOK_STRING, "Expected string argument for setFocus");
  action.focus_group = group.text;
  consume(Token::TOK_RPAREN, "Expected ')' after group name");

  return action;
}

CompiledAction RhsParser::Parser::parse_if() {
  CompiledAction action;
  action.type = RhsActionType::IF;

  advance(); // consume 'if'

  // Parse condition expression
  std::string condition_expr = parse_expression();
  std::string compile_error;
  action.condition = ExpressionDescriptor::compile(condition_expr, &compile_error);
  if (!action.condition) {
    error("Failed to compile IF condition: " + compile_error);
    return action;
  }
  validate_expression_sources(*action.condition);

  consume(Token::TOK_LBRACE, "Expected '{' after if condition");

  // Parse then actions
  action.then_actions = parse_scoped_actions();
  consume(Token::TOK_RBRACE, "Expected '}' after then block");

  // Optional else / else if block
  if (match(Token::TOK_ELSE)) {
    if (check(Token::TOK_IF)) {
      // else if -> nested if in else_actions
      action.else_actions.push_back(parse_if());
    } else {
      consume(Token::TOK_LBRACE, "Expected '{' after 'else'");
      action.else_actions = parse_scoped_actions();
      consume(Token::TOK_RBRACE, "Expected '}' after else block");
    }
  }

  return action;
}

CompiledAction RhsParser::Parser::parse_for() {
  CompiledAction action;
  action.type = RhsActionType::FOR;

  advance(); // consume 'for'

  Token iter_var = consume(Token::TOK_VARIABLE, "Expected iteration variable after 'for'");
  action.iter_var = iter_var.text;
  if (result_bindings_.contains(action.iter_var)) error("FOR variable shadows a CALL result");

  consume(Token::TOK_IN, "Expected 'in' after iteration variable");

  if (check(Token::TOK_LPAREN)) {
    // Value list form: for $item in ($a, $b, $c) { ... }
    advance(); // consume '('
    Token first = consume(Token::TOK_VARIABLE, "Expected variable in list");
    action.iter_source_list.emplace_back(first.text);
    if (!first.text.empty() && !is_fact_binding_available(action.iter_source_list.back())) {
      error("RHS uses undeclared variable '" + action.iter_source_list.back() + "'");
    }
    while (match(Token::TOK_COMMA)) {
      Token var = consume(Token::TOK_VARIABLE, "Expected variable in list");
      action.iter_source_list.emplace_back(var.text);
      if (!var.text.empty() && !is_fact_binding_available(action.iter_source_list.back())) {
        error("RHS uses undeclared variable '" + action.iter_source_list.back() + "'");
      }
    }
    consume(Token::TOK_RPAREN, "Expected ')' after variable list");
  } else if (check(Token::TOK_VARIABLE)) {
    // Variable/container form: for $item in $var { ... }
    // Or field form: for $item in $var.field { ... }
    Token source_var = advance();
    action.iter_source_var = source_var.text;
    if (!source_var.text.empty() && !is_value_source_available(action.iter_source_var)) {
      error("RHS uses undeclared variable '" + action.iter_source_var + "'");
    }
    if (match(Token::TOK_DOT)) {
      Token field = consume(Token::TOK_IDENTIFIER, "Expected field name after '.'");
      action.iter_source_field = field.text;
    }
  } else {
    error("Expected variable or '(' after 'in'");
    return action;
  }

  consume(Token::TOK_LBRACE, "Expected '{' after for header");

  // Parse body actions
  bool const inserted_local_binding = !action.iter_var.empty()
      && local_bindings_.insert(action.iter_var).second;
  action.body_actions = parse_scoped_actions();
  if (inserted_local_binding) {
    local_bindings_.erase(action.iter_var);
  }
  consume(Token::TOK_RBRACE, "Expected '}' after for body");

  return action;
}

CompiledAction RhsParser::Parser::parse_while() {
  CompiledAction action;
  action.type = RhsActionType::WHILE;

  advance(); // consume 'while'

  // Parse condition expression
  std::string condition_expr = parse_expression();
  std::string compile_error;
  action.condition = ExpressionDescriptor::compile(condition_expr, &compile_error);
  if (!action.condition) {
    error("Failed to compile WHILE condition: " + compile_error);
    return action;
  }
  validate_expression_sources(*action.condition);

  consume(Token::TOK_LBRACE, "Expected '{' after while condition");

  // Parse body actions
  action.body_actions = parse_scoped_actions();
  consume(Token::TOK_RBRACE, "Expected '}' after while body");

  return action;
}

CompiledAction RhsParser::Parser::parse_switch() {
  CompiledAction action;
  action.type = RhsActionType::SWITCH;

  advance(); // consume 'switch'

  // Parse switch expression
  std::string switch_expr = parse_expression();
  std::string compile_error;
  action.switch_expr = ExpressionDescriptor::compile(switch_expr, &compile_error);
  if (!action.switch_expr) {
    error("Failed to compile SWITCH expression: " + compile_error);
    return action;
  }
  validate_expression_sources(*action.switch_expr);

  consume(Token::TOK_LBRACE, "Expected '{' after switch expression");

  // Parse cases
  while (!check(Token::TOK_RBRACE) && !check(Token::TOK_END) && !had_error_) {
    if (check(Token::TOK_CASE)) {
      advance(); // consume 'case'
      SwitchCase sc;
      sc.is_default = false;

      // Parse case value
      std::string case_expr = parse_expression();
      sc.value = ExpressionDescriptor::compile(case_expr, &compile_error);
      if (!sc.value) {
        error("Failed to compile CASE value: " + compile_error);
        return action;
      }
      validate_expression_sources(*sc.value);

      consume(Token::TOK_LBRACE, "Expected '{' after case value");

      // Parse case actions until }
      sc.actions = parse_scoped_actions();
      consume(Token::TOK_RBRACE, "Expected '}' after case block");

      action.switch_cases.push_back(std::move(sc));

    } else if (check(Token::TOK_DEFAULT)) {
      advance(); // consume 'default'
      SwitchCase sc;
      sc.is_default = true;
      sc.value = nullptr;

      consume(Token::TOK_LBRACE, "Expected '{' after default");

      // Parse default actions until }
      sc.actions = parse_scoped_actions();
      consume(Token::TOK_RBRACE, "Expected '}' after default block");

      action.switch_cases.push_back(std::move(sc));

    } else {
      error("Expected 'case' or 'default' in switch block");
      return action;
    }
  }

  consume(Token::TOK_RBRACE, "Expected '}' after switch body");

  return action;
}

CompiledAction RhsParser::Parser::parse_break() {
  CompiledAction action;
  action.type = RhsActionType::BREAK;
  advance(); // consume 'break'
  return action;
}

CompiledAction RhsParser::Parser::parse_continue() {
  CompiledAction action;
  action.type = RhsActionType::CONTINUE;
  advance(); // consume 'continue'
  return action;
}

std::vector<FieldAssignment> RhsParser::Parser::parse_field_assignments() {
  std::vector<FieldAssignment> assignments;

  if (check(Token::TOK_RBRACE)) {
    return assignments; // Empty assignments
  }

  assignments.push_back(parse_field_assignment());

  while (match(Token::TOK_COMMA)) {
    if (check(Token::TOK_RBRACE))
      break; // Trailing comma
    assignments.push_back(parse_field_assignment());
  }

  return assignments;
}

FieldAssignment RhsParser::Parser::parse_value() {
  FieldAssignment assign;

  if (check(Token::TOK_IDENTIFIER) && lexer_.peek().type == Token::TOK_LPAREN
      && !is_rhs_builtin(current_.text)) {
    error("External function calls are not supported in RHS assignments: " + std::string(current_.text));
    return assign;
  }

  // Determine value type based on next token
  if (check(Token::TOK_STRING)) {
    assign.type = RhsValueType::STRING;
    assign.string_literal = advance().text;
    assign.has_precomputed_literal = true;
    assign.precomputed_literal = assign.string_literal;
  } else if (check(Token::TOK_TRUE)) {
    assign.type = RhsValueType::BOOLEAN;
    assign.string_literal = "true";
    assign.has_precomputed_literal = true;
    assign.precomputed_literal = true;
    advance();
  } else if (check(Token::TOK_FALSE)) {
    assign.type = RhsValueType::BOOLEAN;
    assign.string_literal = "false";
    assign.has_precomputed_literal = true;
    assign.precomputed_literal = false;
    advance();
  } else if (check(Token::TOK_IDENTIFIER) && lexer_.peek().type != Token::TOK_DOT &&
             std::all_of(current_.text.begin(), current_.text.end(), [](unsigned char ch) {
               return std::isupper(ch) || std::isdigit(ch) || ch == '_';
             })) {
    assign.type = RhsValueType::STRING;
    assign.string_literal = advance().text;
    assign.has_precomputed_literal = true;
    assign.precomputed_literal = assign.string_literal;
  } else if (check(Token::TOK_INTEGER)) {
    assign.type = RhsValueType::NUMERIC;
    Token num = advance();
    int64_t value = 0;
    auto [end, status] = std::from_chars(num.text.data(), num.text.data() + num.text.size(), value);
    if (status != std::errc{} || end != num.text.data() + num.text.size()) {
      error("Integer literal is invalid or outside int64 range: " + std::string(num.text));
      return assign;
    }
    assign.has_precomputed_literal = true;
    assign.precomputed_literal = value;
  } else if (check(Token::TOK_DOUBLE)) {
    assign.type = RhsValueType::NUMERIC;
    Token num = advance();
    double value = 0;
    auto [end, status] = std::from_chars(num.text.data(), num.text.data() + num.text.size(), value);
    if (status != std::errc{} || end != num.text.data() + num.text.size() || !std::isfinite(value)) {
      error("Floating literal is invalid or outside double range: " + std::string(num.text));
      return assign;
    }
    assign.has_precomputed_literal = true;
    assign.precomputed_literal = value;
  } else {
    // Parse as expression
    std::string expr = parse_expression();

    // Check if it's a simple variable reference
    if (!expr.empty() && expr[0] == '$' && expr.find_first_of("+-*/><!=") == std::string::npos) {
      assign.type = RhsValueType::VAR_REF;
      assign.var_ref = expr;
      validate_value_source_ref(assign.var_ref);
    } else {
      // Compile as numeric expression
      assign.type = RhsValueType::NUMERIC;
      std::string compile_error;
      assign.numeric_expr = ExpressionDescriptor::compile(expr, &compile_error);
      if (!assign.numeric_expr) {
        error("Failed to compile expression '" + expr + "': " + compile_error);
      }
      if (assign.numeric_expr) {
        validate_expression_sources(*assign.numeric_expr);
      }
    }
  }

  return assign;
}

FieldAssignment RhsParser::Parser::parse_field_assignment() {
  FieldAssignment assign;

  Token field = consume(Token::TOK_IDENTIFIER, "Expected field name");
  assign.field_name = field.text;
  assign.field_key = rulesforge::StringInterner::instance().intern_persistent(assign.field_name);

  consume(Token::TOK_ASSIGN, "Expected '=' after field name");

  FieldAssignment value = parse_value();
  assign.type = value.type;
  assign.numeric_expr = std::move(value.numeric_expr);
  assign.string_literal = std::move(value.string_literal);
  assign.var_ref = std::move(value.var_ref);
  assign.has_precomputed_literal = value.has_precomputed_literal;
  assign.precomputed_literal = std::move(value.precomputed_literal);

  return assign;
}

std::string RhsParser::Parser::parse_expression() {
  std::string expr = parse_ternary();
  return expr;
}

std::string RhsParser::Parser::parse_ternary() {
  std::string expr = parse_or();

  // Handle ternary: condition ? true_expr : false_expr
  if (check(Token::TOK_QUESTION)) {
    advance();
    std::string true_expr = parse_ternary();
    consume(Token::TOK_COLON, "Expected ':' in ternary expression");
    std::string false_expr = parse_ternary();
    expr = "(" + expr + ") ? (" + true_expr + ") : (" + false_expr + ")";
  }

  return expr;
}

std::string RhsParser::Parser::parse_or() {
  std::string expr = parse_and();

  while (check(Token::TOK_OR)) {
    Token op = advance();
    expr.append(" ").append(op.text).append(" ");
    expr += parse_and();
  }

  return expr;
}

std::string RhsParser::Parser::parse_and() {
  std::string expr = parse_comparison();

  while (check(Token::TOK_AND)) {
    Token op = advance();
    expr.append(" ").append(op.text).append(" ");
    expr += parse_comparison();
  }

  return expr;
}

std::string RhsParser::Parser::parse_comparison() {
  std::string expr = parse_additive();

  while (check(Token::TOK_GT) || check(Token::TOK_LT) || check(Token::TOK_GE) ||
         check(Token::TOK_LE) || check(Token::TOK_EQ) || check(Token::TOK_NE)) {
    Token op = advance();
    expr.append(" ").append(op.text).append(" ");
    expr += parse_additive();
  }

  return expr;
}

std::string RhsParser::Parser::parse_additive() {
  std::string expr = parse_multiplicative();

  while (check(Token::TOK_PLUS) || check(Token::TOK_MINUS)) {
    Token op = advance();
    expr.append(" ").append(op.text).append(" ");
    expr += parse_multiplicative();
  }

  return expr;
}

std::string RhsParser::Parser::parse_multiplicative() {
  std::string expr = parse_power();

  while (check(Token::TOK_STAR) || check(Token::TOK_SLASH) || check(Token::TOK_MOD)) {
    Token op = advance();
    expr.append(" ").append(op.text).append(" ");
    expr += parse_power();
  }

  return expr;
}

std::string RhsParser::Parser::parse_power() {
  std::string expr = parse_term();

  while (check(Token::TOK_CARET)) {
    Token op = advance();
    expr.append(" ").append(op.text).append(" ");
    expr += parse_term(); // Right associative
  }

  return expr;
}

std::string RhsParser::Parser::parse_term() {
  std::string term;

  if (check(Token::TOK_LPAREN)) {
    advance();
    term = "(" + parse_expression() + ")";
    consume(Token::TOK_RPAREN, "Expected ')' after expression");
  } else if (check(Token::TOK_VARIABLE)) {
    term = advance().text;
    while (check(Token::TOK_DOT)) {
      advance();
      Token field = consume(Token::TOK_IDENTIFIER, "Expected field name after '.'");
      term.append(".").append(field.text);
    }
  } else if (check(Token::TOK_INTEGER) || check(Token::TOK_DOUBLE)) {
    term = advance().text;
  } else if (check(Token::TOK_IDENTIFIER)) {
    std::string name(advance().text);
    if (check(Token::TOK_LPAREN)) {
      std::vector<std::string> args;
      advance();
      if (!check(Token::TOK_RPAREN)) {
        args.push_back(parse_expression());
        while (match(Token::TOK_COMMA)) {
          args.push_back(parse_expression());
        }
      }
      consume(Token::TOK_RPAREN, "Expected ')' after function arguments");

      term = name + "(";
      for (size_t i = 0; i < args.size(); ++i) {
        if (i != 0) term += ", ";
        term += args[i];
      }
      term += ")";
    } else {
      term = name;
      while (check(Token::TOK_DOT)) {
        advance();
        Token field = consume(Token::TOK_IDENTIFIER, "Expected field name after '.'");
        term.append(".").append(field.text);
      }
    }
  } else if (check(Token::TOK_STRING)) {
    term = '"';
    for (char value : advance().text) {
      switch (value) {
        case '"': term += "\\\""; break;
        case '\\': term += "\\\\"; break;
        case '\n': term += "\\n"; break;
        case '\r': term += "\\r"; break;
        case '\t': term += "\\t"; break;
        default: term += value; break;
      }
    }
    term += '"';
  } else {
    error("Expected expression term");
    return "";
  }

  return term;
}

// ============================================================================
// RhsParser Static Methods
// ============================================================================

std::vector<CompiledAction> RhsParser::parse(
    std::string const &rhs_code,
    std::map<std::string, int> const &bindings,
    std::unordered_set<std::string> const &globals,
    std::string *error_out) {

  Parser parser(rhs_code, bindings, globals);
  auto actions = parser.parse();

  if (actions.empty() && error_out) {
    *error_out = parser.error();
  }

  return actions;
}

std::vector<CompiledAction> RhsParser::parse(std::string const &rhs_code,
                                             std::map<std::string, int> const &bindings,
                                             std::string *error_out) {
  static std::unordered_set<std::string> const kNoGlobals;
  return parse(rhs_code, bindings, kNoGlobals, error_out);
}

} // namespace rulesforge
