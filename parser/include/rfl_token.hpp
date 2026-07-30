#ifndef RFL_TOKEN_HPP
#define RFL_TOKEN_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

enum class TokenType : uint8_t {
    // --- Literals ---
    TK_INTEGER,
    TK_DOUBLE,
    TK_STRING,
    TK_DURATION,

    // --- Identifiers ---
    TK_IDENTIFIER,
    TK_VARIABLE,       // $name

    // --- Keywords ---
    TK_RULE,
    TK_WHEN,
    TK_THEN,
    TK_END,
    TK_SALIENCE,
    TK_EXTENDS,
    TK_AGENDA_GROUP,
    TK_ACTIVATION_GROUP,
    TK_TIMER,
    TK_FROM,
    TK_NOT,
    TK_EXISTS,
    TK_COLLECT,
    TK_ACCUMULATE,
    TK_FORALL,
    TK_EVAL,
    TK_ENTRY_POINT,
    TK_IN,
    TK_FUNCTION,
    TK_DECLARE,
    TK_ENUM,
    TK_QUERY,
    TK_GLOBAL,
    TK_PACKAGE,
    TK_IMPORT,
    TK_OR,
    TK_MODIFY,
    TK_NIL,
    TK_TRUE,
    TK_FALSE,
    TK_THIS,
    TK_UNNEST,
    TK_NO_LOOP,
    TK_LOCK_ON_ACTIVE,
    TK_ENABLED,
    TK_DURATION_KW,
    TK_AUTO_FOCUS,
    TK_BINARY,      // binary
    TK_CODEC,       // codec

    // --- Word operators ---
    TK_STARTS_WITH,
    TK_ENDS_WITH,
    TK_LENGTH_IS,
    TK_CONTAINS,
    TK_MATCHES,
    TK_MEMBER_OF,
    TK_CONTAINS_KEY,
    TK_HAS_FLAG,

    // --- Temporal keywords ---
    TK_AFTER,
    TK_BEFORE,
    TK_WITHIN,
    TK_OF,
    TK_COINCIDES,
    TK_DURING,

    // --- Window Keywords ---
    TK_OVER,
    TK_WINDOW,
    TK_TIME,
    TK_LENGTH,

    // --- Symbolic operators ---
    TK_EQ,          // ==
    TK_NE,          // !=
    TK_LT,          // <
    TK_GT,          // >
    TK_LE,          // <=
    TK_GE,          // >=
    TK_PLUS,        // +
    TK_MINUS,       // -
    TK_STAR,        // *
    TK_SLASH,       // /
    TK_AND_AND,     // &&
    TK_OR_OR,       // ||
    TK_BANG,        // !
    TK_BANG_DOT,    // !.

    // --- Delimiters ---
    TK_LPAREN,      // (
    TK_RPAREN,      // )
    TK_LBRACKET,    // [
    TK_RBRACKET,    // ]
    TK_LBRACE,      // {
    TK_RBRACE,      // }
    TK_COLON,       // :
    TK_COLON_EQ,    // :=
    TK_SEMICOLON,   // ;
    TK_COMMA,       // ,
    TK_DOT,         // .
    TK_DOTSTAR,     // .*
    TK_AT,          // @
    TK_DOLLAR,      // $
    TK_HASH,        // #

    // --- RHS special ---
    TK_CODE_CHUNK,

    // --- Control ---
    TK_EOF,
    TK_ERROR
};

struct RflToken {
    enum TokenType type;
    std::string_view text;
    std::size_t line;
    std::size_t column;
};

// POD version for Lemon's C union — no std::string_view, just raw pointer+length
struct RflLemonToken {
    char const* text;
    std::size_t text_len;
    std::size_t line;
    std::size_t column;

    std::string_view as_sv() const { return {text, text_len}; }
    std::string as_string() const { return std::string(text, text_len); }
};

