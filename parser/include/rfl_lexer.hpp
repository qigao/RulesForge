#ifndef RFL_LEXER_HPP
#define RFL_LEXER_HPP

#include "rfl_token.hpp"
#include <string>
#include <string_view>

class RflLexer {
public:
    RflLexer(std::string_view source, std::string const& file_name);

    RflToken next_token();
    void enter_rhs_mode();

    std::string const& file_name() const { return file_name_; }

private:
    RflToken scan_normal();
    RflToken scan_rhs();
    void skip_whitespace_and_comments();

    RflToken make_token(enum TokenType type, char const* start) const;

    std::string_view source_;
    std::string file_name_;
    char const* cursor_;
    char const* limit_;
    char const* marker_;
    char const* tok_;       // start of current token
    std::size_t line_;
    char const* line_start_; // pointer to start of current line
    bool rhs_mode_;
};

#endif // RFL_LEXER_HPP
