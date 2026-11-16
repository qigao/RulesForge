#pragma once
#include <string>
#include <string_view>

namespace rulesforge::scalar {
enum class Lexeme {
    End, Number, String, Variable, Identifier,
    Plus, Minus, Star, Slash, Not, And, Or,
    EQ, NE, GT, GE, LT, LE, Open, Close, Comma, Dot,
    LeftIndex, RightIndex
};
struct Token { Lexeme kind; std::string_view text; };
// Borrows the complete owning string, including its trailing sentinel.
class Lexer {
    std::string_view input_;
    size_t position_ = 0;
public:
    explicit Lexer(std::string const& input) : input_(input) {}
    Token next();
};
} // namespace rulesforge::scalar
