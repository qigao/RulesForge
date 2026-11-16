#include "model/constraint_types.hpp"
#include <sstream>

// --- TypeParameter copy operations ---
TypeParameter::TypeParameter(TypeParameter const& other)
    : base_type(other.base_type), custom_type(other.custom_type) {
    if (other.nested) {
        nested = std::make_unique<TypeParameter>(*other.nested);
    }
}

TypeParameter& TypeParameter::operator=(TypeParameter const& other) {
    if (this != &other) {
        base_type = other.base_type;
        custom_type = other.custom_type;
        nested = other.nested ? std::make_unique<TypeParameter>(*other.nested) : nullptr;
    }
    return *this;
}

ParsedConstraint::ParsedConstraint(ParsedConstraint const& other)
    : field_binding(other.field_binding),
      left_binding(other.left_binding),
      left_field(other.left_field),
      op(other.op),
      right_literal(other.right_literal),
      right_bound_field(other.right_bound_field),
      right_value_list(other.right_value_list),
      temporal_constraint(other.temporal_constraint),
      right_arith_expr(other.right_arith_expr),
      right_arith_program(other.right_arith_program),
      cached_left_field_path(other.cached_left_field_path),
      cached_right_field_path(other.cached_right_field_path)
{
}

ParsedConstraint& ParsedConstraint::operator=(ParsedConstraint const& other) {
    if (this != &other) {
        field_binding = other.field_binding;
        left_binding = other.left_binding;
        left_field = other.left_field;
        op = other.op;
        right_literal = other.right_literal;
        right_bound_field = other.right_bound_field;
        right_value_list = other.right_value_list;
        temporal_constraint = other.temporal_constraint;
        right_arith_expr = other.right_arith_expr;
        right_arith_program = other.right_arith_program;
        cached_left_field_path = other.cached_left_field_path;
        cached_right_field_path = other.cached_right_field_path;
    }
    return *this;
}

// Added equality operator
bool ParsedConstraint::operator==(ParsedConstraint const& other) const {
    if (op != other.op) return false;
    if (left_field != other.left_field) return false;
    if (field_binding != other.field_binding) return false;
    if (left_binding != other.left_binding) return false;
    if (right_literal != other.right_literal) return false;
    if (right_bound_field != other.right_bound_field) return false;
    if (right_value_list != other.right_value_list) return false;
    if (right_arith_expr != other.right_arith_expr) return false;

    // Check temporal constraints
    bool this_has_temp = temporal_constraint.has_value();
    bool other_has_temp = other.temporal_constraint.has_value();
    if (this_has_temp != other_has_temp) return false;
    if (this_has_temp) {
        auto const& t1 = *temporal_constraint;
        auto const& t2 = *other.temporal_constraint;
        if (t1.op != t2.op) return false;
        if (t1.lhs_field != t2.lhs_field) return false;
        if (t1.rhs_binding_and_field != t2.rhs_binding_and_field) return false;
        if (t1.window_ms != t2.window_ms) return false;
    }

    return true;
}

// --- Implementation for constraint_to_string ---
std::string constraint_to_string(ParsedConstraint const& c) {
    std::ostringstream oss;
    if (c.temporal_constraint) {
        auto const& tc = *c.temporal_constraint;
        oss << "temporal " << tc.lhs_field << " " << temporal_op_str(tc.op) << " "
            << tc.rhs_binding_and_field.first << "." << tc.rhs_binding_and_field.second;
        if (tc.op == TemporalOp::Within) { oss << " " << tc.window_ms << "ms"; }
        return oss.str();
    }

    if (c.left_binding) {
        oss << *c.left_binding;
    } else {
        oss << "fact";
    }
    oss << "." << c.left_field << " " << compare_op_str(c.op) << " ";

    if (c.right_bound_field) {
        oss << c.right_bound_field->first << "." << c.right_bound_field->second;
    } else if (c.right_literal) {
        oss << to_string(*c.right_literal);
    }
    return oss.str();
}

// --- Implementations for custom constructors / operators ---
ConstraintNode::ConstraintNode(NodeType t) : type(t) {}

ConstraintNode::ConstraintNode() : type(NodeType::LEAF) {}

ConstraintNode::ConstraintNode(ConstraintNode const& other) : type(other.type), constraint(other.constraint) {
    for (auto const& child : other.children) { children.push_back(std::make_unique<ConstraintNode>(*child)); }
}

ConstraintNode& ConstraintNode::operator=(ConstraintNode const& other) {
    if (this != &other) {
        type = other.type;
        constraint = other.constraint;
        children.clear();
        for (auto const& child : other.children) { children.push_back(std::make_unique<ConstraintNode>(*child)); }
    }
    return *this;
}
