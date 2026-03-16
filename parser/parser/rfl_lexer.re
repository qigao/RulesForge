// re2c --lang c
#include "rfl_lexer.hpp"
#include <cstring>

RflLexer::RflLexer(std::string_view source, std::string const& file_name)
    : source_(source)
    , file_name_(file_name)
    , cursor_(source_.data())
    , limit_(source_.data() + source_.size())
    , marker_(cursor_)
    , tok_(cursor_)
    , line_(1)
    , line_start_(cursor_)
    , rhs_mode_(false)
{}

RflToken RflLexer::make_token(TokenType type, char const* start) const {
    return {type, {start, static_cast<std::size_t>(cursor_ - start)},
            line_, static_cast<std::size_t>(start - line_start_ + 1)};
}

void RflLexer::enter_rhs_mode() {
    rhs_mode_ = true;
}

void RflLexer::skip_whitespace_and_comments() {
    for (;;) {
        char const* start = cursor_;
        /*!re2c
            re2c:define:YYCTYPE  = char;
            re2c:define:YYCURSOR = cursor_;
            re2c:define:YYLIMIT  = limit_;
            re2c:define:YYMARKER = marker_;
            re2c:yyfill:enable   = 0;
            re2c:eof             = 0;

            [ \t\r]+  { continue; }
            "\n"      { line_++; line_start_ = cursor_; continue; }

            "//" [^\n\x00]* { continue; }
            "--" [^\n\x00]* { continue; }
            "#"  [^\n\x00]* { continue; }

            "/*" {
                for (;;) {
                    if (cursor_ >= limit_) break;
                    if (*cursor_ == '\n') { line_++; line_start_ = cursor_ + 1; }
                    if (*cursor_ == '*' && cursor_ + 1 < limit_ && *(cursor_ + 1) == '/') {
                        cursor_ += 2;
                        break;
                    }
                    cursor_++;
                }
                continue;
            }

            * { cursor_ = start; return; }
            $ { cursor_ = start; return; }
        */
    }
}

RflToken RflLexer::next_token() {
    skip_whitespace_and_comments();
    if (rhs_mode_) return scan_rhs();
    return scan_normal();
}

