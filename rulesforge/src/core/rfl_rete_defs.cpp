
#include "core/rfl_rete_defs.hpp"
#include "parser/expression_evaluator.hpp"
#include "core/logging_control.hpp"

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

// --- ConstraintValueCompare implementation ---
bool ConstraintValueCompare::operator()(ConstraintValue const& a, ConstraintValue const& b) const {
    // First compare by variant index
    if (a.index() != b.index()) {
        return a.index() < b.index();
    }
    // Same type, compare values
    return std::visit([&b, this](auto const& val_a) -> bool {
        using T = std::decay_t<decltype(val_a)>;
        auto const& val_b = std::get<T>(b);
        if constexpr (std::is_same_v<T, std::string>) {
            return val_a < val_b;
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return val_a < val_b;
        } else if constexpr (std::is_same_v<T, double>) {
            return val_a < val_b;
        } else if constexpr (std::is_same_v<T, FactList>) {
            return false;  // FactLists are not comparable
        } else if constexpr (std::is_same_v<T, NilValue>) {
            return false;  // All nils are equal
        } else if constexpr (std::is_same_v<T, std::shared_ptr<TypedList>>) {
            if (!val_a && !val_b) return false;
            if (!val_a) return true;
            if (!val_b) return false;
            return std::lexicographical_compare(
                val_a->values.begin(), val_a->values.end(),
                val_b->values.begin(), val_b->values.end(),
                *this);
        } else if constexpr (std::is_same_v<T, std::shared_ptr<ValueSet>>) {
            if (!val_a && !val_b) return false;
            if (!val_a) return true;
            if (!val_b) return false;
            return std::lexicographical_compare(
                val_a->values.begin(), val_a->values.end(),
                val_b->values.begin(), val_b->values.end(),
                *this);
        } else if constexpr (std::is_same_v<T, std::shared_ptr<ValueMap>>) {
            if (!val_a && !val_b) return false;
            if (!val_a) return true;
            if (!val_b) return false;
            // Compare maps by iterating through entries
            auto it_a = val_a->entries.begin();
            auto it_b = val_b->entries.begin();
            while (it_a != val_a->entries.end() && it_b != val_b->entries.end()) {
                if ((*this)(it_a->first, it_b->first)) return true;
                if ((*this)(it_b->first, it_a->first)) return false;
                if ((*this)(it_a->second, it_b->second)) return true;
                if ((*this)(it_b->second, it_a->second)) return false;
                ++it_a;
                ++it_b;
            }
            return it_a == val_a->entries.end() && it_b != val_b->entries.end();
        }
        return false;
    }, a);
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
      compiled_expr(other.compiled_expr),  // shared_ptr shallow copy
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
        compiled_expr = other.compiled_expr;  // shared_ptr shallow copy
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

    // Check compiled expressions - compare by expression string for sharing
    bool this_has_expr = (compiled_expr != nullptr);
    bool other_has_expr = (other.compiled_expr != nullptr);
    if (this_has_expr != other_has_expr) return false;
    if (this_has_expr && compiled_expr->expression_string() != other.compiled_expr->expression_string()) {
        return false;
    }

    return true;
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
            } else if constexpr (std::is_same_v<T, std::shared_ptr<TypedList>>) {
                if (!arg) return "[]";
                std::string result = "[";
                for (size_t i = 0; i < arg->values.size(); ++i) {
                    if (i > 0) result += ", ";
                    result += to_string(arg->values[i]);
                }
                return result + "]";
            } else if constexpr (std::is_same_v<T, std::shared_ptr<ValueSet>>) {
                if (!arg) return "{}";
                std::string result = "{";
                bool first = true;
                for (auto const& v : arg->values) {
                    if (!first) result += ", ";
                    result += to_string(v);
                    first = false;
                }
                return result + "}";
            } else if constexpr (std::is_same_v<T, std::shared_ptr<ValueMap>>) {
                if (!arg) return "{}";
                std::string result = "{";
                bool first = true;
                for (auto const& [k, v] : arg->entries) {
                    if (!first) result += ", ";
                    result += to_string(k) + ": " + to_string(v);
                    first = false;
                }
                return result + "}";
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

// --- Implementation for Hasher ---
std::size_t ConstraintValueHasher::operator()(ConstraintValue const& v) const {
    return std::visit(
        [](auto const& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, FactList> || std::is_same_v<T, NilValue>) {
                return (size_t)0;
            } else if constexpr (std::is_same_v<T, std::shared_ptr<TypedList>>) {
                if (!arg) return (size_t)0;
                size_t h = 0;
                for (auto const& val : arg->values) {
                    h ^= ConstraintValueHasher{}(val) + 0x9e3779b9 + (h << 6) + (h >> 2);
                }
                return h;
            } else if constexpr (std::is_same_v<T, std::shared_ptr<ValueSet>>) {
                if (!arg) return (size_t)0;
                size_t h = 0;
                for (auto const& val : arg->values) {
                    h ^= ConstraintValueHasher{}(val);
                }
                return h;
            } else if constexpr (std::is_same_v<T, std::shared_ptr<ValueMap>>) {
                if (!arg) return (size_t)0;
                size_t h = 0;
                for (auto const& [k, val] : arg->entries) {
                    h ^= ConstraintValueHasher{}(k) ^ ConstraintValueHasher{}(val);
                }
                return h;
            } else {
                return std::hash<T>{}(arg);
            }
        },
        v);
}

