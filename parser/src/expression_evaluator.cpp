#include "expression_evaluator.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rulesforge {
namespace {

struct EvalContext {
  std::vector<double> const *vars = nullptr;
};

struct Node {
  virtual ~Node() = default;
  virtual double eval(EvalContext const &ctx) const = 0;
};

struct NumberNode final : Node {
  explicit NumberNode(double v) : v(v) {}
  double eval(EvalContext const &) const override { return v; }
  double v;
};

struct VarNode final : Node {
  explicit VarNode(size_t idx) : idx(idx) {}
  double eval(EvalContext const &ctx) const override {
    if (!ctx.vars || idx >= ctx.vars->size())
      return 0.0;
    return (*ctx.vars)[idx];
  }
  size_t idx;
};

struct UnaryNode final : Node {
  enum class Op { Plus, Minus, Not };
  UnaryNode(Op op, std::unique_ptr<Node> child) : op(op), child(std::move(child)) {}
  double eval(EvalContext const &ctx) const override {
    double v = child->eval(ctx);
    switch (op) {
    case Op::Plus:
      return v;
    case Op::Minus:
      return -v;
    case Op::Not:
      return (v == 0.0) ? 1.0 : 0.0;
    }
    return 0.0;
  }
  Op op;
  std::unique_ptr<Node> child;
};

struct BinaryNode final : Node {
  enum class Op { Add, Sub, Mul, Div, Mod, Pow, Lt, Le, Gt, Ge, Eq, Ne, And, Or, Xor };
  BinaryNode(Op op, std::unique_ptr<Node> l, std::unique_ptr<Node> r)
      : op(op), lhs(std::move(l)), rhs(std::move(r)) {}
  double eval(EvalContext const &ctx) const override {
    double a = lhs->eval(ctx);
    double b = rhs->eval(ctx);
    switch (op) {
    case Op::Add:
      return a + b;
    case Op::Sub:
      return a - b;
    case Op::Mul:
      return a * b;
    case Op::Div:
      return b == 0.0 ? 0.0 : (a / b);
    case Op::Mod:
      return std::fmod(a, b);
    case Op::Pow:
      return std::pow(a, b);
    case Op::Lt:
      return a < b ? 1.0 : 0.0;
    case Op::Le:
      return a <= b ? 1.0 : 0.0;
    case Op::Gt:
      return a > b ? 1.0 : 0.0;
    case Op::Ge:
      return a >= b ? 1.0 : 0.0;
    case Op::Eq:
      return a == b ? 1.0 : 0.0;
    case Op::Ne:
      return a != b ? 1.0 : 0.0;
    case Op::And:
      return (a != 0.0 && b != 0.0) ? 1.0 : 0.0;
    case Op::Or:
      return (a != 0.0 || b != 0.0) ? 1.0 : 0.0;
    case Op::Xor:
      return ((a != 0.0) != (b != 0.0)) ? 1.0 : 0.0;
    }
    return 0.0;
  }
  Op op;
  std::unique_ptr<Node> lhs;
  std::unique_ptr<Node> rhs;
};

struct TernaryNode final : Node {
  TernaryNode(std::unique_ptr<Node> c, std::unique_ptr<Node> t, std::unique_ptr<Node> f)
      : cond(std::move(c)), tval(std::move(t)), fval(std::move(f)) {}
  double eval(EvalContext const &ctx) const override {
    return cond->eval(ctx) != 0.0 ? tval->eval(ctx) : fval->eval(ctx);
  }
  std::unique_ptr<Node> cond;
  std::unique_ptr<Node> tval;
  std::unique_ptr<Node> fval;
};

struct FuncNode final : Node {
  explicit FuncNode(std::string name, std::vector<std::unique_ptr<Node>> args)
      : name(std::move(name)), args(std::move(args)) {}

  static double sign(double x) { return (x > 0) - (x < 0); }
  static double frac(double x) { return x - std::trunc(x); }

