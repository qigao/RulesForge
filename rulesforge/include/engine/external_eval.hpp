#ifndef RULESFORGE_ENGINE_EXTERNAL_EVAL_HPP
#define RULESFORGE_ENGINE_EXTERNAL_EVAL_HPP

#include "core/value_types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace rulesforge {

enum class ExternalEvalArgumentKind {
    Variable,
    Literal,
    NumericExpression,
};

struct ExternalEvalArgument {
    ExternalEvalArgumentKind kind = ExternalEvalArgumentKind::Variable;
    std::string text;
    ConstraintValue literal = NilValue{};
};

struct ExternalEvalCall {
    std::string predicate_name;
    std::vector<ExternalEvalArgument> arguments;
};

bool parse_external_eval_helper_name(std::string const& raw_name, std::string& predicate_name);
bool split_external_eval_arg_text(std::string const& arg_text, std::vector<std::string>& out);
std::optional<ConstraintValue> parse_external_eval_literal(std::string const& token);
std::optional<ExternalEvalCall> parse_external_eval_call(std::string const& expression);

}

#endif