RflToken RflLexer::scan_normal() {
    tok_ = cursor_;
    if (cursor_ >= limit_) return make_token(TokenType::TK_EOF, tok_);

    /*!re2c
        re2c:define:YYCTYPE  = char;
        re2c:define:YYCURSOR = cursor_;
        re2c:define:YYLIMIT  = limit_;
        re2c:define:YYMARKER = marker_;
        re2c:yyfill:enable   = 0;
        re2c:eof             = 0;

        // --- Hyphenated keywords (must come before identifier) ---
        "agenda-group"     / [^a-zA-Z0-9_] { return make_token(TokenType::TK_AGENDA_GROUP, tok_); }
        "activation-group" / [^a-zA-Z0-9_] { return make_token(TokenType::TK_ACTIVATION_GROUP, tok_); }
        "entry-point"      / [^a-zA-Z0-9_] { return make_token(TokenType::TK_ENTRY_POINT, tok_); }
        "no-loop"          / [^a-zA-Z0-9_] { return make_token(TokenType::TK_NO_LOOP, tok_); }
        "lock-on-active"   / [^a-zA-Z0-9_] { return make_token(TokenType::TK_LOCK_ON_ACTIVE, tok_); }
        "auto-focus"       / [^a-zA-Z0-9_] { return make_token(TokenType::TK_AUTO_FOCUS, tok_); }

        // --- Regular keywords ---
        "rule"       / [^a-zA-Z0-9_] { return make_token(TokenType::TK_RULE, tok_); }
        "when"       / [^a-zA-Z0-9_] { return make_token(TokenType::TK_WHEN, tok_); }
        "then"       / [^a-zA-Z0-9_] { return make_token(TokenType::TK_THEN, tok_); }
        "end"        / [^a-zA-Z0-9_] { return make_token(TokenType::TK_END, tok_); }
        "salience"   / [^a-zA-Z0-9_] { return make_token(TokenType::TK_SALIENCE, tok_); }
        "extends"    / [^a-zA-Z0-9_] { return make_token(TokenType::TK_EXTENDS, tok_); }
        "timer"      / [^a-zA-Z0-9_] { return make_token(TokenType::TK_TIMER, tok_); }
        "from"       / [^a-zA-Z0-9_] { return make_token(TokenType::TK_FROM, tok_); }
        "not"        / [^a-zA-Z0-9_] { return make_token(TokenType::TK_NOT, tok_); }
        "exists"     / [^a-zA-Z0-9_] { return make_token(TokenType::TK_EXISTS, tok_); }
        "collect"    / [^a-zA-Z0-9_] { return make_token(TokenType::TK_COLLECT, tok_); }
        "accumulate" / [^a-zA-Z0-9_] { return make_token(TokenType::TK_ACCUMULATE, tok_); }
        "forall"     / [^a-zA-Z0-9_] { return make_token(TokenType::TK_FORALL, tok_); }
        "eval"       / [^a-zA-Z0-9_] { return make_token(TokenType::TK_EVAL, tok_); }
        "in"         / [^a-zA-Z0-9_] { return make_token(TokenType::TK_IN, tok_); }
        "function"   / [^a-zA-Z0-9_] { return make_token(TokenType::TK_FUNCTION, tok_); }
        "declare"    / [^a-zA-Z0-9_] { return make_token(TokenType::TK_DECLARE, tok_); }
        "enum"       / [^a-zA-Z0-9_] { return make_token(TokenType::TK_ENUM, tok_); }
        "query"      / [^a-zA-Z0-9_] { return make_token(TokenType::TK_QUERY, tok_); }
        "global"     / [^a-zA-Z0-9_] { return make_token(TokenType::TK_GLOBAL, tok_); }
        "package"    / [^a-zA-Z0-9_] { return make_token(TokenType::TK_PACKAGE, tok_); }
        "import"     / [^a-zA-Z0-9_] { return make_token(TokenType::TK_IMPORT, tok_); }
        "or"         / [^a-zA-Z0-9_] { return make_token(TokenType::TK_OR, tok_); }
        "modify"     / [^a-zA-Z0-9_] { return make_token(TokenType::TK_MODIFY, tok_); }
        "nil"        / [^a-zA-Z0-9_] { return make_token(TokenType::TK_NIL, tok_); }
        "true"       / [^a-zA-Z0-9_] { return make_token(TokenType::TK_TRUE, tok_); }
        "false"      / [^a-zA-Z0-9_] { return make_token(TokenType::TK_FALSE, tok_); }
        "this"       / [^a-zA-Z0-9_] { return make_token(TokenType::TK_THIS, tok_); }
        "unnest"     / [^a-zA-Z0-9_] { return make_token(TokenType::TK_UNNEST, tok_); }
        "binary"     / [^a-zA-Z0-9_] { return make_token(TokenType::TK_BINARY, tok_); }
        "codec"      / [^a-zA-Z0-9_] { return make_token(TokenType::TK_CODEC, tok_); }
        "enabled"    / [^a-zA-Z0-9_] { return make_token(TokenType::TK_ENABLED, tok_); }
        "duration"   / [^a-zA-Z0-9_] { return make_token(TokenType::TK_DURATION_KW, tok_); }

        // --- Word operators ---
        "startsWith"  / [^a-zA-Z0-9_] { return make_token(TokenType::TK_STARTS_WITH, tok_); }
        "endsWith"    / [^a-zA-Z0-9_] { return make_token(TokenType::TK_ENDS_WITH, tok_); }
        "lengthIs"    / [^a-zA-Z0-9_] { return make_token(TokenType::TK_LENGTH_IS, tok_); }
        "contains"    / [^a-zA-Z0-9_] { return make_token(TokenType::TK_CONTAINS, tok_); }
        "containsKey" / [^a-zA-Z0-9_] { return make_token(TokenType::TK_CONTAINS_KEY, tok_); }
        "matches"     / [^a-zA-Z0-9_] { return make_token(TokenType::TK_MATCHES, tok_); }
        "memberOf"    / [^a-zA-Z0-9_] { return make_token(TokenType::TK_MEMBER_OF, tok_); }

        // --- Temporal keywords ---
        "after"      / [^a-zA-Z0-9_] { return make_token(TokenType::TK_AFTER, tok_); }
        "before"     / [^a-zA-Z0-9_] { return make_token(TokenType::TK_BEFORE, tok_); }
        "within"     / [^a-zA-Z0-9_] { return make_token(TokenType::TK_WITHIN, tok_); }
        "of"         / [^a-zA-Z0-9_] { return make_token(TokenType::TK_OF, tok_); }
        "coincides"  / [^a-zA-Z0-9_] { return make_token(TokenType::TK_COINCIDES, tok_); }
        "during"     / [^a-zA-Z0-9_] { return make_token(TokenType::TK_DURING, tok_); }

        // --- Duration literals (before integers: longest match) ---
        [0-9]+ "ms" { return make_token(TokenType::TK_DURATION, tok_); }
        [0-9]+ "s"  { return make_token(TokenType::TK_DURATION, tok_); }
        [0-9]+ "m" / [^a-zA-Z0-9_] { return make_token(TokenType::TK_DURATION, tok_); }
        [0-9]+ "h"  { return make_token(TokenType::TK_DURATION, tok_); }

        // --- Numeric literals ---
        "-"? [0-9]+ "." [0-9]+ { return make_token(TokenType::TK_DOUBLE, tok_); }
        "-"? [0-9]+            { return make_token(TokenType::TK_INTEGER, tok_); }

        // --- String literals ---
        ["] ([^"\x00\\] | [\\] .)* ["] { return make_token(TokenType::TK_STRING, tok_); }
        ['] ([^'\x00\\] | [\\] .)* ['] { return make_token(TokenType::TK_STRING, tok_); }

        // --- Variable ($identifier) ---
        "$" [a-zA-Z_] [a-zA-Z0-9_]* { return make_token(TokenType::TK_VARIABLE, tok_); }

        // --- Identifier ---
        [a-zA-Z_] [a-zA-Z0-9_]* { return make_token(TokenType::TK_IDENTIFIER, tok_); }

        // --- Multi-char operators ---
        "==" { return make_token(TokenType::TK_EQ, tok_); }
        "!=" { return make_token(TokenType::TK_NE, tok_); }
        "<=" { return make_token(TokenType::TK_LE, tok_); }
        ">=" { return make_token(TokenType::TK_GE, tok_); }
        "&&" { return make_token(TokenType::TK_AND_AND, tok_); }
        "||" { return make_token(TokenType::TK_OR_OR, tok_); }
        ":=" { return make_token(TokenType::TK_COLON_EQ, tok_); }
        "!." { return make_token(TokenType::TK_BANG_DOT, tok_); }
        ".*" { return make_token(TokenType::TK_DOTSTAR, tok_); }

        // --- Single-char operators ---
        "<"  { return make_token(TokenType::TK_LT, tok_); }
        ">"  { return make_token(TokenType::TK_GT, tok_); }
        "+"  { return make_token(TokenType::TK_PLUS, tok_); }
        "-"  { return make_token(TokenType::TK_MINUS, tok_); }
        "*"  { return make_token(TokenType::TK_STAR, tok_); }
        "/"  { return make_token(TokenType::TK_SLASH, tok_); }
        "!"  { return make_token(TokenType::TK_BANG, tok_); }

        // --- Delimiters ---
        "("  { return make_token(TokenType::TK_LPAREN, tok_); }
        ")"  { return make_token(TokenType::TK_RPAREN, tok_); }
        "["  { return make_token(TokenType::TK_LBRACKET, tok_); }
        "]"  { return make_token(TokenType::TK_RBRACKET, tok_); }
        "{"  { return make_token(TokenType::TK_LBRACE, tok_); }
        "}"  { return make_token(TokenType::TK_RBRACE, tok_); }
        ":"  { return make_token(TokenType::TK_COLON, tok_); }
        ";"  { return make_token(TokenType::TK_SEMICOLON, tok_); }
        ","  { return make_token(TokenType::TK_COMMA, tok_); }
        "."  { return make_token(TokenType::TK_DOT, tok_); }
        "@"  { return make_token(TokenType::TK_AT, tok_); }

        // --- EOF ---
        $    { return make_token(TokenType::TK_EOF, tok_); }

        // --- Error: unexpected character ---
        *    { return make_token(TokenType::TK_ERROR, tok_); }
    */
}