  double eval(EvalContext const &ctx) const override {
    std::vector<double> a;
    a.reserve(args.size());
    for (auto const &n : args)
      a.push_back(n->eval(ctx));

    if (name == "abs" && a.size() == 1)
      return std::abs(a[0]);
    if (name == "ceil" && a.size() == 1)
      return std::ceil(a[0]);
    if (name == "floor" && a.size() == 1)
      return std::floor(a[0]);
    if (name == "round" && a.size() == 1)
      return std::round(a[0]);
    if (name == "trunc" && a.size() == 1)
      return std::trunc(a[0]);
    if (name == "sgn" && a.size() == 1)
      return sign(a[0]);
    if (name == "frac" && a.size() == 1)
      return frac(a[0]);
    if (name == "sqrt" && a.size() == 1)
      return std::sqrt(a[0]);
    if (name == "pow" && a.size() == 2)
      return std::pow(a[0], a[1]);
    if (name == "root" && a.size() == 2)
      return std::pow(a[0], 1.0 / a[1]);
    if (name == "exp" && a.size() == 1)
      return std::exp(a[0]);
    if (name == "log" && a.size() == 1)
      return std::log(a[0]);
    if (name == "log2" && a.size() == 1)
      return std::log2(a[0]);
    if (name == "log10" && a.size() == 1)
      return std::log10(a[0]);
    if (name == "sin" && a.size() == 1)
      return std::sin(a[0]);
    if (name == "cos" && a.size() == 1)
      return std::cos(a[0]);
    if (name == "tan" && a.size() == 1)
      return std::tan(a[0]);
    if (name == "asin" && a.size() == 1)
      return std::asin(a[0]);
    if (name == "acos" && a.size() == 1)
      return std::acos(a[0]);
    if (name == "atan" && a.size() == 1)
      return std::atan(a[0]);
    if (name == "atan2" && a.size() == 2)
      return std::atan2(a[0], a[1]);
    if (name == "sinh" && a.size() == 1)
      return std::sinh(a[0]);
    if (name == "cosh" && a.size() == 1)
      return std::cosh(a[0]);
    if (name == "tanh" && a.size() == 1)
      return std::tanh(a[0]);
    if (name == "asinh" && a.size() == 1)
      return std::asinh(a[0]);
    if (name == "acosh" && a.size() == 1)
      return std::acosh(a[0]);
    if (name == "atanh" && a.size() == 1)
      return std::atanh(a[0]);
    if (name == "min" && a.size() == 2)
      return std::min(a[0], a[1]);
    if (name == "max" && a.size() == 2)
      return std::max(a[0], a[1]);
    if (name == "clamp" && a.size() == 3)
      return std::max(a[0], std::min(a[1], a[2]));
    if (name == "inrange" && a.size() == 3)
      return (a[0] >= a[1] && a[0] <= a[2]) ? 1.0 : 0.0;
    if (name == "if" && a.size() == 3)
      return a[0] != 0.0 ? a[1] : a[2];
    if (name == "avg" && !a.empty()) {
      double s = 0.0;
      for (double x : a)
        s += x;
      return s / static_cast<double>(a.size());
    }
    if (name == "sum" && !a.empty()) {
      double s = 0.0;
      for (double x : a)
        s += x;
      return s;
    }
    if (name == "mul" && !a.empty()) {
      double p = 1.0;
      for (double x : a)
        p *= x;
      return p;
    }
    if (name == "erf" && a.size() == 1)
      return std::erf(a[0]);
    if (name == "erfc" && a.size() == 1)
      return std::erfc(a[0]);
    if (name == "ncdf" && a.size() == 1)
      return 0.5 * (1.0 + std::erf(a[0] / std::sqrt(2.0)));
    if (name == "hypot" && a.size() == 2)
      return std::hypot(a[0], a[1]);
    if (name == "mod" && a.size() == 2)
      return std::fmod(a[0], a[1]);
    if (name == "fmod" && a.size() == 2)
      return std::fmod(a[0], a[1]);
    if (name == "expm1" && a.size() == 1)
      return std::expm1(a[0]);
    if (name == "log1p" && a.size() == 1)
      return std::log1p(a[0]);
    if (name == "logn" && a.size() == 2)
      return std::log(a[0]) / std::log(a[1]);

    return 0.0;
  }

  std::string name;
  std::vector<std::unique_ptr<Node>> args;
};

enum class TokType {
  End,
  Number,
  Variable,
  Ident,
  LParen,
  RParen,
  Comma,
  Question,
  Colon,
  Plus,
  Minus,
  Star,
  Slash,
  Percent,
  Caret,
  Bang,
  AndAnd,
  OrOr,
  EqEq,
  NotEq,
  Lt,
  Le,
  Gt,
  Ge
};

struct Token {
  TokType type = TokType::End;
  std::string text;
  double number = 0.0;
};

class Lexer {
public:
  explicit Lexer(std::string_view s) : s_(s) {}

