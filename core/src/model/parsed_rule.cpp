#include "model/parsed_rule.hpp"

ParsedAccumulate::ParsedAccumulate() {}

ParsedAccumulate::ParsedAccumulate(ParsedAccumulate const& other) :
    function(other.function), field(other.field), accumulate_field_name(other.accumulate_field_name),
    uses_runtime_value_expression(other.uses_runtime_value_expression),
    value_program(other.value_program),
    inline_binding_to_field(other.inline_binding_to_field) {
    if (other.source_pattern) { source_pattern = std::make_unique<ParsedPattern>(*other.source_pattern); }
}

ParsedAccumulate& ParsedAccumulate::operator=(ParsedAccumulate const& other) {
    if (this == &other) return *this;
    function = other.function;
    field = other.field;
    accumulate_field_name = other.accumulate_field_name;
    uses_runtime_value_expression = other.uses_runtime_value_expression;
    value_program = other.value_program;
    inline_binding_to_field = other.inline_binding_to_field;
    source_pattern = other.source_pattern ? std::make_unique<ParsedPattern>(*other.source_pattern) : nullptr;
    return *this;
}

ParsedQuery::ParsedQuery() = default;

ParsedPattern::ParsedPattern() = default;

ParsedRule::ParsedRule() = default;

ParsedPattern::ParsedPattern(ParsedPattern const& other) :
    type(other.type), pos(other.pos), binding(other.binding), fact_type(other.fact_type),
    nested_patterns(other.nested_patterns), eval_expression(other.eval_expression), forall_info(other.forall_info),
    window_info(other.window_info),
    source(other.source) {
    constraint_root = other.constraint_root ? std::make_unique<ConstraintNode>(*other.constraint_root) : nullptr;
}

ParsedPattern& ParsedPattern::operator=(ParsedPattern const& other) {
    if (this == &other) return *this;
    type = other.type;
    pos = other.pos;
    binding = other.binding;
    fact_type = other.fact_type;
    constraint_root = other.constraint_root ? std::make_unique<ConstraintNode>(*other.constraint_root) : nullptr;
    nested_patterns = other.nested_patterns;
    eval_expression = other.eval_expression;
    forall_info = other.forall_info;
    window_info = other.window_info;
    source = other.source;
    return *this;
}
