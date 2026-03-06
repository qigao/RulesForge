#include "rfl_parser_impl.hpp"
#include "rfl_lexer.hpp"
#include "rfl_token.hpp"

// Lemon-generated parser interface (compiled as C++ via LANGUAGE CXX)
// %extra_context passes ctx at alloc time, not per-parse call.
void* RflParseAlloc(void* (*mallocProc)(size_t), RflParserContext* ctx);
void  RflParseFree(void* parser, void (*freeProc)(void*));
void  RflParse(void* parser, int tokenType, RflLemonToken token);

// Map TokenType enum to Lemon token IDs.
// Lemon generates TK_xxx constants in rfl_grammar_lemon.h.
// Since we used %token_prefix TK_, the generated constants match our enum names.
#include "rfl_grammar_lemon.h"

static int token_type_to_lemon_id(TokenType t) {
    switch (t) {
        case TokenType::TK_INTEGER:          return TOK_INTEGER;
        case TokenType::TK_DOUBLE:           return TOK_DOUBLE;
        case TokenType::TK_STRING:           return TOK_STRING;
        case TokenType::TK_DURATION:         return TOK_DURATION;
        case TokenType::TK_IDENTIFIER:       return TOK_IDENTIFIER;
        case TokenType::TK_VARIABLE:         return TOK_VARIABLE;
        case TokenType::TK_RULE:             return TOK_RULE;
        case TokenType::TK_WHEN:             return TOK_WHEN;
        case TokenType::TK_THEN:             return TOK_THEN;
        case TokenType::TK_END:              return TOK_END;
        case TokenType::TK_SALIENCE:         return TOK_SALIENCE;
        case TokenType::TK_EXTENDS:          return TOK_EXTENDS;
        case TokenType::TK_AGENDA_GROUP:     return TOK_AGENDA_GROUP;
        case TokenType::TK_ACTIVATION_GROUP: return TOK_ACTIVATION_GROUP;
        case TokenType::TK_TIMER:            return TOK_TIMER;
        case TokenType::TK_FROM:             return TOK_FROM;
        case TokenType::TK_NOT:              return TOK_NOT;
        case TokenType::TK_EXISTS:           return TOK_EXISTS;
        case TokenType::TK_COLLECT:          return TOK_COLLECT;
        case TokenType::TK_ACCUMULATE:       return TOK_ACCUMULATE;
        case TokenType::TK_FORALL:           return TOK_FORALL;
        case TokenType::TK_EVAL:             return TOK_EVAL;
        case TokenType::TK_ENTRY_POINT:      return TOK_ENTRY_POINT;
        case TokenType::TK_IN:              return TOK_IN;
        case TokenType::TK_FUNCTION:         return TOK_FUNCTION;
        case TokenType::TK_DECLARE:          return TOK_DECLARE;
        case TokenType::TK_QUERY:            return TOK_QUERY;
        case TokenType::TK_GLOBAL:           return TOK_GLOBAL;
        case TokenType::TK_PACKAGE:          return TOK_PACKAGE;
        case TokenType::TK_IMPORT:           return TOK_IMPORT;
        case TokenType::TK_OR:               return TOK_OR;
        case TokenType::TK_MODIFY:           return TOK_MODIFY;
        case TokenType::TK_NIL:              return TOK_NIL;
        case TokenType::TK_TRUE:             return TOK_TRUE;
        case TokenType::TK_FALSE:            return TOK_FALSE;
        case TokenType::TK_THIS:             return TOK_THIS;
        case TokenType::TK_UNNEST:           return TOK_UNNEST;
        case TokenType::TK_JMESPATH:         return TOK_JMESPATH;
        case TokenType::TK_DSV:              return TOK_DSV;
        case TokenType::TK_CSV:              return TOK_CSV;
        case TokenType::TK_NO_LOOP:          return TOK_NO_LOOP;
        case TokenType::TK_LOCK_ON_ACTIVE:   return TOK_LOCK_ON_ACTIVE;
        case TokenType::TK_ENABLED:          return TOK_ENABLED;
        case TokenType::TK_DURATION_KW:      return TOK_DURATION_KW;
        case TokenType::TK_AUTO_FOCUS:       return TOK_AUTO_FOCUS;
        case TokenType::TK_STARTS_WITH:      return TOK_STARTS_WITH;
        case TokenType::TK_ENDS_WITH:        return TOK_ENDS_WITH;
        case TokenType::TK_LENGTH_IS:        return TOK_LENGTH_IS;
        case TokenType::TK_CONTAINS:         return TOK_CONTAINS;
        case TokenType::TK_MATCHES:          return TOK_MATCHES;
        case TokenType::TK_MEMBER_OF:        return TOK_MEMBER_OF;
        case TokenType::TK_AFTER:            return TOK_AFTER;
        case TokenType::TK_BEFORE:           return TOK_BEFORE;
        case TokenType::TK_WITHIN:           return TOK_WITHIN;
        case TokenType::TK_OF:               return TOK_OF;
        case TokenType::TK_COINCIDES:        return TOK_COINCIDES;
        case TokenType::TK_DURING:           return TOK_DURING;
        case TokenType::TK_EQ:              return TOK_EQ;
        case TokenType::TK_NE:              return TOK_NE;
        case TokenType::TK_LT:              return TOK_LT;
        case TokenType::TK_GT:              return TOK_GT;
        case TokenType::TK_LE:              return TOK_LE;
        case TokenType::TK_GE:              return TOK_GE;
        case TokenType::TK_PLUS:             return TOK_PLUS;
        case TokenType::TK_MINUS:            return TOK_MINUS;
        case TokenType::TK_STAR:             return TOK_STAR;
        case TokenType::TK_SLASH:            return TOK_SLASH;
        case TokenType::TK_AND_AND:          return TOK_AND_AND;
        case TokenType::TK_OR_OR:            return TOK_OR_OR;
        case TokenType::TK_BANG:             return TOK_BANG;
        case TokenType::TK_BANG_DOT:         return TOK_BANG_DOT;
        case TokenType::TK_LPAREN:           return TOK_LPAREN;
        case TokenType::TK_RPAREN:           return TOK_RPAREN;
        case TokenType::TK_LBRACKET:         return TOK_LBRACKET;
        case TokenType::TK_RBRACKET:         return TOK_RBRACKET;
        case TokenType::TK_LBRACE:           return TOK_LBRACE;
        case TokenType::TK_RBRACE:           return TOK_RBRACE;
        case TokenType::TK_COLON:            return TOK_COLON;
        case TokenType::TK_COLON_EQ:         return TOK_COLON; // not used in grammar
        case TokenType::TK_SEMICOLON:        return TOK_SEMICOLON;
        case TokenType::TK_COMMA:            return TOK_COMMA;
        case TokenType::TK_DOT:              return TOK_DOT;
        case TokenType::TK_DOTSTAR:          return TOK_DOTSTAR;
        case TokenType::TK_AT:               return TOK_AT;
        case TokenType::TK_DOLLAR:           return TOK_VARIABLE; // not used directly in grammar
        case TokenType::TK_HASH:             return TOK_IDENTIFIER; // not used directly in grammar
        case TokenType::TK_CODE_CHUNK:       return TOK_CODE_CHUNK;
        case TokenType::TK_EOF:              return 0; // Lemon uses 0 for EOF
        case TokenType::TK_ERROR:            return 0;
    }
    return 0;
}

