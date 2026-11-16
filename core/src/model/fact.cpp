#include "model/fact.hpp"
#include <charconv>
#include <stdexcept>

// --- Implementation for Core Struct Methods ---
// IndexAccess and PathSegment are now in header

    // Parse index access expressions from a segment (e.g., "items[0][1]" -> name="items", indices=[0,1])
    PathSegment parse_segment_with_indices(std::string_view segment, bool null_safe) {
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
            if (close_pos == std::string_view::npos)
                throw std::invalid_argument("Unclosed field-path index");

            std::string_view index_str = segment.substr(pos + 1, close_pos - pos - 1);

            IndexAccess idx{};
            // Check if it's a string index (starts with " or ')
            if (!index_str.empty() && (index_str[0] == '"' || index_str[0] == '\'')) {
                idx.is_integer = false;
                if (index_str.size() < 2 || index_str.back() != index_str.front())
                    throw std::invalid_argument("Unclosed field-path string key");
                // Remove quotes
                idx.str_index = index_str.substr(1, index_str.length() - 2);
            } else {
                idx.is_integer = true;
                auto [end, status] = std::from_chars(
                    index_str.data(), index_str.data() + index_str.size(), idx.int_index);
                idx.valid = status == std::errc{} && end == index_str.data() + index_str.size();
            }
            result.indices.push_back(idx);

            pos = close_pos + 1;
        }
        if (pos != segment.size())
            throw std::invalid_argument("Unexpected text after field-path index");

        return result;
    }


// End of helpers (parse_segment_with_indices)

    std::vector<PathSegment> parse_field_path(std::string_view path) {
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
                std::string_view segment = path.substr(pos);
                if (!segment.empty()) {
                    segments.push_back(parse_segment_with_indices(segment, next_null_safe));
                }
                break;
            }

            std::string_view segment = path.substr(pos, sep_pos - pos);
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
        if (!idx.valid) {
            return std::nullopt;
        }
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
    ValueMap const* current_map = nullptr;
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

            // DataBind records use ValueMap, while native nested facts use
            // FactList. Copy the child before replacing its owning current value.
            std::optional<ConstraintValue> next;
            if (current_map) {
                auto it = current_map->entries.find(seg.name);
                if (it != current_map->entries.end()) next = it->second;
            } else {
                auto it = current_fact->fields.find(seg.name);
                if (it != current_fact->fields.end()) next = it->second;
            }
            if (!next) {
                if (seg.null_safe) return NilValue{};
                return std::nullopt;
            }
            current_value = std::move(next);
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
            current_map = nullptr;
            FactList const& fl = std::get<FactList>(*current_value);
            auto const& next_seg = segments[i + 1];
            if (next_seg.name == "size" || next_seg.name == "isEmpty") {
                continue;
            }
            if (fl.facts.empty()) {
                return make_typed_list();
            }
            if (fl.facts.size() != 1) {
                return std::nullopt;
            }
            current_fact = fl.facts[0];
            if (!current_fact) {
                 bool next_null_safe = (i + 1 < segments.size()) && segments[i + 1].null_safe;
                 if (seg.null_safe || next_null_safe) return NilValue{};
                 return std::nullopt;
            }
        } else if (auto const* map = std::get_if<std::shared_ptr<ValueMap>>(&*current_value)) {
            if (!*map) {
                bool next_null_safe = segments[i + 1].null_safe;
                return seg.null_safe || next_null_safe ? std::optional(ConstraintValue{NilValue{}}) : std::nullopt;
            }
            current_map = map->get();
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

std::optional<ConstraintValue> Fact::get_field(std::string_view name) const {
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