  Token next() {
    skip_ws();
    if (i_ >= s_.size())
      return {TokType::End, "", 0.0};

    char c = s_[i_];
    if (std::isdigit(static_cast<unsigned char>(c)) || c == '.')
      return lex_number();
    if (c == '$')
      return lex_variable();
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
      return lex_ident();

    if (match("&&"))
      return {TokType::AndAnd, "&&", 0.0};
    if (match("||"))
      return {TokType::OrOr, "||", 0.0};
    if (match("=="))
      return {TokType::EqEq, "==", 0.0};
    if (match("!="))
      return {TokType::NotEq, "!=", 0.0};
    if (match("<="))
      return {TokType::Le, "<=", 0.0};
    if (match(">="))
      return {TokType::Ge, ">=", 0.0};

    ++i_;
    switch (c) {
    case '(':
      return {TokType::LParen, "(", 0.0};
    case ')':
      return {TokType::RParen, ")", 0.0};
    case ',':
      return {TokType::Comma, ",", 0.0};
    case '?':
      return {TokType::Question, "?", 0.0};
    case ':':
      return {TokType::Colon, ":", 0.0};
    case '+':
      return {TokType::Plus, "+", 0.0};
    case '-':
      return {TokType::Minus, "-", 0.0};
    case '*':
      return {TokType::Star, "*", 0.0};
    case '/':
      return {TokType::Slash, "/", 0.0};
    case '%':
      return {TokType::Percent, "%", 0.0};
    case '^':
      return {TokType::Caret, "^", 0.0};
    case '!':
      return {TokType::Bang, "!", 0.0};
    case '<':
      return {TokType::Lt, "<", 0.0};
    case '>':
      return {TokType::Gt, ">", 0.0};
    default:
      throw std::runtime_error(std::string("Unexpected character: ") + c);
    }
  }

private:
  Token lex_number() {
    size_t start = i_;
    bool dot = false;
    while (i_ < s_.size()) {
      char c = s_[i_];
      if (std::isdigit(static_cast<unsigned char>(c))) {
        ++i_;
      } else if (c == '.' && !dot) {
        dot = true;
        ++i_;
      } else {
        break;
      }
    }
    std::string t(s_.substr(start, i_ - start));
    return {TokType::Number, t, std::stod(t)};
  }

  Token lex_variable() {
    // Accept both "$a.b" and "$a . b" forms.
    std::string out;
    out.push_back('$');
    ++i_; // consume '$'

    auto is_ident_start = [](char c) {
      return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
    };
    auto is_ident_char = [](char c) {
      return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    };

    if (i_ >= s_.size() || !is_ident_start(s_[i_])) {
      throw std::runtime_error("Invalid variable: missing identifier after '$'");
    }

    while (i_ < s_.size() && is_ident_char(s_[i_])) {
      out.push_back(s_[i_++]);
    }

    while (i_ < s_.size()) {
      size_t j = i_;
      while (j < s_.size() && std::isspace(static_cast<unsigned char>(s_[j])) != 0)
        ++j;
      if (j >= s_.size() || s_[j] != '.')
        break;
      ++j; // consume '.'
      while (j < s_.size() && std::isspace(static_cast<unsigned char>(s_[j])) != 0)
        ++j;

      if (j >= s_.size() || !is_ident_start(s_[j])) {
        throw std::runtime_error("Invalid variable path: expected identifier after '.'");
      }

      out.push_back('.');
      while (j < s_.size() && is_ident_char(s_[j])) {
        out.push_back(s_[j++]);
      }
      i_ = j;
    }

    return {TokType::Variable, out, 0.0};
  }

  Token lex_ident() {
    size_t start = i_++;
    while (i_ < s_.size()) {
      char c = s_[i_];
      if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
        ++i_;
      } else {
        break;
      }
    }
    return {TokType::Ident, std::string(s_.substr(start, i_ - start)), 0.0};
  }

  void skip_ws() {
    while (i_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[i_])) != 0)
      ++i_;
  }

  bool match(char const *t) {
    size_t n = std::char_traits<char>::length(t);
    if (i_ + n > s_.size())
      return false;
    if (s_.substr(i_, n) == t) {
      i_ += n;
      return true;
    }
    return false;
  }

  std::string_view s_;
  size_t i_ = 0;
};

class Parser {
public:
  Parser(std::string const &expr, std::vector<std::string> &vars,
         std::unordered_map<std::string, size_t> &idx)
      : lx_(expr), vars_(vars), var_to_idx_(idx) {
    tok_ = lx_.next();
  }

