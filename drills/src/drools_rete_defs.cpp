#include "drools_rete_defs.hpp"
#include "pubcxx/logger.hpp"

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
    LOG_DEBUG("Fact::get_field(this={}, id={}, name='{}')", (void*)this, this->id, name);
    // "this" is the special keyword to refer to the fact's identity (its internal ID).
    if (name == "this") { return this->id; }
    // All other names, including "id", must be looked for exclusively in the fields map.
    auto it = fields.find(name);
    if (it != fields.end()) {
        LOG_DEBUG("  > Found key '{}' in fields map.", name);
        return it->second;
    }

    LOG_DEBUG("  > Did not find key '{}' in fields map.", name);
    if (LOG_LEVEL_PUBCXX_TRACE) {
        std::stringstream ss;
        ss << "  > Available fields in map: {";
        bool first = true;
        for (auto const& [key, val] : fields) {
            if (!first) ss << ", ";
            ss << "'" << key << "'";
            first = false;
        }
        ss << "}";
        LOG_DEBUG("{}", ss.str());
    }

    return std::nullopt;
}

Token::Token(std::shared_ptr<TokenWME const> w, PropagationType pt) : wme(std::move(w)), type(pt) {
    LOG_DEBUG("Token created with PropagationType: {}", magic_enum::enum_name(pt));
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
    LOG_DEBUG("ConstraintNode created with NodeType: {}", magic_enum::enum_name(t));
}