inline char const* token_type_name(enum TokenType t) {
    switch (t) {
        case TokenType::TK_INTEGER:          return "INTEGER";
        case TokenType::TK_DOUBLE:           return "DOUBLE";
        case TokenType::TK_STRING:           return "STRING";
        case TokenType::TK_DURATION:         return "DURATION";
        case TokenType::TK_IDENTIFIER:       return "IDENTIFIER";
        case TokenType::TK_VARIABLE:         return "VARIABLE";
        case TokenType::TK_RULE:             return "RULE";
        case TokenType::TK_WHEN:             return "WHEN";
        case TokenType::TK_THEN:             return "THEN";
        case TokenType::TK_END:              return "END";
        case TokenType::TK_SALIENCE:         return "SALIENCE";
        case TokenType::TK_EXTENDS:          return "EXTENDS";
        case TokenType::TK_AGENDA_GROUP:     return "AGENDA_GROUP";
        case TokenType::TK_ACTIVATION_GROUP: return "ACTIVATION_GROUP";
        case TokenType::TK_TIMER:            return "TIMER";
        case TokenType::TK_FROM:             return "FROM";
        case TokenType::TK_NOT:              return "NOT";
        case TokenType::TK_EXISTS:           return "EXISTS";
        case TokenType::TK_COLLECT:          return "COLLECT";
        case TokenType::TK_ACCUMULATE:       return "ACCUMULATE";
        case TokenType::TK_FORALL:           return "FORALL";
        case TokenType::TK_EVAL:             return "EVAL";
        case TokenType::TK_ENTRY_POINT:      return "ENTRY_POINT";
        case TokenType::TK_IN:              return "IN";
        case TokenType::TK_FUNCTION:         return "FUNCTION";
        case TokenType::TK_DECLARE:          return "DECLARE";
        case TokenType::TK_ENUM:             return "ENUM";
        case TokenType::TK_QUERY:            return "QUERY";
        case TokenType::TK_GLOBAL:           return "GLOBAL";
        case TokenType::TK_PACKAGE:          return "PACKAGE";
        case TokenType::TK_IMPORT:           return "IMPORT";
        case TokenType::TK_OR:               return "OR";
        case TokenType::TK_MODIFY:           return "MODIFY";
        case TokenType::TK_NIL:              return "NIL";
        case TokenType::TK_TRUE:             return "TRUE";
        case TokenType::TK_FALSE:            return "FALSE";
        case TokenType::TK_THIS:             return "THIS";
        case TokenType::TK_UNNEST:           return "UNNEST";
        case TokenType::TK_NO_LOOP:          return "NO_LOOP";
        case TokenType::TK_LOCK_ON_ACTIVE:   return "LOCK_ON_ACTIVE";
        case TokenType::TK_ENABLED:          return "ENABLED";
        case TokenType::TK_DURATION_KW:      return "DURATION_KW";
        case TokenType::TK_AUTO_FOCUS:       return "AUTO_FOCUS";
        case TokenType::TK_BINARY:           return "BINARY";
        case TokenType::TK_CODEC:            return "CODEC";
        case TokenType::TK_STARTS_WITH:      return "STARTS_WITH";
        case TokenType::TK_ENDS_WITH:        return "ENDS_WITH";
        case TokenType::TK_LENGTH_IS:        return "LENGTH_IS";
        case TokenType::TK_CONTAINS:         return "CONTAINS";
        case TokenType::TK_MATCHES:          return "MATCHES";
        case TokenType::TK_MEMBER_OF:        return "MEMBER_OF";
        case TokenType::TK_CONTAINS_KEY:     return "CONTAINS_KEY";
        case TokenType::TK_HAS_FLAG:         return "HAS_FLAG";
        case TokenType::TK_AFTER:            return "AFTER";
        case TokenType::TK_BEFORE:           return "BEFORE";
        case TokenType::TK_WITHIN:           return "WITHIN";
        case TokenType::TK_OF:               return "OF";
        case TokenType::TK_COINCIDES:        return "COINCIDES";
        case TokenType::TK_DURING:           return "DURING";
        case TokenType::TK_OVER:             return "OVER";
        case TokenType::TK_WINDOW:           return "WINDOW";
        case TokenType::TK_TIME:             return "TIME";
        case TokenType::TK_LENGTH:           return "LENGTH";
        case TokenType::TK_EQ:              return "==";
        case TokenType::TK_NE:              return "!=";
        case TokenType::TK_LT:              return "<";
        case TokenType::TK_GT:              return ">";
        case TokenType::TK_LE:              return "<=";
        case TokenType::TK_GE:              return ">=";
        case TokenType::TK_PLUS:             return "+";
        case TokenType::TK_MINUS:            return "-";
        case TokenType::TK_STAR:             return "*";
        case TokenType::TK_SLASH:            return "/";
        case TokenType::TK_AND_AND:          return "&&";
        case TokenType::TK_OR_OR:            return "||";
        case TokenType::TK_BANG:             return "!";
        case TokenType::TK_BANG_DOT:         return "!.";
        case TokenType::TK_LPAREN:           return "(";
        case TokenType::TK_RPAREN:           return ")";
        case TokenType::TK_LBRACKET:         return "[";
        case TokenType::TK_RBRACKET:         return "]";
        case TokenType::TK_LBRACE:           return "{";
        case TokenType::TK_RBRACE:           return "}";
        case TokenType::TK_COLON:            return ":";
        case TokenType::TK_COLON_EQ:         return ":=";
        case TokenType::TK_SEMICOLON:        return ";";
        case TokenType::TK_COMMA:            return ",";
        case TokenType::TK_DOT:              return ".";
        case TokenType::TK_DOTSTAR:          return ".*";
        case TokenType::TK_AT:               return "@";
        case TokenType::TK_DOLLAR:           return "$";
        case TokenType::TK_HASH:             return "#";
        case TokenType::TK_CODE_CHUNK:       return "CODE_CHUNK";
        case TokenType::TK_EOF:              return "EOF";
        case TokenType::TK_ERROR:            return "ERROR";
    }
    return "UNKNOWN";
}

#endif // RFL_TOKEN_HPP