  std::unique_ptr<Node> parse() {
    auto n = parse_ternary();
    if (tok_.type != TokType::End)
      throw std::runtime_error("Unexpected trailing token");
    return n;
  }

private:
  std::unique_ptr<Node> parse_ternary() {
    auto cond = parse_or();
    if (tok_.type == TokType::Question) {
      next();
      auto t = parse_ternary();
      expect(TokType::Colon, "Expected ':' in ternary");
      auto f = parse_ternary();
      return std::make_unique<TernaryNode>(std::move(cond), std::move(t), std::move(f));
    }
    return cond;
  }

  std::unique_ptr<Node> parse_or() {
    auto n = parse_and();
    while (tok_.type == TokType::OrOr || (tok_.type == TokType::Ident && tok_.text == "or")) {
      next();
      n = std::make_unique<BinaryNode>(BinaryNode::Op::Or, std::move(n), parse_and());
    }
    return n;
  }

  std::unique_ptr<Node> parse_and() {
    auto n = parse_xor();
    while (tok_.type == TokType::AndAnd || (tok_.type == TokType::Ident && tok_.text == "and")) {
      next();
      n = std::make_unique<BinaryNode>(BinaryNode::Op::And, std::move(n), parse_xor());
    }
    return n;
  }

  std::unique_ptr<Node> parse_xor() {
    auto n = parse_cmp();
    while (tok_.type == TokType::Ident && tok_.text == "xor") {
      next();
      n = std::make_unique<BinaryNode>(BinaryNode::Op::Xor, std::move(n), parse_cmp());
    }
    return n;
  }

  std::unique_ptr<Node> parse_cmp() {
    auto n = parse_add();
    while (true) {
      BinaryNode::Op op;
      switch (tok_.type) {
      case TokType::Lt:
        op = BinaryNode::Op::Lt;
        break;
      case TokType::Le:
        op = BinaryNode::Op::Le;
        break;
      case TokType::Gt:
        op = BinaryNode::Op::Gt;
        break;
      case TokType::Ge:
        op = BinaryNode::Op::Ge;
        break;
      case TokType::EqEq:
        op = BinaryNode::Op::Eq;
        break;
      case TokType::NotEq:
        op = BinaryNode::Op::Ne;
        break;
      default:
        return n;
      }
      next();
      n = std::make_unique<BinaryNode>(op, std::move(n), parse_add());
    }
  }

  std::unique_ptr<Node> parse_add() {
    auto n = parse_mul();
    while (tok_.type == TokType::Plus || tok_.type == TokType::Minus) {
      TokType t = tok_.type;
      next();
      n = std::make_unique<BinaryNode>(t == TokType::Plus ? BinaryNode::Op::Add
                                                          : BinaryNode::Op::Sub,
                                       std::move(n), parse_mul());
    }
    return n;
  }

  std::unique_ptr<Node> parse_mul() {
    auto n = parse_pow();
    while (tok_.type == TokType::Star || tok_.type == TokType::Slash ||
           tok_.type == TokType::Percent) {
      TokType t = tok_.type;
      next();
      BinaryNode::Op op = BinaryNode::Op::Mul;
      if (t == TokType::Slash)
        op = BinaryNode::Op::Div;
      if (t == TokType::Percent)
        op = BinaryNode::Op::Mod;
      n = std::make_unique<BinaryNode>(op, std::move(n), parse_pow());
    }
    return n;
  }

  std::unique_ptr<Node> parse_pow() {
    auto n = parse_unary();
    if (tok_.type == TokType::Caret) {
      next();
      n = std::make_unique<BinaryNode>(BinaryNode::Op::Pow, std::move(n), parse_pow());
    }
    return n;
  }

  std::unique_ptr<Node> parse_unary() {
    if (tok_.type == TokType::Plus) {
      next();
      return std::make_unique<UnaryNode>(UnaryNode::Op::Plus, parse_unary());
    }
    if (tok_.type == TokType::Minus) {
      next();
      return std::make_unique<UnaryNode>(UnaryNode::Op::Minus, parse_unary());
    }
    if (tok_.type == TokType::Bang || (tok_.type == TokType::Ident && tok_.text == "not")) {
      next();
      return std::make_unique<UnaryNode>(UnaryNode::Op::Not, parse_unary());
    }
    return parse_primary();
  }