RflToken RflLexer::scan_rhs() {
    tok_ = cursor_;
    if (cursor_ >= limit_) return make_token(TokenType::TK_EOF, tok_);

    // In RHS mode, we only recognize: modify, end, variables, delimiters, and raw code chunks.
    // Check for keywords first.
    /*!re2c
        re2c:define:YYCTYPE  = char;
        re2c:define:YYCURSOR = cursor_;
        re2c:define:YYLIMIT  = limit_;
        re2c:define:YYMARKER = marker_;
        re2c:yyfill:enable   = 0;
        re2c:eof             = 0;

        "end"    / [^a-zA-Z0-9_] { rhs_mode_ = false; return make_token(TokenType::TK_END, tok_); }
        "modify" / [^a-zA-Z0-9_] { return make_token(TokenType::TK_MODIFY, tok_); }

        "$" [a-zA-Z_] [a-zA-Z0-9_]* { return make_token(TokenType::TK_VARIABLE, tok_); }

        "(" { return make_token(TokenType::TK_LPAREN, tok_); }
        ")" { return make_token(TokenType::TK_RPAREN, tok_); }
        "{" { return make_token(TokenType::TK_LBRACE, tok_); }
        "}" { return make_token(TokenType::TK_RBRACE, tok_); }
        ";" { return make_token(TokenType::TK_SEMICOLON, tok_); }
        "." { return make_token(TokenType::TK_DOT, tok_); }

        // String literals in RHS
        ["] ([^"\x00\\] | [\\] .)* ["] { return make_token(TokenType::TK_STRING, tok_); }
        ['] ([^'\x00\\] | [\\] .)* ['] { return make_token(TokenType::TK_STRING, tok_); }

        $    { return make_token(TokenType::TK_EOF, tok_); }

        // Everything else is a code chunk — consume until we see modify, end, or EOF
        * {
            // Back up and capture a code chunk
            cursor_ = tok_;
            char const* chunk_start = cursor_;
            for (;;) {
                if (cursor_ >= limit_) break;
                // Check for "end" or "modify" keyword boundary
                if (cursor_ + 3 <= limit_ && cursor_[0] == 'e' && cursor_[1] == 'n' && cursor_[2] == 'd') {
                    if (cursor_ + 3 >= limit_ || !(cursor_[3] >= 'a' && cursor_[3] <= 'z')
                        && !(cursor_[3] >= 'A' && cursor_[3] <= 'Z')
                        && !(cursor_[3] >= '0' && cursor_[3] <= '9')
                        && cursor_[3] != '_') {
                        break;
                    }
                }
                if (cursor_ + 6 <= limit_ && cursor_[0] == 'm' && cursor_[1] == 'o' && cursor_[2] == 'd'
                    && cursor_[3] == 'i' && cursor_[4] == 'f' && cursor_[5] == 'y') {
                    if (cursor_ + 6 >= limit_ || !(cursor_[6] >= 'a' && cursor_[6] <= 'z')
                        && !(cursor_[6] >= 'A' && cursor_[6] <= 'Z')
                        && !(cursor_[6] >= '0' && cursor_[6] <= '9')
                        && cursor_[6] != '_') {
                        break;
                    }
                }
                if (*cursor_ == '\n') { line_++; line_start_ = cursor_ + 1; }
                cursor_++;
            }
            if (cursor_ > chunk_start) {
                return make_token(TokenType::TK_CODE_CHUNK, chunk_start);
            }
            return make_token(TokenType::TK_EOF, tok_);
        }
    */
}