// --- Implementation for Core Struct Methods ---
// IndexAccess and PathSegment are now in header

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


// End of helpers (parse_segment_with_indices)

    std::vector<PathSegment> parse_field_path(std::string const& path) {
        std::vector<PathSegment> segments;
        size_t pos = 0;
        bool next_null_safe = false;

        while (pos < path.length()) {
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
                std::string segment = path.substr(pos);
                if (!segment.empty()) {
                    segments.push_back(parse_segment_with_indices(segment, next_null_safe));
                }
                break;
            }

            std::string segment = path.substr(pos, sep_pos - pos);
            if (!segment.empty()) {
                segments.push_back(parse_segment_with_indices(segment, next_null_safe));
            }

            if (is_null_safe_sep) {
                next_null_safe = true;
                pos = sep_pos + 2;
            } else {
                next_null_safe = false;
                pos = sep_pos + 1;
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
                if (index < 0) {
                    index = static_cast<int64_t>(fl.facts.size()) + index;
                }
                if (index < 0 || static_cast<size_t>(index) >= fl.facts.size()) {
                    return null_safe ? std::optional(ConstraintValue{NilValue{}}) : std::nullopt;
                }
                FactList result;
                result.facts.push_back(fl.facts[static_cast<size_t>(index)]);
                return result;
            } else {
                for (auto const& fact : fl.facts) {
                    if (!fact) continue;
                    auto key_field = fact->fields.find(std::string_view("key"));
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
        } else if (std::holds_alternative<std::shared_ptr<TypedList>>(val)) {
            auto const& tl_ptr = std::get<std::shared_ptr<TypedList>>(val);
            if (!tl_ptr) return null_safe ? std::optional(ConstraintValue{NilValue{}}) : std::nullopt;
            if (idx.is_integer) {
                int64_t index = idx.int_index;
                if (index < 0) {
                    index = static_cast<int64_t>(tl_ptr->values.size()) + index;
                }
                if (index < 0 || static_cast<size_t>(index) >= tl_ptr->values.size()) {
                    return null_safe ? std::optional(ConstraintValue{NilValue{}}) : std::nullopt;
                }
                return tl_ptr->values[static_cast<size_t>(index)];
            }
            return null_safe ? std::optional(ConstraintValue{NilValue{}}) : std::nullopt;
        } else if (std::holds_alternative<std::shared_ptr<ValueMap>>(val)) {
            auto const& vm_ptr = std::get<std::shared_ptr<ValueMap>>(val);
            if (!vm_ptr) return null_safe ? std::optional(ConstraintValue{NilValue{}}) : std::nullopt;
            ConstraintValue key;
            if (idx.is_integer) {
                key = idx.int_index;
            } else {
                key = idx.str_index;
            }
            auto it = vm_ptr->entries.find(key);
            if (it != vm_ptr->entries.end()) {
                return it->second;
            }
            return null_safe ? std::optional(ConstraintValue{NilValue{}}) : std::nullopt;
        } else if (std::holds_alternative<std::string>(val)) {
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

// Helper: get size of a collection value
static std::optional<int64_t> get_collection_size(ConstraintValue const& val) {
    if (std::holds_alternative<FactList>(val)) {
        return static_cast<int64_t>(std::get<FactList>(val).facts.size());
    } else if (std::holds_alternative<std::shared_ptr<TypedList>>(val)) {
        auto const& ptr = std::get<std::shared_ptr<TypedList>>(val);
        return ptr ? static_cast<int64_t>(ptr->values.size()) : 0;
    } else if (std::holds_alternative<std::shared_ptr<ValueSet>>(val)) {
        auto const& ptr = std::get<std::shared_ptr<ValueSet>>(val);
        return ptr ? static_cast<int64_t>(ptr->values.size()) : 0;
    } else if (std::holds_alternative<std::shared_ptr<ValueMap>>(val)) {
        auto const& ptr = std::get<std::shared_ptr<ValueMap>>(val);
        return ptr ? static_cast<int64_t>(ptr->entries.size()) : 0;
    } else if (std::holds_alternative<std::string>(val)) {
        return static_cast<int64_t>(std::get<std::string>(val).length());
    }
    return std::nullopt;
}

std::optional<ConstraintValue> Fact::get_field(std::vector<PathSegment> const& segments) const {
    if (segments.empty()) return std::nullopt;

    Fact const* current_fact = this;
    std::optional<ConstraintValue> current_value;

    for (size_t i = 0; i < segments.size(); ++i) {
        auto const& seg = segments[i];

        if (!seg.name.empty()) {
            // Handle pseudo-fields for collections: .size, .isEmpty
            if (current_value.has_value()) {
                if (seg.name == "size") {
                    auto sz = get_collection_size(*current_value);
                    if (sz) return *sz;
                    return seg.null_safe ? std::optional(ConstraintValue{NilValue{}}) : std::nullopt;
                }
                if (seg.name == "isEmpty") {
                    auto sz = get_collection_size(*current_value);
                    if (sz) return static_cast<int64_t>(*sz == 0 ? 1 : 0);
                    return seg.null_safe ? std::optional(ConstraintValue{NilValue{}}) : std::nullopt;
                }
            }

            auto it = current_fact->fields.find(seg.name);
            if (it == current_fact->fields.end()) {
                if (seg.null_safe) return NilValue{};
                return std::nullopt;
            }
            current_value = it->second;
        }

        for (auto const& idx : seg.indices) {
            if (!current_value) return seg.null_safe ? std::optional(ConstraintValue{NilValue{}}) : std::nullopt;
            auto indexed_result = apply_index(*current_value, idx, seg.null_safe);
            if (!indexed_result) return std::nullopt;
            current_value = *indexed_result;
        }

        if (i == segments.size() - 1) return current_value;

        if (!current_value) return seg.null_safe ? std::optional(ConstraintValue{NilValue{}}) : std::nullopt;

        if (std::holds_alternative<FactList>(*current_value)) {
            FactList const& fl = std::get<FactList>(*current_value);
            if (fl.facts.empty()) {
                bool next_null_safe = (i + 1 < segments.size()) && segments[i + 1].null_safe;
                if (seg.null_safe || next_null_safe) return NilValue{};
                return std::nullopt;
            }
            current_fact = fl.facts[0];
            if (!current_fact) {
                 bool next_null_safe = (i + 1 < segments.size()) && segments[i + 1].null_safe;
                 if (seg.null_safe || next_null_safe) return NilValue{};
                 return std::nullopt;
            }
        } else if (std::holds_alternative<NilValue>(*current_value)) {
            bool next_null_safe = (i + 1 < segments.size()) && segments[i + 1].null_safe;
            if (seg.null_safe || next_null_safe) return NilValue{};
            return std::nullopt;
        } else {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<ConstraintValue> Fact::get_field(std::string const& name) const {
   // logd("Fact::get_field(this={}, id={}, name='{}')", (void*)this, this->id, name);

    // "this" is the special keyword to refer to the fact's identity (its internal ID).
    if (name == "this") { return this->id; }

    // Fast path: simple field names (no path separators) go straight to map lookup.
    // This avoids two O(n) string scans for the 99% common case.
    bool has_path_sep = false;
    bool has_index = false;
    for (char c : name) {
        if (c == '.') { has_path_sep = true; break; }
        if (c == '[') { has_index = true; break; }
    }
    if (!has_path_sep && !has_index) {
        auto it = fields.find(name);
        if (it != fields.end()) return it->second;
        return std::nullopt;
    }

    if (has_path_sep || has_index) {
        auto segments = parse_field_path(name);
        return get_field(segments);
    }

    // Simple field lookup (original behavior)
    auto it = fields.find(name);
    if (it != fields.end()) {
        return it->second;
    }

    return std::nullopt;
}

Token::Token(TokenWME const* w, PropagationType pt) : wme(w), type(pt) {}

Fact const* Token::get_fact() const { return wme ? wme->fact : nullptr; }

int Token::get_depth() const { return wme ? wme->depth : 0; }

std::vector<Fact*> Token::get_facts() const {
    if (!wme) return {};
    std::vector<Fact*> all_facts;
    // WME depth is 1-based size of chain (excluding dummy root if depth 0)
    // Wait, dummy has depth 0. Real WMEs start at 1.
    // So reserve depth.
    all_facts.resize(wme->depth);

    auto curr = wme;
    int idx = wme->depth - 1;
    while (curr && curr->depth > 0 && idx >= 0) {
        if (curr->fact) {
            all_facts[idx] = const_cast<Fact*>(curr->fact);
        }
        curr = curr->parent;
        idx--;
    }
    return all_facts;
}

Fact* Token::get_fact_at_depth(int d) const {
    if (!wme) return nullptr;
    // Target index is d. Current WME is at index (depth - 1).
    // Steps to walk up = (current_depth - 1) - d.
    int current_index = wme->depth - 1;
    if (d < 0 || d > current_index) return nullptr;

    int steps = current_index - d;
    auto curr = wme;
    while (steps > 0 && curr) {
        curr = curr->parent;
        steps--;
    }

    return (curr && curr->fact) ? const_cast<Fact*>(curr->fact) : nullptr;
}

bool TokenWME::operator==(TokenWME const& other) const {
    if (this == &other) return true;
    if (depth != other.depth) return false;
    bool fact_match = (fact && other.fact) ? (fact->id == other.fact->id) : (fact == other.fact);
    if (!fact_match) return false;
    // Use pointer comparison for parent to avoid recursive deep compare
    return parent == other.parent;
}

bool Activation::operator<(Activation const& other) const { return rule->salience < other.rule->salience; }



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

ParsedAccumulate::ParsedAccumulate() {}

ParsedAccumulate::ParsedAccumulate(ParsedAccumulate const& other) :
    function(other.function), field(other.field), accumulate_field_name(other.accumulate_field_name),
    compiled_expr(other.compiled_expr),  // shared_ptr shallow copy
    inline_binding_to_field(other.inline_binding_to_field) {
    if (other.source_pattern) { source_pattern = std::make_unique<ParsedPattern>(*other.source_pattern); }
}

ParsedAccumulate& ParsedAccumulate::operator=(ParsedAccumulate const& other) {
    if (this == &other) return *this;
    function = other.function;
    field = other.field;
    accumulate_field_name = other.accumulate_field_name;
    compiled_expr = other.compiled_expr;  // shared_ptr shallow copy
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