parser_state rfl_parse_lemon(std::string_view input,
                             std::string const& source_name,
                             std::vector<StructuredError>& out_errors) {
    RflParserContext ctx;
    ctx.source_name = source_name;

    RflLexer lexer(input, source_name);
    void* parser = RflParseAlloc(malloc, &ctx);

    for (;;) {
        RflToken tok = lexer.next_token();

        if (tok.type == TokenType::TK_ERROR) {
            ctx.add_error(tok.line, tok.column,
                "Unexpected character: '" + std::string(tok.text) + "'");
            continue;
        }

        // Switch lexer to RHS mode when we see THEN
        if (tok.type == TokenType::TK_THEN) {
            lexer.enter_rhs_mode();
        }

        int lemon_id = token_type_to_lemon_id(tok.type);
        RflLemonToken ltok{tok.text.data(), tok.text.size(), tok.line, tok.column};
        RflParse(parser, lemon_id, ltok);

        if (tok.type == TokenType::TK_EOF || ctx.failed) break;

        // Bail early on too many errors
        if (ctx.errors.size() > 50) {
            ctx.add_error(tok.line, tok.column, "Too many errors, aborting parse");
            break;
        }
    }

    RflParseFree(parser, free);

    out_errors = std::move(ctx.errors);
    return std::move(ctx.state);
}