  std::unique_ptr<Node> parse_primary() {
    if (tok_.type == TokType::Number) {
      double v = tok_.number;
      next();
      return std::make_unique<NumberNode>(v);
    }

    if (tok_.type == TokType::Variable) {
      std::string v = tok_.text;
      size_t idx = get_var_index(v);
      next();
      return std::make_unique<VarNode>(idx);
    }

    if (tok_.type == TokType::Ident) {
      std::string name = tok_.text;
      next();

      if (name == "pi")
        return std::make_unique<NumberNode>(3.14159265358979323846);
      if (name == "e")
        return std::make_unique<NumberNode>(2.71828182845904523536);
      if (name == "inf")
        return std::make_unique<NumberNode>(std::numeric_limits<double>::infinity());
      if (name == "epsilon")
        return std::make_unique<NumberNode>(std::numeric_limits<double>::epsilon());

      if (tok_.type != TokType::LParen) {
        throw std::runtime_error("Unknown identifier: " + name);
      }
      next();
      std::vector<std::unique_ptr<Node>> args;
      if (tok_.type != TokType::RParen) {
        args.push_back(parse_ternary());
        while (tok_.type == TokType::Comma) {
          next();
          args.push_back(parse_ternary());
        }
      }
      expect(TokType::RParen, "Expected ')' after function call");
      if (!is_known_function(name))
        throw std::runtime_error("Unknown function: " + name);
      return std::make_unique<FuncNode>(name, std::move(args));
    }

    if (tok_.type == TokType::LParen) {
      next();
      auto n = parse_ternary();
      expect(TokType::RParen, "Expected ')'");
      return n;
    }

    throw std::runtime_error("Expected expression term");
  }

  size_t get_var_index(std::string const &v) {
    auto it = var_to_idx_.find(v);
    if (it != var_to_idx_.end())
      return it->second;
    size_t idx = vars_.size();
    vars_.push_back(v);
    var_to_idx_[v] = idx;
    return idx;
  }

  static bool is_known_function(std::string const &n) {
    static std::unordered_map<std::string, bool> const k{
        {"abs", true},     {"ceil", true},  {"floor", true}, {"round", true}, {"trunc", true},
        {"sgn", true},     {"frac", true},  {"sqrt", true},  {"pow", true},   {"root", true},
        {"exp", true},     {"log", true},   {"log2", true},  {"log10", true}, {"sin", true},
        {"cos", true},     {"tan", true},   {"asin", true},  {"acos", true},  {"atan", true},
        {"atan2", true},   {"sinh", true},  {"cosh", true},  {"tanh", true},  {"asinh", true},
        {"acosh", true},   {"atanh", true}, {"min", true},   {"max", true},   {"clamp", true},
        {"inrange", true}, {"avg", true},   {"sum", true},   {"if", true},    {"mul", true},
        {"erf", true},     {"erfc", true},  {"ncdf", true},  {"hypot", true}, {"mod", true},
        {"fmod", true},    {"expm1", true}, {"log1p", true}, {"logn", true}};
    return k.find(n) != k.end();
  }

  void next() { tok_ = lx_.next(); }

  void expect(TokType t, char const *msg) {
    if (tok_.type != t)
      throw std::runtime_error(msg);
    next();
  }

  Lexer lx_;
  Token tok_;
  std::vector<std::string> &vars_;
  std::unordered_map<std::string, size_t> &var_to_idx_;
};

} // namespace

struct ExpressionEvaluator::Impl {
  std::string original_expr;
  std::vector<std::string> var_names;
  std::unordered_map<std::string, size_t> var_to_index;
  std::vector<double> var_values;
  std::unique_ptr<Node> ast;
};

ExpressionEvaluator::ExpressionEvaluator(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}

std::shared_ptr<ExpressionEvaluator> ExpressionEvaluator::compile(std::string const &expr,
                                                                  std::string *error_out) {
  if (expr.empty()) {
    if (error_out)
      *error_out = "Empty expression";
    return nullptr;
  }

  auto impl = std::make_shared<Impl>();
  impl->original_expr = expr;
  try {
    Parser p(expr, impl->var_names, impl->var_to_index);
    impl->ast = p.parse();
  } catch (std::exception const &e) {
    if (error_out)
      *error_out = e.what();
    return nullptr;
  }
  impl->var_values.resize(impl->var_names.size(), 0.0);

  return std::shared_ptr<ExpressionEvaluator>(new ExpressionEvaluator(std::move(impl)));
}

double ExpressionEvaluator::evaluate(VariableResolver const &resolver) const {
  for (size_t i = 0; i < impl_->var_names.size(); ++i) {
    impl_->var_values[i] = resolver(impl_->var_names[i]);
  }
  EvalContext ctx{&impl_->var_values};
  return impl_->ast ? impl_->ast->eval(ctx) : 0.0;
}

std::vector<std::string> const &ExpressionEvaluator::variables() const { return impl_->var_names; }

std::string const &ExpressionEvaluator::expression_string() const { return impl_->original_expr; }

} // namespace rulesforge
