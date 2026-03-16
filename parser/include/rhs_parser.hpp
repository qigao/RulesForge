#ifndef RHS_PARSER_HPP
#define RHS_PARSER_HPP

#include "core/rhs_actions.hpp"
#include <map>
#include <string>
#include <vector>

namespace rulesforge {

/**
 * @brief Parser for native RHS syntax.
 *
 * Parses RHS code like:
 *   update $o { status = "processed", tax = total * 0.1 }
 *   insert Discount { amount = $o.total * 0.05, orderId = $o.id }
 *   retract $o
 *   halt
 *   setFocus("group")
 *   if $o.total > 1000 { insert VIPOrder { orderId = $o.id } }
 *   for $item in $order.items { update $item { processed = 1 } }
 *   for $item in ($a, $b, $c) { update $item { processed = 1 } }
 *
 * Returns compiled actions that can be executed by RhsExecutor.
 */
class RhsParser {
public:
    /**
     * @brief Parse native RHS code into compiled actions.
     * @param rhs_code The RHS code string
     * @param bindings Variable bindings from LHS
     * @param error_out Error message if parsing fails
     * @return Vector of compiled actions, empty on failure
     */
    static std::vector<CompiledAction> parse(
        std::string const& rhs_code,
        std::map<std::string, int> const& bindings,
        std::string* error_out = nullptr);

private:
    struct Token {
        enum Type {
            TOK_UPDATE, TOK_INSERT, TOK_INSERT_LOGICAL, TOK_RETRACT, TOK_HALT, TOK_SET_FOCUS,
            TOK_INVOKE, TOK_IF, TOK_ELSE, TOK_FOR, TOK_IN,
            TOK_WHILE, TOK_SWITCH, TOK_CASE, TOK_DEFAULT, TOK_BREAK, TOK_CONTINUE,
            TOK_IDENTIFIER, TOK_VARIABLE, TOK_STRING, TOK_INTEGER, TOK_DOUBLE, TOK_TRUE, TOK_FALSE,
            TOK_LBRACE, TOK_RBRACE, TOK_LPAREN, TOK_RPAREN, TOK_COMMA, TOK_DOT, TOK_ASSIGN,
            TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_MOD, TOK_CARET, TOK_QUESTION, TOK_COLON,
            TOK_GT, TOK_LT, TOK_GE, TOK_LE, TOK_EQ, TOK_NE, TOK_AND, TOK_OR,
            TOK_END, TOK_ERROR
        };
        Type type;
        std::string text;
        size_t line;
        size_t column;
    };

    class Lexer {
    public:
        explicit Lexer(std::string const& input);
        Token next();
        Token peek();
    private:
        void skip_whitespace();
        Token scan_identifier();
        Token scan_number();
        Token scan_string();
        std::string input_;
        size_t pos_ = 0;
        size_t line_ = 1;
        size_t column_ = 1;
        Token peeked_;
        bool has_peeked_ = false;
    };

    class Parser {
    public:
        Parser(std::string const& input, std::map<std::string, int> const& bindings);
        std::vector<CompiledAction> parse();
        std::string const& error() const { return error_; }
    private:
        CompiledAction parse_action();
        CompiledAction parse_update();
        CompiledAction parse_insert();
        CompiledAction parse_insert_logical();
        CompiledAction parse_retract();
        CompiledAction parse_halt();
        CompiledAction parse_set_focus();
        CompiledAction parse_invoke();
        CompiledAction parse_if();
        CompiledAction parse_for();
        CompiledAction parse_while();
        CompiledAction parse_switch();
        CompiledAction parse_break();
        CompiledAction parse_continue();
        FieldAssignment parse_value();
        FieldAssignment parse_native_call_value();
        std::vector<FieldAssignment> parse_field_assignments();
        FieldAssignment parse_field_assignment();
        std::string parse_expression();
        std::string parse_ternary();
        std::string parse_or();
        std::string parse_and();
        std::string parse_comparison();
        std::string parse_additive();
        std::string parse_multiplicative();
        std::string parse_power();
        std::string parse_term();

        bool match(Token::Type type);
        bool check(Token::Type type);
        Token advance();
        Token consume(Token::Type type, std::string const& message);
        void error(std::string const& message);

        Lexer lexer_;
        std::map<std::string, int> const& bindings_;
        Token current_;
        std::string error_;
        bool had_error_ = false;
    };
};

} // namespace rulesforge

#endif // RHS_PARSER_HPP
