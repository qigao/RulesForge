#include "rhs_parser.hpp"
#include "core/logging_control.hpp"
#include "expression_descriptor.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <unordered_set>
#include <string_view>
#include "rhs_builtins.hpp"

namespace rulesforge {

// ============================================================================
// Lexer Implementation
// ============================================================================

RhsParser::Lexer::Lexer(std::string const &input) : input_(input) {}

void RhsParser::Lexer::skip_whitespace() {
  while (pos_ < input_.size()) {
    char c = input_[pos_];
    if (c == ' ' || c == '\t' || c == '\r') {
      pos_++;
      column_++;
    } else if (c == '\n') {
      pos_++;
      line_++;
      column_ = 1;
    } else if (c == '/' && pos_ + 1 < input_.size() && input_[pos_ + 1] == '/') {
      // Line comment
      while (pos_ < input_.size() && input_[pos_] != '\n')
        pos_++;
    } else {
      break;
    }
  }
}

RhsParser::Token RhsParser::Lexer::scan_identifier() {
  size_t start = pos_;
  size_t start_col = column_;
  while (pos_ < input_.size() && (std::isalnum(input_[pos_]) || input_[pos_] == '_')) {
    pos_++;
    column_++;
  }
  std::string text = input_.substr(start, pos_ - start);

  // Check for keywords
  Token::Type type = Token::TOK_IDENTIFIER;
  if (text == "update")
    type = Token::TOK_UPDATE;
  else if (text == "insert")
    type = Token::TOK_INSERT;
  else if (text == "insertLogical")
    type = Token::TOK_INSERT_LOGICAL;
  else if (text == "retract")
    type = Token::TOK_RETRACT;
  else if (text == "halt")
    type = Token::TOK_HALT;
  else if (text == "setFocus")
    type = Token::TOK_SET_FOCUS;
  else if (text == "invoke")
    type = Token::TOK_INVOKE;
  else if (text == "if")
    type = Token::TOK_IF;
  else if (text == "else")
    type = Token::TOK_ELSE;
  else if (text == "for")
    type = Token::TOK_FOR;
  else if (text == "in")
    type = Token::TOK_IN;
  else if (text == "while")
    type = Token::TOK_WHILE;
  else if (text == "switch")
    type = Token::TOK_SWITCH;
  else if (text == "case")
    type = Token::TOK_CASE;
  else if (text == "default")
    type = Token::TOK_DEFAULT;
  else if (text == "break")
    type = Token::TOK_BREAK;
  else if (text == "continue")
    type = Token::TOK_CONTINUE;
  else if (text == "true")
    type = Token::TOK_TRUE;
  else if (text == "false")
    type = Token::TOK_FALSE;

  return {type, text, line_, start_col};
}

RhsParser::Token RhsParser::Lexer::scan_number() {
  size_t start = pos_;
  size_t start_col = column_;
  bool is_double = false;

  if (input_[pos_] == '-') {
    pos_++;
    column_++;
  }
  while (pos_ < input_.size() && std::isdigit(input_[pos_])) {
    pos_++;
    column_++;
  }
  if (pos_ < input_.size() && input_[pos_] == '.') {
    is_double = true;
    pos_++;
    column_++;
    while (pos_ < input_.size() && std::isdigit(input_[pos_])) {
      pos_++;
      column_++;
    }
  }

  std::string text = input_.substr(start, pos_ - start);
  return {is_double ? Token::TOK_DOUBLE : Token::TOK_INTEGER, text, line_, start_col};
}

RhsParser::Token RhsParser::Lexer::scan_string() {
  size_t start_col = column_;
  char quote = input_[pos_];
  pos_++;
  column_++;

  std::string text;
  while (pos_ < input_.size() && input_[pos_] != quote) {
    if (input_[pos_] == '\\' && pos_ + 1 < input_.size()) {
      pos_++;
      column_++;
      char escaped = input_[pos_];
      switch (escaped) {
      case 'n':
        text += '\n';
        break;
      case 't':
        text += '\t';
        break;
      case 'r':
        text += '\r';
        break;
      case '\\':
        text += '\\';
        break;
      case '"':
        text += '"';
        break;
      case '\'':
        text += '\'';
        break;
      default:
        text += escaped;
        break;
      }
    } else {
      text += input_[pos_];
    }
    pos_++;
    column_++;
  }

  if (pos_ < input_.size()) {
    pos_++; // Skip closing quote
    column_++;
  }

  return {Token::TOK_STRING, text, line_, start_col};
}

RhsParser::Token RhsParser::Lexer::next() {
  if (has_peeked_) {
    has_peeked_ = false;
    return peeked_;
  }

  skip_whitespace();

  if (pos_ >= input_.size()) {
    return {Token::TOK_END, "", line_, column_};
  }

  char c = input_[pos_];
  size_t start_col = column_;

  // Variable ($identifier)
  if (c == '$') {
    size_t start = pos_;
    pos_++;
    column_++;
    while (pos_ < input_.size() && (std::isalnum(input_[pos_]) || input_[pos_] == '_')) {
      pos_++;
      column_++;
    }
    return {Token::TOK_VARIABLE, input_.substr(start, pos_ - start), line_, start_col};
  }

  // Identifier or keyword
  if (std::isalpha(c) || c == '_') {
    return scan_identifier();
  }

  // Number
  if (std::isdigit(c) || (c == '-' && pos_ + 1 < input_.size() && std::isdigit(input_[pos_ + 1]))) {
    return scan_number();
  }

  // String
  if (c == '"' || c == '\'') {
    return scan_string();
  }

  // Two-character operators
  if (pos_ + 1 < input_.size()) {
    std::string two = input_.substr(pos_, 2);
    if (two == "==") {
      pos_ += 2;
      column_ += 2;
      return {Token::TOK_EQ, two, line_, start_col};
    }
    if (two == "!=") {
      pos_ += 2;
      column_ += 2;
      return {Token::TOK_NE, two, line_, start_col};
    }
    if (two == ">=") {
      pos_ += 2;
      column_ += 2;
      return {Token::TOK_GE, two, line_, start_col};
    }
    if (two == "<=") {
      pos_ += 2;
      column_ += 2;
      return {Token::TOK_LE, two, line_, start_col};
    }
    if (two == "&&") {
      pos_ += 2;
      column_ += 2;
      return {Token::TOK_AND, two, line_, start_col};
    }
    if (two == "||") {
      pos_ += 2;
      column_ += 2;
      return {Token::TOK_OR, two, line_, start_col};
    }
  }

  // Single-character tokens
  pos_++;
  column_++;
  switch (c) {
  case '{':
    return {Token::TOK_LBRACE, "{", line_, start_col};
  case '}':
    return {Token::TOK_RBRACE, "}", line_, start_col};
  case '(':
    return {Token::TOK_LPAREN, "(", line_, start_col};
  case ')':
    return {Token::TOK_RPAREN, ")", line_, start_col};
  case ',':
    return {Token::TOK_COMMA, ",", line_, start_col};
  case '.':
    return {Token::TOK_DOT, ".", line_, start_col};
  case '=':
    return {Token::TOK_ASSIGN, "=", line_, start_col};
  case '+':
    return {Token::TOK_PLUS, "+", line_, start_col};
  case '-':
    return {Token::TOK_MINUS, "-", line_, start_col};
  case '*':
    return {Token::TOK_STAR, "*", line_, start_col};
  case '/':
    return {Token::TOK_SLASH, "/", line_, start_col};
  case '%':
    return {Token::TOK_MOD, "%", line_, start_col};
  case '^':
    return {Token::TOK_CARET, "^", line_, start_col};
  case '?':
    return {Token::TOK_QUESTION, "?", line_, start_col};
  case ':':
    return {Token::TOK_COLON, ":", line_, start_col};
  case '>':
    return {Token::TOK_GT, ">", line_, start_col};
  case '<':
    return {Token::TOK_LT, "<", line_, start_col};
  default:
    return {Token::TOK_ERROR, std::string(1, c), line_, start_col};
  }
}

RhsParser::Token RhsParser::Lexer::peek() {
  if (!has_peeked_) {
    peeked_ = next();
    has_peeked_ = true;
  }
  return peeked_;
}

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
             std::to_string(current_.column) + ": " + message;
    had_error_ = true;
  }
}

