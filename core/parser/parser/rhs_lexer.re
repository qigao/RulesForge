#include "rhs_parser.hpp"

namespace rulesforge {
RhsParser::Lexer::Lexer(std::string const& input) : input_(input) {}

RhsParser::Token RhsParser::Lexer::decode_string(Token token) {
    auto raw = token.text.substr(1, token.text.size() - 2);
    if (raw.find('\\') == std::string_view::npos) {
        token.text = raw;
        return token;
    }
    OwnedText decoded;
    size_t begin = 0;
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] != '\\') continue;
        decoded.append(raw.substr(begin, i - begin));
        char value = raw[++i]; // The DFA admitted one character after every backslash.
        switch (value) {
            case 'n': value = '\n'; break;
            case 't': value = '\t'; break;
            case 'r': value = '\r'; break;
            case '\\': case '"': case '\'': break;
            default: return {Token::TOK_ERROR, "Unsupported string escape", token.line, token.column};
        }
        decoded.append(std::string_view(&value, 1));
        begin = i + 1;
    }
    decoded.append(raw.substr(begin));
    decoded_strings_.push_back(std::move(decoded));
    token.text = decoded_strings_.back().view();
    return token;
}

RhsParser::Token RhsParser::Lexer::next() {
    if (has_peeked_) {
        has_peeked_ = false;
        return peeked_;
    }
    for (;;) {
        char const* start = input_.data() + pos_;
        char const* YYCURSOR = start;
        char const* YYLIMIT = input_.data() + input_.size();
        [[maybe_unused]] char const* YYMARKER = start;
        size_t line = line_, column = column_;
        auto emit = [&](Token::Type type) {
            std::string_view text(start, YYCURSOR - start);
            pos_ += text.size();
            if (type == Token::TOK_STRING) {
                for (char ch : text) {
                    if (ch == '\n') { ++line_; column_ = 1; }
                    else ++column_;
                }
            } else column_ += text.size();
            return Token{type, text, line, column};
        };
        // input_ borrows the complete std::string passed to synchronous parse().
        // Its NUL at YYLIMIT is the re2c sentinel, not a required extra allocation.
        /*!re2c
            re2c:define:YYCTYPE = char;
            re2c:yyfill:enable = 0;
            re2c:eof = 0;
            ident = [a-zA-Z_] [a-zA-Z_0-9]*;
            integer = "-"? [0-9]+;
            real = integer "." [0-9]*;
            dq = "\"" ([^"\\\x00] | "\\" [^\x00])* "\"";
            sq = "'" ([^'\\\x00] | "\\" [^\x00])* "'";

            [ \t\r]+ { emit(Token::TOK_END); continue; }
            "\n" { ++pos_; ++line_; column_ = 1; continue; }
            "//" [^\n\x00]* { emit(Token::TOK_END); continue; }
            dq | sq { return decode_string(emit(Token::TOK_STRING)); }
            integer { return emit(Token::TOK_INTEGER); }
            real { return emit(Token::TOK_DOUBLE); }
            "$" [a-zA-Z_0-9]* { return emit(Token::TOK_VARIABLE); }
            "update" { return emit(Token::TOK_UPDATE); }
            "insert" { return emit(Token::TOK_INSERT); }
            "insertLogical" { return emit(Token::TOK_INSERT_LOGICAL); }
            "retract" { return emit(Token::TOK_RETRACT); }
            "halt" { return emit(Token::TOK_HALT); }
            "setFocus" { return emit(Token::TOK_SET_FOCUS); }
            "if" { return emit(Token::TOK_IF); }
            "else" { return emit(Token::TOK_ELSE); }
            "for" { return emit(Token::TOK_FOR); }
            "in" { return emit(Token::TOK_IN); }
            "while" { return emit(Token::TOK_WHILE); }
            "switch" { return emit(Token::TOK_SWITCH); }
            "case" { return emit(Token::TOK_CASE); }
            "default" { return emit(Token::TOK_DEFAULT); }
            "break" { return emit(Token::TOK_BREAK); }
            "continue" { return emit(Token::TOK_CONTINUE); }
            "true" { return emit(Token::TOK_TRUE); }
            "false" { return emit(Token::TOK_FALSE); }
            "{" { return emit(Token::TOK_LBRACE); }
            "}" { return emit(Token::TOK_RBRACE); }
            "(" { return emit(Token::TOK_LPAREN); }
            ")" { return emit(Token::TOK_RPAREN); }
            "," { return emit(Token::TOK_COMMA); }
            "." { return emit(Token::TOK_DOT); }
            "=" { return emit(Token::TOK_ASSIGN); }
            "+" { return emit(Token::TOK_PLUS); }
            "-" { return emit(Token::TOK_MINUS); }
            "*" { return emit(Token::TOK_STAR); }
            "/" { return emit(Token::TOK_SLASH); }
            "%" { return emit(Token::TOK_MOD); }
            "^" { return emit(Token::TOK_CARET); }
            "?" { return emit(Token::TOK_QUESTION); }
            ":" { return emit(Token::TOK_COLON); }
            ">" { return emit(Token::TOK_GT); }
            "<" { return emit(Token::TOK_LT); }
            ">=" { return emit(Token::TOK_GE); }
            "<=" { return emit(Token::TOK_LE); }
            "==" { return emit(Token::TOK_EQ); }
            "!=" { return emit(Token::TOK_NE); }
            "&&" { return emit(Token::TOK_AND); }
            "||" { return emit(Token::TOK_OR); }
            ident { return emit(Token::TOK_IDENTIFIER); }
            * {
                auto token = emit(Token::TOK_ERROR);
                if (*start == '"' || *start == '\'') token.text = "Unterminated string literal";
                return token;
            }
            $ { return {Token::TOK_END, {}, line_, column_}; }
        */
    }
}
RhsParser::Token RhsParser::Lexer::peek() {
    if (!has_peeked_) {
        peeked_ = next();
        has_peeked_ = true;
    }
    return peeked_;
}
} // namespace rulesforge
