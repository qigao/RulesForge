#include "scalar_lexer.hpp"
#include <stdexcept>

namespace rulesforge::scalar {
Token Lexer::next() {
    for (;;) {
        char const* start = input_.data() + position_;
        char const* YYCURSOR = start;
        char const* YYLIMIT = input_.data() + input_.size();
        [[maybe_unused]] char const* YYMARKER = start;
        auto emit = [&](Lexeme kind) {
            auto size = static_cast<size_t>(YYCURSOR - start);
            position_ += size;
            return Token{kind, {start, size}};
        };
        /*!re2c
            re2c:define:YYCTYPE = char;
            re2c:yyfill:enable = 0;
            re2c:eof = 0;
            id = [a-zA-Z_] [a-zA-Z_0-9]*;
            digits = [0-9]+;
            num = (digits ("." [0-9]*)? | "." digits) ([eE] [+-]? digits)?;
            dq = "\"" ([^"\\\x00] | "\\" [^\x00])* "\"";
            sq = "'" ([^'\\\x00] | "\\" [^\x00])* "'";
            [ \t\r\n]+ { emit(Lexeme::End); continue; }
            num { return emit(Lexeme::Number); }
            dq | sq { return emit(Lexeme::String); }
            "$" id { return emit(Lexeme::Variable); }
            id { return emit(Lexeme::Identifier); }
            "+" { return emit(Lexeme::Plus); }
            "-" { return emit(Lexeme::Minus); }
            "*" { return emit(Lexeme::Star); }
            "/" { return emit(Lexeme::Slash); }
            "!" { return emit(Lexeme::Not); }
            "&&" { return emit(Lexeme::And); }
            "||" { return emit(Lexeme::Or); }
            "==" { return emit(Lexeme::EQ); }
            "!=" { return emit(Lexeme::NE); }
            ">" { return emit(Lexeme::GT); }
            ">=" { return emit(Lexeme::GE); }
            "<" { return emit(Lexeme::LT); }
            "<=" { return emit(Lexeme::LE); }
            "(" { return emit(Lexeme::Open); }
            ")" { return emit(Lexeme::Close); }
            "," { return emit(Lexeme::Comma); }
            "." { return emit(Lexeme::Dot); }
            "[" { return emit(Lexeme::LeftIndex); }
            "]" { return emit(Lexeme::RightIndex); }
            * { throw std::invalid_argument("Invalid scalar expression token"); }
            $ { return {Lexeme::End, {}}; }
        */
    }
}
} // namespace rulesforge::scalar