bool RhsParser::Parser::is_fact_binding_available(std::string const& name) const {
  return bindings_.find(name) != bindings_.end()
      || local_bindings_.find(name) != local_bindings_.end();
}

bool RhsParser::Parser::is_value_source_available(std::string const& name) const {
  return is_fact_binding_available(name)
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
  case Token::TOK_INVOKE:
    return parse_invoke();
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
    error("Expected action keyword (update, insert, retract, halt, setFocus, invoke, if, for, "
          "while, switch, break, continue)");
    return {};
  }
}

CompiledAction RhsParser::Parser::parse_update() {
  CompiledAction action;
  action.type = RhsActionType::UPDATE;

  advance(); // consume 'update'
  Token var = consume(Token::TOK_VARIABLE, "Expected variable after 'update'");
  action.target_var = var.text;
  if (!var.text.empty() && !is_fact_binding_available(var.text)) {
    error("RHS uses undeclared variable '" + var.text + "'");
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
  if (!var.text.empty() && !is_fact_binding_available(var.text)) {
    error("RHS uses undeclared variable '" + var.text + "'");
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

CompiledAction RhsParser::Parser::parse_invoke() {
  CompiledAction action;
  action.type = RhsActionType::INVOKE;

  advance(); // consume 'invoke'
  Token fn = consume(Token::TOK_IDENTIFIER, "Expected function name after 'invoke'");
  action.invoke_function = fn.text;

  consume(Token::TOK_LPAREN, "Expected '(' after function name");
  if (!check(Token::TOK_RPAREN)) {
    action.invoke_args.push_back(parse_value());
    while (match(Token::TOK_COMMA)) {
      if (check(Token::TOK_RPAREN))
        break;
      action.invoke_args.push_back(parse_value());
    }
  }
  consume(Token::TOK_RPAREN, "Expected ')' after invoke arguments");

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
  while (!check(Token::TOK_RBRACE) && !check(Token::TOK_END) && !had_error_) {
    action.then_actions.push_back(parse_action());
  }
  consume(Token::TOK_RBRACE, "Expected '}' after then block");

  // Optional else / else if block
  if (match(Token::TOK_ELSE)) {
    if (check(Token::TOK_IF)) {
      // else if -> nested if in else_actions
      action.else_actions.push_back(parse_if());
    } else {
      consume(Token::TOK_LBRACE, "Expected '{' after 'else'");
      while (!check(Token::TOK_RBRACE) && !check(Token::TOK_END) && !had_error_) {
        action.else_actions.push_back(parse_action());
      }
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

  consume(Token::TOK_IN, "Expected 'in' after iteration variable");

  if (check(Token::TOK_LPAREN)) {
    // Value list form: for $item in ($a, $b, $c) { ... }
    advance(); // consume '('
    Token first = consume(Token::TOK_VARIABLE, "Expected variable in list");
    action.iter_source_list.push_back(first.text);
    if (!first.text.empty() && !is_fact_binding_available(first.text)) {
      error("RHS uses undeclared variable '" + first.text + "'");
    }
    while (match(Token::TOK_COMMA)) {
      Token var = consume(Token::TOK_VARIABLE, "Expected variable in list");
      action.iter_source_list.push_back(var.text);
      if (!var.text.empty() && !is_fact_binding_available(var.text)) {
        error("RHS uses undeclared variable '" + var.text + "'");
      }
    }
    consume(Token::TOK_RPAREN, "Expected ')' after variable list");
  } else if (check(Token::TOK_VARIABLE)) {
    // Variable/container form: for $item in $var { ... }
    // Or field form: for $item in $var.field { ... }
    Token source_var = advance();
    action.iter_source_var = source_var.text;
    if (!source_var.text.empty() && !is_value_source_available(source_var.text)) {
      error("RHS uses undeclared variable '" + source_var.text + "'");
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
  while (!check(Token::TOK_RBRACE) && !check(Token::TOK_END) && !had_error_) {
    action.body_actions.push_back(parse_action());
  }
  if (inserted_local_binding) {
    local_bindings_.erase(action.iter_var);
  }
  consume(Token::TOK_RBRACE, "Expected '}' after for body");

  return action;
}

CompiledAction RhsParser::Parser::parse_while() {
  CompiledAction action;
  action.type = RhsActionType::WHILE;
  action.max_iterations = 1000; // Safety limit

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
  while (!check(Token::TOK_RBRACE) && !check(Token::TOK_END) && !had_error_) {
    action.body_actions.push_back(parse_action());
  }
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
      while (!check(Token::TOK_RBRACE) && !check(Token::TOK_END) && !had_error_) {
        sc.actions.push_back(parse_action());
      }
      consume(Token::TOK_RBRACE, "Expected '}' after case block");

      action.switch_cases.push_back(std::move(sc));

    } else if (check(Token::TOK_DEFAULT)) {
      advance(); // consume 'default'
      SwitchCase sc;
      sc.is_default = true;
      sc.value = nullptr;

      consume(Token::TOK_LBRACE, "Expected '{' after default");

      // Parse default actions until }
      while (!check(Token::TOK_RBRACE) && !check(Token::TOK_END) && !had_error_) {
        sc.actions.push_back(parse_action());
      }
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

  // Check if the identifier is a standard math/string/date function
  bool is_builtin_func = false;
  if (check(Token::TOK_IDENTIFIER) && lexer_.peek().type == Token::TOK_LPAREN) {
    if (is_exprtk_builtin(current_.text)) {
      is_builtin_func = true;
    }
  }

  // Determine value type based on next token
  if (check(Token::TOK_IDENTIFIER) && lexer_.peek().type == Token::TOK_LPAREN && !is_builtin_func) {
    assign = parse_native_call_value();
  } else if (check(Token::TOK_STRING)) {
    assign.type = RhsValueType::STRING;
    assign.string_literal = advance().text;
    assign.has_precomputed_literal = true;
    assign.precomputed_literal = assign.string_literal;
  } else if (check(Token::TOK_TRUE)) {
    assign.type = RhsValueType::BOOLEAN;
    assign.string_literal = "true";
    assign.has_precomputed_literal = true;
    assign.precomputed_literal = int64_t(1);
    advance();
  } else if (check(Token::TOK_FALSE)) {
    assign.type = RhsValueType::BOOLEAN;
    assign.string_literal = "false";
    assign.has_precomputed_literal = true;
    assign.precomputed_literal = int64_t(0);
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
    try {
      assign.has_precomputed_literal = true;
      assign.precomputed_literal = static_cast<int64_t>(std::stoll(num.text));
    } catch (...) {
      assign.has_precomputed_literal = false;
    }
    if (!assign.has_precomputed_literal) {
      std::string compile_error;
      assign.numeric_expr = ExpressionDescriptor::compile(num.text, &compile_error);
      if (!assign.numeric_expr) {
        error("Failed to compile expression '" + num.text + "': " + compile_error);
      }
      if (assign.numeric_expr) {
        validate_expression_sources(*assign.numeric_expr);
      }
    }
  } else if (check(Token::TOK_DOUBLE)) {
    assign.type = RhsValueType::NUMERIC;
    Token num = advance();
    try {
      assign.has_precomputed_literal = true;
      assign.precomputed_literal = std::stod(num.text);
    } catch (...) {
      assign.has_precomputed_literal = false;
    }
    if (!assign.has_precomputed_literal) {
      std::string compile_error;
      assign.numeric_expr = ExpressionDescriptor::compile(num.text, &compile_error);
      if (!assign.numeric_expr) {
        error("Failed to compile expression '" + num.text + "': " + compile_error);
      }
      if (assign.numeric_expr) {
        validate_expression_sources(*assign.numeric_expr);
      }
    }
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

FieldAssignment RhsParser::Parser::parse_native_call_value() {
  FieldAssignment assign;
  assign.type = RhsValueType::NATIVE_CALL;

  Token name = consume(Token::TOK_IDENTIFIER, "Expected native function name");
  assign.native_call_name = name.text;

  consume(Token::TOK_LPAREN, "Expected '(' after native function name");
  if (!check(Token::TOK_RPAREN)) {
    assign.native_call_args.push_back(parse_value());
    while (match(Token::TOK_COMMA)) {
      if (check(Token::TOK_RPAREN))
        break;
      assign.native_call_args.push_back(parse_value());
    }
  }
  consume(Token::TOK_RPAREN, "Expected ')' after native function arguments");

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
  assign.native_call_name = std::move(value.native_call_name);
  assign.native_call_args = std::move(value.native_call_args);
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
    expr += " " + op.text + " ";
    expr += parse_and();
  }

  return expr;
}

std::string RhsParser::Parser::parse_and() {
  std::string expr = parse_comparison();

  while (check(Token::TOK_AND)) {
    Token op = advance();
    expr += " " + op.text + " ";
    expr += parse_comparison();
  }

  return expr;
}

std::string RhsParser::Parser::parse_comparison() {
  std::string expr = parse_additive();

  while (check(Token::TOK_GT) || check(Token::TOK_LT) || check(Token::TOK_GE) ||
         check(Token::TOK_LE) || check(Token::TOK_EQ) || check(Token::TOK_NE)) {
    Token op = advance();
    expr += " " + op.text + " ";
    expr += parse_additive();
  }

  return expr;
}

std::string RhsParser::Parser::parse_additive() {
  std::string expr = parse_multiplicative();

  while (check(Token::TOK_PLUS) || check(Token::TOK_MINUS)) {
    Token op = advance();
    expr += " " + op.text + " ";
    expr += parse_multiplicative();
  }

  return expr;
}

std::string RhsParser::Parser::parse_multiplicative() {
  std::string expr = parse_power();

  while (check(Token::TOK_STAR) || check(Token::TOK_SLASH) || check(Token::TOK_MOD)) {
    Token op = advance();
    expr += " " + op.text + " ";
    expr += parse_power();
  }

  return expr;
}

std::string RhsParser::Parser::parse_power() {
  std::string expr = parse_term();

  while (check(Token::TOK_CARET)) {
    Token op = advance();
    expr += " " + op.text + " ";
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
      term += "." + field.text;
    }
  } else if (check(Token::TOK_INTEGER) || check(Token::TOK_DOUBLE)) {
    term = advance().text;
  } else if (check(Token::TOK_IDENTIFIER)) {
    std::string name = advance().text;
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

      if (name == "to_upper") {
        name = "upper";
      } else if (name == "to_lower") {
        name = "lower";
      }

      if (name == "concat") {
        if (args.empty()) {
          error("concat requires at least one argument");
          return "";
        }
        term = "(" + args[0];
        for (size_t i = 1; i < args.size(); ++i) {
          term += " + " + args[i];
        }
        term += ")";
      } else {
        term = name + "(";
        for (size_t i = 0; i < args.size(); ++i) {
          if (i != 0) {
            term += ", ";
          }
          term += args[i];
        }
        term += ")";
      }
    } else {
      term = name;
      while (check(Token::TOK_DOT)) {
        advance();
        Token field = consume(Token::TOK_IDENTIFIER, "Expected field name after '.'");
        term += "." + field.text;
      }
    }
  } else if (check(Token::TOK_STRING)) {
    term = "\"" + advance().text + "\"";
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

