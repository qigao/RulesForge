#include "drools_rete_defs.hpp"
#include "fmtlog.h"

#include <iostream>
#include <magic_enum/magic_enum.hpp>
#include <sstream>
#include <tao/pegtl/contrib/parse_tree.hpp>
#include <tao/pegtl/parse.hpp>
#include <tao/pegtl/string_input.hpp>

ArithExprValue clone_arith_expr_value(ArithExprValue const& v) {
    return std::visit(
        [](auto const& arg) -> ArithExprValue {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, double>) {
                return arg;
            } else if constexpr (std::is_same_v<T, std::string>) {
                return arg;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<ArithExprNode>>) {
                if (arg) {
                    return std::make_unique<ArithExprNode>(*arg);
                }
                return std::unique_ptr<ArithExprNode>(nullptr);
            }
            return double{0};  // Should never reach here
        },
        v);
}

ArithExprNode::ArithExprNode(ArithExprNode const& other)
    : op(other.op),
      left(clone_arith_expr_value(other.left)),
      right(clone_arith_expr_value(other.right)) {}

ArithExprNode& ArithExprNode::operator=(ArithExprNode const& other) {
    if (this != &other) {
        op = other.op;
        left = clone_arith_expr_value(other.left);
        right = clone_arith_expr_value(other.right);
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
      right_arith_expr(other.right_arith_expr)
{
    if (other.right_arith_ast.has_value()) {
        right_arith_ast = clone_arith_expr_value(*other.right_arith_ast);
    }
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
        if (other.right_arith_ast.has_value()) {
            right_arith_ast = clone_arith_expr_value(*other.right_arith_ast);
        } else {
            right_arith_ast.reset();
        }
    }
    return *this;
}

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
namespace {
    struct IndexAccess {
        bool is_integer;
        int64_t int_index;
        std::string str_index;
    };

    struct PathSegment {
        std::string name;
        bool null_safe;  // true if this segment was preceded by !.
        std::vector<IndexAccess> indices;  // index accesses like [0] or ["key"]
    };

    // Parse index access expressions from a segment (e.g., "items[0][1]" -> name="items", indices=[0,1])
    PathSegment parse_segment_with_indices(std::string const& segment, bool null_safe) {
        PathSegment result;
        result.null_safe = null_safe;

        size_t bracket_pos = segment.find('[');
        if (bracket_pos == std::string::npos) {
            result.name = segment;
            return result;
        }

        result.name = segment.substr(0, bracket_pos);
        size_t pos = bracket_pos;

        while (pos < segment.length() && segment[pos] == '[') {
            size_t close_pos = segment.find(']', pos);
            if (close_pos == std::string::npos) break;

            std::string index_str = segment.substr(pos + 1, close_pos - pos - 1);

            IndexAccess idx;
            // Check if it's a string index (starts with " or ')
            if (!index_str.empty() && (index_str[0] == '"' || index_str[0] == '\'')) {
                idx.is_integer = false;
                // Remove quotes
                idx.str_index = index_str.substr(1, index_str.length() - 2);
            } else {
                idx.is_integer = true;
                try {
                    idx.int_index = std::stoll(index_str);
                } catch (...) {
                    idx.int_index = 0;  // Default to 0 on parse error
                }
            }
            result.indices.push_back(idx);

            pos = close_pos + 1;
        }

        return result;
    }

    std::vector<PathSegment> parse_field_path(std::string const& path) {
        std::vector<PathSegment> segments;
        size_t pos = 0;
        bool next_null_safe = false;

        while (pos < path.length()) {
            // Find next separator (either !. or .)
            // But skip separators inside brackets
            size_t bracket_depth = 0;
            size_t sep_pos = std::string::npos;
            bool is_null_safe_sep = false;

            for (size_t i = pos; i < path.length(); ++i) {
                if (path[i] == '[') {
                    bracket_depth++;
                } else if (path[i] == ']') {
                    if (bracket_depth > 0) bracket_depth--;
                } else if (bracket_depth == 0) {
                    if (i + 1 < path.length() && path[i] == '!' && path[i + 1] == '.') {
                        sep_pos = i;
                        is_null_safe_sep = true;
                        break;
                    } else if (path[i] == '.' && (i == 0 || path[i - 1] != '!')) {
                        sep_pos = i;
                        is_null_safe_sep = false;
                        break;
                    }
                }
            }

            if (sep_pos == std::string::npos) {
                // No more separators, take the rest
                std::string segment = path.substr(pos);
                if (!segment.empty()) {
                    segments.push_back(parse_segment_with_indices(segment, next_null_safe));
                }
                break;
            }

            // Extract segment before separator
            std::string segment = path.substr(pos, sep_pos - pos);
            if (!segment.empty()) {
                segments.push_back(parse_segment_with_indices(segment, next_null_safe));
            }

            // Determine if next segment uses null-safe access
            if (is_null_safe_sep) {
                next_null_safe = true;
                pos = sep_pos + 2;  // Skip "!."
            } else {
                next_null_safe = false;
                pos = sep_pos + 1;  // Skip "."
            }
        }

        return segments;
    }

    // Apply index access to a ConstraintValue
    std::optional<ConstraintValue> apply_index(ConstraintValue const& val, IndexAccess const& idx, bool null_safe) {
        if (std::holds_alternative<FactList>(val)) {
            FactList const& fl = std::get<FactList>(val);
            if (idx.is_integer) {
                int64_t index = idx.int_index;
                // Handle negative indices (Python-style)
                if (index < 0) {
                    index = static_cast<int64_t>(fl.facts.size()) + index;
                }
                if (index < 0 || static_cast<size_t>(index) >= fl.facts.size()) {
                    return null_safe ? std::optional(ConstraintValue{NilValue{}}) : std::nullopt;
                }
                // For FactList, return the fact at index as a FactList containing single fact
                // (so further field access can traverse into it)
                FactList result;
                result.facts.push_back(fl.facts[static_cast<size_t>(index)]);
                return result;
            } else {
                // String key on FactList - look for fact with matching 'key' or 'name' field
                for (auto const& fact : fl.facts) {
                    if (!fact) continue;
                    auto key_field = fact->fields.find("key");
                    if (key_field != fact->fields.end() &&
                        std::holds_alternative<std::string>(key_field->second) &&
                        std::get<std::string>(key_field->second) == idx.str_index) {
                        FactList result;
                        result.facts.push_back(fact);
                        return result;
                    }
                }
                return null_safe ? std::optional(ConstraintValue{NilValue{}}) : std::nullopt;
            }
        } else if (std::holds_alternative<std::string>(val)) {
            // String indexing - return character at position
            std::string const& str = std::get<std::string>(val);
            if (idx.is_integer) {
                int64_t index = idx.int_index;
                if (index < 0) {
                    index = static_cast<int64_t>(str.length()) + index;
                }
                if (index < 0 || static_cast<size_t>(index) >= str.length()) {
                    return null_safe ? std::optional(ConstraintValue{NilValue{}}) : std::nullopt;
                }
                return std::string(1, str[static_cast<size_t>(index)]);
            }
        }
        return null_safe ? std::optional(ConstraintValue{NilValue{}}) : std::nullopt;
    }
}  // namespace

std::optional<ConstraintValue> Fact::get_field(std::string const& name) const {
    logd("Fact::get_field(this={}, id={}, name='{}')", (void*)this, this->id, name);

    // "this" is the special keyword to refer to the fact's identity (its internal ID).
    if (name == "this") { return this->id; }
    bool has_path_sep = name.find('.') != std::string::npos;
    bool has_index = name.find('[') != std::string::npos;

    if (has_path_sep || has_index) {
        auto segments = parse_field_path(name);

        if (!segments.empty()) {
            logd("  > Path traversal with {} segments", segments.size());

            Fact const* current_fact = this;
            std::optional<ConstraintValue> current_value;

            for (size_t i = 0; i < segments.size(); ++i) {
                auto const& seg = segments[i];
                logd("  > Segment {}: '{}' (null_safe={}, indices={})", i, seg.name, seg.null_safe, seg.indices.size());

                // Get the field from current fact
                if (!seg.name.empty()) {
                    auto it = current_fact->fields.find(seg.name);
                    if (it == current_fact->fields.end()) {
                        // Field not found
                        if (seg.null_safe) {
                            logd("  > Field '{}' not found, null-safe returning NilValue", seg.name);
                            return NilValue{};
                        }
                        logd("  > Field '{}' not found, returning nullopt", seg.name);
                        return std::nullopt;
                    }
                    current_value = it->second;
                }

                // Apply any index accesses
                for (auto const& idx : seg.indices) {
                    if (!current_value) {
                        return seg.null_safe ? std::optional(ConstraintValue{NilValue{}}) : std::nullopt;
                    }
                    auto indexed_result = apply_index(*current_value, idx, seg.null_safe);
                    if (!indexed_result) {
                        return std::nullopt;
                    }
                    current_value = *indexed_result;
                    logd("  > Applied index access, result type: {}", current_value->index());
                }

                // If this is the last segment, return the value
                if (i == segments.size() - 1) {
                    logd("  > Reached final segment, returning value");
                    // If final value is a FactList with single fact and we need a scalar,
                    // this will be handled by the comparison logic
                    return current_value;
                }

                // Need to traverse further - check if value is a FactList
                if (!current_value) {
                    return seg.null_safe ? std::optional(ConstraintValue{NilValue{}}) : std::nullopt;
                }

                if (std::holds_alternative<FactList>(*current_value)) {
                    FactList const& fl = std::get<FactList>(*current_value);
                    if (fl.facts.empty()) {
                        bool next_null_safe = (i + 1 < segments.size()) && segments[i + 1].null_safe;
                        if (seg.null_safe || next_null_safe) {
                            logd("  > FactList is empty, null-safe returning NilValue");
                            return NilValue{};
                        }
                        logd("  > FactList is empty, returning nullopt");
                        return std::nullopt;
                    }
                    // Use first fact for traversal
                    current_fact = fl.facts[0].get();
                    if (!current_fact) {
                        bool next_null_safe = (i + 1 < segments.size()) && segments[i + 1].null_safe;
                        if (seg.null_safe || next_null_safe) {
                            return NilValue{};
                        }
                        return std::nullopt;
                    }
                } else if (std::holds_alternative<NilValue>(*current_value)) {
                    // Current value is nil, check null-safety
                    bool next_null_safe = (i + 1 < segments.size()) && segments[i + 1].null_safe;
                    if (seg.null_safe || next_null_safe) {
                        logd("  > Value is nil, null-safe returning NilValue");
                        return NilValue{};
                    }
                    logd("  > Value is nil, returning nullopt");
                    return std::nullopt;
                } else {
                    // Cannot traverse into non-object value
                    logd("  > Cannot traverse into non-FactList value");
                    return std::nullopt;
                }
            }
        }
    }

    // Simple field lookup (original behavior)
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


