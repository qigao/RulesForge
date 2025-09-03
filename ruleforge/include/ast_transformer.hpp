#ifndef AST_TRANSFORMER_HPP
#define AST_TRANSFORMER_HPP

#include "drools_parser_state.hpp"

/**
 * @class AstTransformer
 * @brief Modifies the parsed AST in-place to expand complex logical constructs
 *        into simpler ones that the Rete network can build directly.
 */
class AstTransformer {
public:
    explicit AstTransformer(parser_state& state);

    // The main entry point to run all transformations.
    void transform();

private:
    // Recursively finds and expands 'forall' patterns in a list.
    void expand_foralls_in_list(std::vector<ParsedPattern>& patterns);

    parser_state& state_;
};

#endif   // AST_TRANSFORMER_HPP
