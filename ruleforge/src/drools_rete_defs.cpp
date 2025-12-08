#include "drools_rete_defs.hpp"
#include "fmtlog.h"

#include <iostream>
#include <magic_enum/magic_enum.hpp>
#include <sstream>
#include <tao/pegtl/contrib/parse_tree.hpp>
#include <tao/pegtl/parse.hpp>
#include <tao/pegtl/string_input.hpp>

// --- Implementation for to_string free function ---
std::string to_string(ConstraintValue const& val) {
    return std::visit(
        [](auto&& arg) -> std::string {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::string>) {
                return "\"" + arg + "\"";
            } else if constexpr (std::is_same_v<T, int64_t>) {
                return std::to_string(arg);
            } else if constexpr (std::is_same_v<T, double>) {
                return std::to_string(arg);
            } else if constexpr (std::is_same_v<T, FactList>) {
                return "[FactList]";
            } else if constexpr (std::is_same_v<T, NilValue>) {
                return "nil";
            }
            return "UNKNOWN";
        },
        val);
}

// --- Implementation for constraint_to_string ---
std::string constraint_to_string(ParsedConstraint const& c) {
    std::ostringstream oss;
    if (c.temporal_constraint) {
        auto const& tc = *c.temporal_constraint;
        oss << "temporal " << tc.lhs_field << " " << tc.op << " "
            << tc.rhs_binding_and_field.first << "." << tc.rhs_binding_and_field.second;
        if (tc.op == "within") { oss << " " << tc.window_ms << "ms"; }
        return oss.str();
    }

    if (c.left_binding) {
        oss << *c.left_binding;
    } else {
        oss << "fact";
    }
    oss << "." << c.left_field << " " << c.op << " ";

    if (c.right_bound_field) {
        oss << c.right_bound_field->first << "." << c.right_bound_field->second;
    } else if (c.right_literal) {
        oss << to_string(*c.right_literal);
    }
    return oss.str();
}

// --- Implementation for Hasher ---
std::size_t ConstraintValueHasher::operator()(ConstraintValue const& v) const {
    return std::visit(
        [](auto const& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, FactList> || std::is_same_v<T, NilValue>) {
                return (size_t)0;
            } else {
                return std::hash<T>{}(arg);
            }
        },
        v);
}

// --- Implementation for Core Struct Methods ---
std::optional<ConstraintValue> Fact::get_field(std::string const& name) const {
    logd("Fact::get_field(this={}, id={}, name='{}')", (void*)this, this->id, name);
    // "this" is the special keyword to refer to the fact's identity (its internal ID).
    if (name == "this") { return this->id; }
    // All other names, including "id", must be looked for exclusively in the fields map.
    auto it = fields.find(name);
    if (it != fields.end()) {
        logd("  > Found key '{}' in fields map.", name);
        return it->second;
    }

    logd("  > Did not find key '{}' in fields map.", name);
    if constexpr(FMTLOG_ACTIVE_LEVEL <= FMTLOG_LEVEL_DBG) {
        std::stringstream ss;
        ss << "  > Available fields in map: {";
        bool first = true;
        for (auto const& [key, val] : fields) {
            if (!first) ss << ", ";
            ss << "'" << key << "'";
            first = false;
        }
        ss << "}";
        logd("{}", ss.str());
    }

    return std::nullopt;
}

Token::Token(std::shared_ptr<TokenWME const> w, PropagationType pt) : wme(std::move(w)), type(pt) {
    logd("Token created with PropagationType: {}", magic_enum::enum_name(pt));
}

std::shared_ptr<Fact const> Token::get_fact() const { return wme ? wme->fact : nullptr; }

int Token::get_depth() const { return wme ? wme->depth : 0; }

std::vector<std::shared_ptr<Fact>> Token::get_facts() const {
    if (!wme) return {};
    std::vector<std::shared_ptr<Fact>> all_facts;
    all_facts.reserve(wme->depth);
    for (TokenWME const* current_wme = wme.get(); current_wme != nullptr && current_wme->fact != nullptr;
         current_wme = current_wme->parent.get()) {
        all_facts.push_back(std::const_pointer_cast<Fact>(current_wme->fact));
    }
    std::reverse(all_facts.begin(), all_facts.end());
    return all_facts;
}

std::shared_ptr<Fact> Token::get_fact_at_depth(int d) const {
    if (!wme) return nullptr;
    int current_depth = wme->depth - 1;
    for (TokenWME const* current_wme = wme.get(); current_wme != nullptr && current_wme->fact != nullptr;
         current_wme = current_wme->parent.get()) {
        if (current_depth == d) { return std::const_pointer_cast<Fact>(current_wme->fact); }
        current_depth--;
    }
    return nullptr;
}

bool TokenWME::operator==(TokenWME const& other) const {
    if (this == &other) return true;
    if (depth != other.depth) return false;
    bool fact_match = (fact && other.fact) ? (fact->id == other.fact->id) : (fact == other.fact);
    if (!fact_match) return false;
    bool parent_match = (parent && other.parent) ? (*parent == *other.parent) : (parent == other.parent);
    return parent_match;
}

bool Activation::operator<(Activation const& other) const { return rule->salience < other.rule->salience; }

bool ScheduledActivation::operator>(ScheduledActivation const& other) const {
    return activation_time > other.activation_time;
}

bool ScheduledExpiration::operator>(ScheduledExpiration const& other) const {
    return expiration_time > other.expiration_time;
}

// --- Implementations for custom constructors / operators ---
ConstraintNode::ConstraintNode(NodeType t) : type(t) {
    logd("ConstraintNode created with NodeType: {}", magic_enum::enum_name(t));
}

ConstraintNode::ConstraintNode() : type(NodeType::LEAF) {
    logd("ConstraintNode created with default NodeType: {}", magic_enum::enum_name(NodeType::LEAF));
}

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

ParsedAccumulate::ParsedAccumulate() {}

ParsedAccumulate::ParsedAccumulate(ParsedAccumulate const& other) :
    function(other.function), field(other.field), accumulate_field_name(other.accumulate_field_name) {
    if (other.source_pattern) { source_pattern = std::make_unique<ParsedPattern>(*other.source_pattern); }
}

ParsedAccumulate& ParsedAccumulate::operator=(ParsedAccumulate const& other) {
    if (this == &other) return *this;
    function = other.function;
    field = other.field;
    accumulate_field_name = other.accumulate_field_name;
    source_pattern = other.source_pattern ? std::make_unique<ParsedPattern>(*other.source_pattern) : nullptr;
    return *this;
}

ParsedQuery::ParsedQuery() : pos(0, 0, 0, "") {}

ParsedPattern::ParsedPattern() : pos(0, 0, 0, "") {}

ParsedRule::ParsedRule() : pos(0, 0, 0, "") {}

ParsedPattern::ParsedPattern(ParsedPattern const& other) :
    type(other.type), pos(other.pos), binding(other.binding), fact_type(other.fact_type),
    nested_patterns(other.nested_patterns), eval_expression(other.eval_expression), forall_info(other.forall_info),
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
    source = other.source;
    return *this;
}