ConstraintNode::ConstraintNode() : type(NodeType::LEAF) {
    LOG_DEBUG("ConstraintNode created with default NodeType: {}", magic_enum::enum_name(NodeType::LEAF));
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

// --- NON-TEMPLATE JSON IMPLEMENTATIONS ---
void to_json(json& j, FactList const& p) { j = json{{"type", "FactList"}, {"size", p.facts.size()}}; }

void from_json(json const& j, FactList& p) { p.facts.clear(); }

void to_json(json& j, NilValue const& p) { j = nullptr; }

void from_json(json const& j, NilValue& p) {}

void to_json(json& j, ParsedConstraint const& p) {
    j = json::object();
    if (p.field_binding) j["field_binding"] = *p.field_binding;
    if (p.left_binding) j["left_binding"] = *p.left_binding;
    j["left_field"] = p.left_field;
    j["op"] = p.op;
    if (p.right_literal) j["right_literal"] = *p.right_literal;
    if (p.right_bound_field) j["right_bound_field"] = {p.right_bound_field->first, p.right_bound_field->second};
    if (p.right_value_list) j["right_value_list"] = *p.right_value_list;
    if (p.temporal_constraint) j["temporal_constraint"] = *p.temporal_constraint;
}

void from_json(json const& j, ParsedConstraint& p) {
    if (j.contains("field_binding")) p.field_binding = j["field_binding"].get<std::string>();
    if (j.contains("left_binding")) p.left_binding = j["left_binding"].get<std::string>();
    j.at("left_field").get_to(p.left_field);
    j.at("op").get_to(p.op);
    if (j.contains("right_literal")) j.at("right_literal").get_to(p.right_literal);
    if (j.contains("right_bound_field")) {
        auto pair_vec = j["right_bound_field"].get<std::vector<std::string>>();
        p.right_bound_field = {pair_vec[0], pair_vec[1]};
    }
    if (j.contains("right_value_list")) j.at("right_value_list").get_to(p.right_value_list);
    if (j.contains("temporal_constraint")) {
        p.temporal_constraint = j["temporal_constraint"].get<ParsedTemporalConstraint>();
    }
}

void to_json(json& j, ConstraintNode const& p) {
    j = json{{"type", p.type}, {"constraint", p.constraint}, {"children", p.children}};
}

void from_json(json const& j, ConstraintNode& p) {
    j.at("type").get_to(p.type);
    j.at("constraint").get_to(p.constraint);
    j.at("children").get_to(p.children);
}

void to_json(json& j, std::unique_ptr<ConstraintNode> const& p) { j = p ? json(*p) : nullptr; }

void from_json(json const& j, std::unique_ptr<ConstraintNode>& p) {
    p = j.is_null() ? nullptr : std::make_unique<ConstraintNode>(j.get<ConstraintNode>());
}

void to_json(json& j, ParsedAccumulate const& p) {
    j = json{{"function", p.function},
             {"field", p.field},
             {"source_pattern", p.source_pattern},
             {"accumulate_field_name", p.accumulate_field_name}};
}

void from_json(json const& j, ParsedAccumulate& p) {
    j.at("function").get_to(p.function);
    j.at("field").get_to(p.field);
    j.at("source_pattern").get_to(p.source_pattern);
    if (j.contains("accumulate_field_name")) j.at("accumulate_field_name").get_to(p.accumulate_field_name);
}

void to_json(json& j, ParsedQuery const& p) {
    j = json{{"name", p.name},
             {"patterns", p.patterns},
             {"parameter_types", p.parameter_types},
             {"parameter_count", p.parameter_count},
             {"source_package", p.source_package},
             {"source_imports", p.source_imports}};
}

void from_json(json const& j, ParsedQuery& p) {
    j.at("name").get_to(p.name);
    j.at("patterns").get_to(p.patterns);
    j.at("parameter_types").get_to(p.parameter_types);
    j.at("parameter_count").get_to(p.parameter_count);
    if (j.contains("source_package")) j.at("source_package").get_to(p.source_package);
    if (j.contains("source_imports")) j.at("source_imports").get_to(p.source_imports);
}

void to_json(json& j, ParsedPattern const& p) {
    j = json{{"type", p.type},
             {"binding", p.binding},
             {"fact_type", p.fact_type},
             {"constraint_root", p.constraint_root},
             {"nested_patterns", p.nested_patterns},
             {"eval_expression", p.eval_expression},
             {"forall_info", p.forall_info},
             {"source", p.source}};
}

void from_json(json const& j, ParsedPattern& p) {
    j.at("type").get_to(p.type);
    j.at("binding").get_to(p.binding);
    j.at("fact_type").get_to(p.fact_type);
    j.at("constraint_root").get_to(p.constraint_root);
    j.at("nested_patterns").get_to(p.nested_patterns);
    j.at("eval_expression").get_to(p.eval_expression);
    j.at("forall_info").get_to(p.forall_info);
    if (j.contains("source")) { j.at("source").get_to(p.source); }
}

void to_json(json& j, std::unique_ptr<ParsedPattern> const& p) { j = p ? json(*p) : nullptr; }

void from_json(json const& j, std::unique_ptr<ParsedPattern>& p) {
    p = j.is_null() ? nullptr : std::make_unique<ParsedPattern>(j.get<ParsedPattern>());
}

void to_json(json& j, ParsedRule const& p) {
    j = json{{"name", p.name},
             {"annotations", p.annotations},
             {"salience", p.salience},
             {"salience_explicitly_set", p.salience_explicitly_set},
             {"parent_rule_name", p.parent_rule_name},
             {"agenda_group", p.agenda_group},
             {"timer", p.timer},
             {"condition_groups", p.condition_groups},
             {"rhs_code", p.rhs_code},
             {"source_package", p.source_package},
             {"source_imports", p.source_imports}};
}

void from_json(json const& j, ParsedRule& p) {
    j.at("name").get_to(p.name);
    if (j.contains("annotations")) { j.at("annotations").get_to(p.annotations); }
    j.at("salience").get_to(p.salience);
    j.at("salience_explicitly_set").get_to(p.salience_explicitly_set);
    j.at("parent_rule_name").get_to(p.parent_rule_name);
    j.at("agenda_group").get_to(p.agenda_group);
    j.at("timer").get_to(p.timer);
    j.at("condition_groups").get_to(p.condition_groups);
    if (j.contains("rhs_code")) { j.at("rhs_code").get_to(p.rhs_code); }
    if (j.contains("source_package")) j.at("source_package").get_to(p.source_package);
    if (j.contains("source_imports")) j.at("source_imports").get_to(p.source_imports);
}

// // New function to generate Lua code for ParsedRule's RHS
// std::string generate_rule_rhs_lua_code(ParsedRule const& rule) {
//     if (rule.rhs_code.empty()) { return ""; }

//     try {
//         // Parse into a parse tree
//         auto root =
//             tao::pegtl::parse_tree::parse<lua_grammar::grammar>(tao::pegtl::string_input<>(rule.rhs_code,
//             "rhs_code"));

//         if (!root) {
//             LOG_ERROR("Failed to parse RHS code for rule '{}': Parse tree is null.", rule.name);
//             return "";
//         }

//         // Build AST from parse tree
//         LuaAstBuilder builder;   // Assuming default constructor
//         LuaAstRoot ast = builder.build(*root);
//         return generate_lua_code(ast);
//     } catch (tao::pegtl::parse_error const& e) {
//         LOG_ERROR("Failed to parse RHS code for rule '{}': {}", rule.name, e.what());
//         return "";   // Or rethrow, depending on desired error handling
//     }
// }
