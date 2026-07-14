#include "core/logging_control.hpp"

#include "engine/external_eval.hpp"
#include "engine/knowledge_base.hpp"
#include "rete/beta_builder.hpp"
#include "rete/compiled_network.hpp"
#include "rete/rete_node.hpp"

#include <algorithm>
#include <regex>
#include <unordered_map>

using namespace rulesforge;

namespace {
struct QueryOutputBinding {
    std::string binding;
    std::string fact_type;
};

bool query_output_pattern_adds_fact(ParsedPattern const& pattern) {
    return pattern.type == PatternType::STANDARD
        || std::holds_alternative<ParsedAccumulate>(pattern.source)
        || std::holds_alternative<ParsedUnnest>(pattern.source);
}

ParsedQuery const* find_query_by_name(std::vector<ParsedQuery> const& queries, std::string const& name) {
    auto it = std::find_if(
        queries.begin(),
        queries.end(),
        [&](ParsedQuery const& query) { return query.name == name; });
    return it == queries.end() ? nullptr : &*it;
}

std::vector<QueryOutputBinding> collect_query_output_bindings(ParsedQuery const& query) {
    std::vector<QueryOutputBinding> outputs;
    for (std::size_t index = static_cast<std::size_t>(query.parameter_count);
         index < query.patterns.size();
         ++index) {
        auto const& pattern = query.patterns[index];
        if (query_output_pattern_adds_fact(pattern) && !pattern.binding.empty()) {
            outputs.push_back(QueryOutputBinding{pattern.binding, pattern.fact_type});
        }
    }
    return outputs;
}

void collect_leaf_constraints(ConstraintNode const* node, std::vector<ParsedConstraint>& out) {
    if (!node) return;
    if (node->type == NodeType::LEAF) {
        ParsedConstraint c = node->constraint;
        // Inline field-binding metadata is for parser symbol resolution; it should not force
        // a token lookup during runtime comparison.
        c.left_binding = std::nullopt;
        out.push_back(std::move(c));
        return;
    }
    for (auto const& child : node->children) {
        collect_leaf_constraints(child.get(), out);
    }
}

struct FieldInfo {
    FieldType type = FT_Unknown;
    std::vector<TypeParameter> type_params;
};

using FieldTypeIndex = std::unordered_map<std::string, std::unordered_map<std::string, FieldInfo>>;

FieldTypeIndex build_field_type_index(std::vector<ParsedDeclaration> const& declarations) {
    FieldTypeIndex result;
    for (auto const& declaration : declarations) {
        auto& fields = result[declaration.type_name];
        for (auto const& field : declaration.fields) {
            fields[field.name] = FieldInfo{field.type, field.type_params};
        }
    }
    return result;
}

FieldInfo const* lookup_field_info(FieldTypeIndex const& field_types,
                                   std::string const& fact_type,
                                   std::string const& field_name) {
    auto fact_it = field_types.find(fact_type);
    if (fact_it == field_types.end()) {
        return nullptr;
    }
    auto field_it = fact_it->second.find(field_name);
    if (field_it == fact_it->second.end()) {
        return nullptr;
    }
    return &field_it->second;
}

FieldInfo const* lookup_first_path_field_info(FieldTypeIndex const& field_types,
                                              std::string const& fact_type,
                                              std::vector<PathSegment> const& path) {
    if (path.empty() || path.front().name.empty()) {
        return nullptr;
    }
    return lookup_field_info(field_types, fact_type, path.front().name);
}

std::string root_field_name(std::string const& field_name) {
    auto const end = field_name.find_first_of(".[!");
    if (end == std::string::npos) {
        return field_name;
    }
    return field_name.substr(0, end);
}

std::optional<FieldType> lookup_field_type(FieldTypeIndex const& field_types,
                                           std::string const& fact_type,
                                           std::string const& field_name) {
    auto const* field_info = lookup_field_info(field_types, fact_type, field_name);
    if (field_info == nullptr) {
        return std::nullopt;
    }
    return field_info->type;
}

std::optional<FieldType> lookup_bound_field_type(
    FieldTypeIndex const& field_types,
    std::map<std::string, std::string> const& binding_fact_types,
    std::pair<std::string, std::string> const& bound_field) {
    if (bound_field.second == "this") {
        return FT_Long;
    }
    auto it = binding_fact_types.find(bound_field.first);
    if (it == binding_fact_types.end()) {
        return std::nullopt;
    }
    return lookup_field_type(field_types, it->second, bound_field.second);
}

FieldInfo const* lookup_bound_field_info(
    FieldTypeIndex const& field_types,
    std::map<std::string, std::string> const& binding_fact_types,
    std::pair<std::string, std::string> const& bound_field) {
    if (bound_field.second == "this") {
        return nullptr;
    }
    auto it = binding_fact_types.find(bound_field.first);
    if (it == binding_fact_types.end()) {
        return nullptr;
    }
    return lookup_field_info(field_types, it->second, bound_field.second);
}

FieldInfo const* lookup_bound_first_path_field_info(
    FieldTypeIndex const& field_types,
    std::map<std::string, std::string> const& binding_fact_types,
    std::pair<std::string, std::string> const& bound_field,
    std::vector<PathSegment> const& path) {
    auto it = binding_fact_types.find(bound_field.first);
    if (it == binding_fact_types.end()) {
        return nullptr;
    }
    if (path.empty()) {
        auto const root = root_field_name(bound_field.second);
        if (!root.empty() && root != bound_field.second) {
            return lookup_field_info(field_types, it->second, root);
        }
    }
    return lookup_first_path_field_info(field_types, it->second, path);
}

bool is_declared_string_field(std::optional<FieldType> type) {
    return type.has_value() && *type == FT_String;
}

bool is_declared_numeric_field(std::optional<FieldType> type) {
    if (!type.has_value()) return false;
    switch (*type) {
        case FT_Int:
        case FT_Long:
        case FT_Double:
        case FT_Float:
        case FT_Number:
            return true;
        default:
            return false;
    }
}

bool is_declared_runtime_scalar_field(std::optional<FieldType> type) {
    if (!type.has_value()) return false;
    switch (*type) {
        case FT_String:
        case FT_Int:
        case FT_Long:
        case FT_Double:
        case FT_Float:
        case FT_Number:
        case FT_Boolean:
        case FT_Uuid:
            return true;
        default:
            return false;
    }
}

bool is_declared_map_field(std::optional<FieldType> type) {
    return type.has_value() && *type == FT_Map;
}

bool is_declared_collection_field(std::optional<FieldType> type) {
    return type.has_value() && (*type == FT_List || *type == FT_Set);
}

bool is_scalar_type_parameter(TypeParameter const& type) {
    if (type.nested) {
        return false;
    }
    switch (type.base_type) {
        case FT_String:
        case FT_Int:
        case FT_Long:
        case FT_Double:
        case FT_Float:
        case FT_Number:
        case FT_Boolean:
        case FT_Uuid:
            return true;
        default:
            return false;
    }
}

bool is_declared_scalar_collection_field(FieldInfo const* field_info) {
    if (field_info == nullptr || !is_declared_collection_field(field_info->type)) {
        return false;
    }
    return field_info->type_params.size() == 1 && is_scalar_type_parameter(field_info->type_params.front());
}

bool is_declared_fact_list_field(FieldInfo const* field_info) {
    return field_info != nullptr
        && field_info->type == FT_Object
        && field_info->type_params.size() == 1
        && field_info->type_params.front().base_type == FT_Object
        && field_info->type_params.front().custom_type == "FactList";
}

bool is_fact_list_projection(FieldInfo const* first_field_info,
                             std::vector<PathSegment> const& path,
                             std::string const& field_name) {
    return is_declared_fact_list_field(first_field_info)
        && (path.size() > 1 || field_name.find('.') != std::string::npos);
}

enum class RuntimeCompareFieldKind {
    None,
    Scalar,
    List,
    Set,
    Map,
    FactList,
};

RuntimeCompareFieldKind runtime_compare_field_kind(FieldInfo const* field_info, std::optional<FieldType> type) {
    if (is_declared_runtime_scalar_field(type)) {
        return RuntimeCompareFieldKind::Scalar;
    }
    if (field_info != nullptr) {
        if (is_declared_fact_list_field(field_info)) {
            return RuntimeCompareFieldKind::FactList;
        }
        type = field_info->type;
    }
    if (!type.has_value()) {
        return RuntimeCompareFieldKind::None;
    }
    switch (*type) {
        case FT_List:
            return RuntimeCompareFieldKind::List;
        case FT_Set:
            return RuntimeCompareFieldKind::Set;
        case FT_Map:
            return RuntimeCompareFieldKind::Map;
        default:
            return RuntimeCompareFieldKind::None;
    }
}

bool is_runtime_compare_value_supported(ConstraintValue const& value) {
    return std::holds_alternative<int64_t>(value)
        || std::holds_alternative<double>(value)
        || std::holds_alternative<std::string>(value)
        || std::holds_alternative<turbo_uuid_t>(value)
        || std::holds_alternative<NilValue>(value);
}

bool is_runtime_compare_literal_supported(ConstraintValue const& value, RuntimeCompareFieldKind field_kind) {
    if (std::holds_alternative<NilValue>(value)) {
        return true;
    }
    switch (field_kind) {
        case RuntimeCompareFieldKind::Scalar:
            return is_runtime_compare_value_supported(value);
        case RuntimeCompareFieldKind::List:
            return std::holds_alternative<std::shared_ptr<TypedList>>(value);
        case RuntimeCompareFieldKind::Set:
            return std::holds_alternative<std::shared_ptr<ValueSet>>(value);
        case RuntimeCompareFieldKind::Map:
            return std::holds_alternative<std::shared_ptr<ValueMap>>(value);
        case RuntimeCompareFieldKind::FactList:
            return std::holds_alternative<FactList>(value);
        case RuntimeCompareFieldKind::None:
            return false;
    }
    return false;
}

bool is_runtime_map_key_supported(ConstraintValue const& value) {
    return std::holds_alternative<int64_t>(value)
        || std::holds_alternative<double>(value)
        || std::holds_alternative<std::string>(value)
        || std::holds_alternative<turbo_uuid_t>(value)
        || std::holds_alternative<NilValue>(value)
        || std::holds_alternative<FactList>(value)
        || std::holds_alternative<std::shared_ptr<TypedList>>(value)
        || std::holds_alternative<std::shared_ptr<ValueSet>>(value)
        || std::holds_alternative<std::shared_ptr<ValueMap>>(value);
}

bool is_runtime_value_list_supported(std::vector<ConstraintValue> const& values) {
    return !values.empty()
        && std::all_of(values.begin(), values.end(), [](ConstraintValue const& value) {
            return is_runtime_compare_value_supported(value);
        });
}

bool supported_compare_constraint(ParsedConstraint const& constraint,
                                  FieldInfo const* left_field_info,
                                  std::optional<FieldType> left_field_type,
                                  FieldTypeIndex const& field_types,
                                  std::map<std::string, std::string> const& binding_fact_types) {
    if (constraint.op != CompareOp::EQ && constraint.op != CompareOp::NE && constraint.op != CompareOp::GT
        && constraint.op != CompareOp::LT && constraint.op != CompareOp::GE && constraint.op != CompareOp::LE) {
        return false;
    }
    auto const left_kind = runtime_compare_field_kind(left_field_info, left_field_type);
    if (left_kind == RuntimeCompareFieldKind::None) {
        return false;
    }
    bool const equality_op = constraint.op == CompareOp::EQ || constraint.op == CompareOp::NE;
    if (constraint.right_literal) {
        if (!equality_op && left_kind != RuntimeCompareFieldKind::Scalar) {
            return false;
        }
        if (std::holds_alternative<NilValue>(*constraint.right_literal) && !equality_op) {
            return false;
        }
        return is_runtime_compare_literal_supported(*constraint.right_literal, left_kind);
    }
    if (constraint.right_bound_field) {
        auto const right_type = lookup_bound_field_type(field_types, binding_fact_types, *constraint.right_bound_field);
        auto const* right_field_info = lookup_bound_field_info(
            field_types,
            binding_fact_types,
            *constraint.right_bound_field);
        auto const right_kind = runtime_compare_field_kind(right_field_info, right_type);
        if (left_kind == RuntimeCompareFieldKind::Scalar) {
            return right_kind == RuntimeCompareFieldKind::Scalar;
        }
        return equality_op && left_kind == right_kind;
    }
    return left_kind == RuntimeCompareFieldKind::Scalar;
}

bool is_declared_runtime_map_key_field(FieldInfo const* field_info) {
    if (field_info == nullptr) {
        return false;
    }
    if (is_declared_runtime_scalar_field(field_info->type)) {
        return true;
    }
    return field_info->type == FT_List
        || field_info->type == FT_Set
        || field_info->type == FT_Map
        || is_declared_fact_list_field(field_info);
}

bool supported_string_rhs(ParsedConstraint const& constraint,
                          FieldTypeIndex const& field_types,
                          std::map<std::string, std::string> const& binding_fact_types) {
    if (constraint.right_literal) {
        if (constraint.op == CompareOp::LengthIs) {
            return std::holds_alternative<int64_t>(*constraint.right_literal)
                || std::holds_alternative<double>(*constraint.right_literal);
        }
        if (!std::holds_alternative<std::string>(*constraint.right_literal)) {
            return false;
        }
        if (constraint.op == CompareOp::Matches || constraint.op == CompareOp::NotMatches) {
            try {
                std::regex re(std::get<std::string>(*constraint.right_literal));
                (void)re;
            } catch (std::regex_error const&) {
                return false;
            }
        }
        return true;
    }
    if (!constraint.right_bound_field) {
        return false;
    }
    auto const right_type = lookup_bound_field_type(field_types, binding_fact_types, *constraint.right_bound_field);
    if (constraint.op == CompareOp::LengthIs) {
        return is_declared_numeric_field(right_type);
    }
    return is_declared_string_field(right_type);
}

bool supported_collection_constraint(ParsedConstraint const& constraint,
                                     FieldInfo const* left_field_info,
                                     FieldTypeIndex const& field_types,
                                     std::map<std::string, std::string> const& binding_fact_types) {
    if (constraint.op == CompareOp::Contains || constraint.op == CompareOp::NotContains) {
        if (is_declared_fact_list_field(left_field_info)) {
            if (is_fact_list_projection(left_field_info, constraint.cached_left_field_path, constraint.left_field)) {
                if (constraint.right_literal) {
                    return is_runtime_compare_value_supported(*constraint.right_literal);
                }
                if (constraint.right_bound_field) {
                    auto const right_type = lookup_bound_field_type(
                        field_types,
                        binding_fact_types,
                        *constraint.right_bound_field);
                    return is_declared_runtime_scalar_field(right_type);
                }
                return false;
            }
            if (constraint.right_literal) {
                return std::holds_alternative<std::string>(*constraint.right_literal);
            }
            if (constraint.right_bound_field) {
                auto const right_type = lookup_bound_field_type(
                    field_types,
                    binding_fact_types,
                    *constraint.right_bound_field);
                return is_declared_string_field(right_type);
            }
            return false;
        }
        if (!is_declared_scalar_collection_field(left_field_info)) {
            return false;
        }
        if (constraint.right_literal && is_runtime_compare_value_supported(*constraint.right_literal)) {
            return true;
        }
        if (constraint.right_bound_field) {
            auto const right_type = lookup_bound_field_type(
                field_types,
                binding_fact_types,
                *constraint.right_bound_field);
            return is_declared_runtime_scalar_field(right_type);
        }
        return false;
    }

    if (constraint.op == CompareOp::MemberOf || constraint.op == CompareOp::NotMemberOf) {
        if (is_declared_string_field(left_field_info ? std::optional<FieldType>{left_field_info->type}
                                                     : std::nullopt)
            && constraint.right_bound_field) {
            auto const* right_field_info = lookup_bound_field_info(
                field_types,
                binding_fact_types,
                *constraint.right_bound_field);
            if (is_declared_fact_list_field(right_field_info)) {
                return true;
            }
        }
        if (!is_declared_runtime_scalar_field(left_field_info ? std::optional<FieldType>{left_field_info->type}
                                                         : std::nullopt)
            || !constraint.right_bound_field) {
            return false;
        }
        auto const* right_field_info = lookup_bound_field_info(
            field_types,
            binding_fact_types,
            *constraint.right_bound_field);
        if (right_field_info == nullptr) {
            right_field_info = lookup_bound_first_path_field_info(
                field_types,
                binding_fact_types,
                *constraint.right_bound_field,
                constraint.cached_right_field_path);
            if (is_fact_list_projection(right_field_info,
                                        constraint.cached_right_field_path,
                                        constraint.right_bound_field->second)) {
                return true;
            }
        }
        return is_declared_scalar_collection_field(right_field_info);
    }

    return false;
}

bool supported_map_key_constraint(ParsedConstraint const& constraint,
                                  FieldInfo const* left_field_info,
                                  FieldTypeIndex const& field_types,
                                  std::map<std::string, std::string> const& binding_fact_types) {
    if ((constraint.op != CompareOp::ContainsKey && constraint.op != CompareOp::NotContainsKey)
        || !is_declared_map_field(left_field_info ? std::optional<FieldType>{left_field_info->type}
                                                 : std::nullopt)) {
        return false;
    }
    if (constraint.right_literal && is_runtime_map_key_supported(*constraint.right_literal)) {
        return true;
    }
    if (constraint.right_bound_field) {
        auto const* right_field_info = lookup_bound_field_info(
            field_types,
            binding_fact_types,
            *constraint.right_bound_field);
        return is_declared_runtime_map_key_field(right_field_info);
    }
    return false;
}

std::optional<rulesforge::RuntimePredicateRef>
build_runtime_predicate_for_constraint(
    KnowledgeBase const& kb,
    ParsedConstraint const& constraint,
    FieldTypeIndex const& field_types,
    std::map<std::string, std::string> const& binding_fact_types,
    FieldInfo const* left_field_info,
    std::optional<FieldType> left_field_type,
    std::optional<FieldType> right_field_type = std::nullopt) {
    (void)kb;
    (void)right_field_type;
    if (constraint.temporal_constraint) {
        return rulesforge::RuntimePredicateRef{
            rulesforge::RuntimePredicateKind::Temporal,
            0,
            CompareOp::None,
            constraint.temporal_constraint->op,
            constraint.temporal_constraint->window_ms};
    }

    if (constraint.op == CompareOp::In || constraint.op == CompareOp::NotIn) {
        if (!constraint.right_value_list) {
            return std::nullopt;
        }
        if (!is_runtime_value_list_supported(*constraint.right_value_list)) {
            return std::nullopt;
        }
        return rulesforge::RuntimePredicateRef{
            rulesforge::RuntimePredicateKind::CollectionContains,
            0,
            constraint.op == CompareOp::In ? CompareOp::MemberOf : CompareOp::NotMemberOf};
    }

    if (constraint.op == CompareOp::Matches || constraint.op == CompareOp::NotMatches) {
        if (!is_declared_string_field(left_field_type)
            || !supported_string_rhs(constraint, field_types, binding_fact_types)) {
            return std::nullopt;
        }
        return rulesforge::RuntimePredicateRef{
            rulesforge::RuntimePredicateKind::StringMatches,
            0,
            constraint.op};
    }
    if (constraint.op == CompareOp::StartsWith || constraint.op == CompareOp::EndsWith) {
        if (!is_declared_string_field(left_field_type)
            || !supported_string_rhs(constraint, field_types, binding_fact_types)) {
            return std::nullopt;
        }
        return rulesforge::RuntimePredicateRef{
            rulesforge::RuntimePredicateKind::StringAffix,
            0,
            constraint.op};
    }
    if (constraint.op == CompareOp::LengthIs) {
        if (!is_declared_string_field(left_field_type)
            || !supported_string_rhs(constraint, field_types, binding_fact_types)) {
            return std::nullopt;
        }
        return rulesforge::RuntimePredicateRef{rulesforge::RuntimePredicateKind::StringLengthIs};
    }
    if (constraint.op == CompareOp::ContainsKey || constraint.op == CompareOp::NotContainsKey) {
        if (!supported_map_key_constraint(constraint, left_field_info, field_types, binding_fact_types)) {
            return std::nullopt;
        }
        return rulesforge::RuntimePredicateRef{
            rulesforge::RuntimePredicateKind::MapContainsKey,
            0,
            constraint.op};
    }
    if (constraint.op == CompareOp::Contains || constraint.op == CompareOp::NotContains) {
        if (is_declared_string_field(left_field_type)) {
            if (!supported_string_rhs(constraint, field_types, binding_fact_types)) {
                return std::nullopt;
            }
            return rulesforge::RuntimePredicateRef{
                rulesforge::RuntimePredicateKind::StringContains,
                0,
                constraint.op};
        }
        if (!supported_collection_constraint(constraint, left_field_info, field_types, binding_fact_types)) {
            return std::nullopt;
        }
        return rulesforge::RuntimePredicateRef{
            rulesforge::RuntimePredicateKind::CollectionContains,
            0,
            constraint.op};
    }
    if (constraint.op == CompareOp::MemberOf || constraint.op == CompareOp::NotMemberOf) {
        if (!supported_collection_constraint(constraint, left_field_info, field_types, binding_fact_types)) {
            return std::nullopt;
        }
        return rulesforge::RuntimePredicateRef{
            rulesforge::RuntimePredicateKind::CollectionContains,
            0,
            constraint.op};
    }

    if (constraint.right_arith_expr) {
        return std::nullopt;
    }

    if (constraint.right_literal && is_declared_numeric_field(left_field_type)
        && (std::holds_alternative<int64_t>(*constraint.right_literal)
            || std::holds_alternative<double>(*constraint.right_literal))) {
        return std::nullopt;
    }

    if (supported_compare_constraint(
            constraint,
            left_field_info,
            left_field_type,
            field_types,
            binding_fact_types)) {
        return std::nullopt;
    }

    return std::nullopt;
}

std::vector<std::optional<rulesforge::RuntimePredicateRef>>
build_runtime_predicates(KnowledgeBase const& kb,
                         FieldTypeIndex const& field_types,
                         std::map<std::string, std::string> const& binding_fact_types,
                         std::string const& fact_type,
                         std::vector<ParsedConstraint> const& constraints) {
    std::vector<std::optional<rulesforge::RuntimePredicateRef>> result;
    result.reserve(constraints.size());
    for (auto const& constraint : constraints) {
        FieldInfo const* left_field_info = nullptr;
        std::optional<FieldType> left_type;
        if (constraint.left_binding) {
            auto it = binding_fact_types.find(*constraint.left_binding);
            if (it != binding_fact_types.end()) {
                left_field_info = lookup_field_info(field_types, it->second, constraint.left_field);
                if (left_field_info == nullptr) {
                    left_field_info = lookup_first_path_field_info(
                        field_types,
                        it->second,
                        constraint.cached_left_field_path);
                }
                if (left_field_info == nullptr) {
                    auto const dot_pos = constraint.left_field.find('.');
                    if (dot_pos != std::string::npos) {
                        left_field_info = lookup_field_info(
                            field_types,
                            it->second,
                            constraint.left_field.substr(0, dot_pos));
                    }
                }
                if (left_field_info != nullptr) {
                    left_type = left_field_info->type;
                }
            }
        } else if (!fact_type.empty()) {
            left_field_info = lookup_field_info(field_types, fact_type, constraint.left_field);
            if (left_field_info == nullptr) {
                left_field_info = lookup_first_path_field_info(
                    field_types,
                    fact_type,
                    constraint.cached_left_field_path);
            }
            if (left_field_info == nullptr) {
                auto const dot_pos = constraint.left_field.find('.');
                if (dot_pos != std::string::npos) {
                    left_field_info = lookup_field_info(
                        field_types,
                        fact_type,
                        constraint.left_field.substr(0, dot_pos));
                }
            }
            if (left_field_info != nullptr) {
                left_type = left_field_info->type;
            }
        }

        std::optional<FieldType> right_type;
        if (constraint.right_bound_field) {
            right_type = lookup_bound_field_type(field_types, binding_fact_types, *constraint.right_bound_field);
        }

        auto predicate = build_runtime_predicate_for_constraint(
            kb,
            constraint,
            field_types,
            binding_fact_types,
            left_field_info,
            left_type,
            right_type);
        result.push_back(std::move(predicate));
    }
    return result;
}
} // namespace

BetaNetworkBuilder::BetaNetworkBuilder(CompiledNetwork& network, KnowledgeBase const& kb,
                                       std::vector<ParsedPattern> const& patterns,
                                       bool is_query, int param_count) :
    network_(network), kb_(kb), patterns_(patterns), is_query_build_(is_query), parameter_count_(param_count),
    first_beta_node_in_chain(nullptr), last_node_(nullptr) {}

std::map<std::string, int> const& BetaNetworkBuilder::get_bindings() const { return binding_to_idx_; }

std::shared_ptr<ReteNode> BetaNetworkBuilder::build() {
    logd("BetaNetworkBuilder::build starting. is_query: {}, param_count: {}", is_query_build_, parameter_count_);
    last_node_ = nullptr;
    binding_to_idx_.clear();
    binding_to_fact_type_.clear();
    inline_binding_to_field_.clear();
    first_beta_node_in_chain = nullptr;
    int pattern_depth = 0;

    for (size_t i = 0; i < patterns_.size(); ++i) {
        ParsedPattern& pattern = patterns_[i];

        if (is_query_build_ && i < parameter_count_) {
            logd("  -> Processing query parameter pattern {}/{} at depth {}", i, parameter_count_, pattern_depth);
            if (!pattern.binding.empty()) {
                binding_to_idx_[pattern.binding] = pattern_depth;
                if (!pattern.fact_type.empty()) {
                    binding_to_fact_type_[pattern.binding] = pattern.fact_type;
                }
            }
            pattern_depth++;
            continue;
        }

        logd("  -> Processing pattern {} at depth {}", i, pattern_depth);
        bool adds_fact_to_token =
            pattern.type == PatternType::STANDARD ||
            std::holds_alternative<ParsedAccumulate>(pattern.source) ||
            std::holds_alternative<ParsedUnnest>(pattern.source);
        if (!pattern.binding.empty()) {
            if (!adds_fact_to_token) {
                throw std::runtime_error("pattern binding is only supported for fact-producing patterns");
            }
            binding_to_idx_[pattern.binding] = pattern_depth;
        }
        register_pattern_fact_type(pattern);

        if (pattern.constraint_root) {
            collect_inline_bindings(pattern.constraint_root.get(), pattern_depth, pattern.fact_type);
        }

        std::shared_ptr<ReteNode> current_node = create_node_for_pattern(pattern, pattern_depth);

        if (last_node_ == nullptr && current_node) {
            first_beta_node_in_chain = current_node;
            logd("  -> Set first beta node in chain to ID {}", current_node->id);
        }

        if (current_node) { last_node_ = current_node; }

        if (auto* query_call = std::get_if<ParsedQueryCall>(&pattern.source)) {
            auto const* query = find_query_by_name(kb_.get_parser_state().parsed_queries, query_call->query_name);
            if (query != nullptr) {
                auto outputs = collect_query_output_bindings(*query);
                for (auto const& output : outputs) {
                    binding_to_idx_[output.binding] = pattern_depth;
                    if (!output.fact_type.empty()) {
                        binding_to_fact_type_[output.binding] = output.fact_type;
                    }
                    logd("    -> Query call projects binding '{}' at depth {}", output.binding, pattern_depth);
                    ++pattern_depth;
                }
            }
        }

        if (adds_fact_to_token) {
            logd("    -> Pattern adds fact to token, incrementing depth to {}", pattern_depth + 1);
            pattern_depth++;
        }
    }

    logd("BetaNetworkBuilder::build finished. Last node ID: {}", last_node_ ? last_node_->id : -1);
    return last_node_;
}

std::vector<std::shared_ptr<ReteNode>>
BetaNetworkBuilder::build_alpha_chain(ConstraintNode const* node,
                                      std::string const& fact_type,
                                      std::vector<std::shared_ptr<ReteNode>> parent_tails) {
    if (!node || node->children.empty()) { return parent_tails; }

    auto const field_types = build_field_type_index(kb_.get_parser_state().parsed_declarations);
    std::vector<std::shared_ptr<ReteNode>> current_tails = parent_tails;
    for (auto const& constraint_leaf : node->children) {
        auto runtime_predicates = build_runtime_predicates(
            kb_,
            field_types,
            binding_to_fact_type_,
            fact_type,
            {constraint_leaf->constraint});
        auto alpha_node = network_.find_or_create_alpha(
            current_tails[0],
            constraint_leaf->constraint,
            runtime_predicates.empty() ? std::nullopt : runtime_predicates.front());
        current_tails = {alpha_node};
    }

    return current_tails;
}

void BetaNetworkBuilder::register_pattern_fact_type(ParsedPattern const& pattern) {
    if (!pattern.binding.empty() && !pattern.fact_type.empty()) {
        binding_to_fact_type_[pattern.binding] = pattern.fact_type;
    }
}

void BetaNetworkBuilder::collect_inline_bindings(ConstraintNode const* node, int depth, std::string const& fact_type) {
    if (!node) return;
    if (node->type == NodeType::LEAF && node->constraint.field_binding) {
        binding_to_idx_[*node->constraint.field_binding] = depth;
        inline_binding_to_field_[*node->constraint.field_binding] = node->constraint.left_field;
        if (!fact_type.empty()) {
            binding_to_fact_type_[*node->constraint.field_binding] = fact_type;
        }
    }
    for (auto const& child : node->children) { collect_inline_bindings(child.get(), depth, fact_type); }
}

void BetaNetworkBuilder::normalize_inline_bound_fields(std::vector<ParsedConstraint>& constraints) const {
    auto should_replace_field = [](std::string const& field, std::string const& binding) {
        if (field.empty() || field == "this") {
            return true;
        }
        if (field == binding) {
            return true;
        }
        return !binding.empty() && binding.front() == '$' && field == binding.substr(1);
    };

    for (auto& constraint : constraints) {
        if (constraint.left_binding) {
            auto field_it = inline_binding_to_field_.find(*constraint.left_binding);
            if (field_it != inline_binding_to_field_.end()
                && should_replace_field(constraint.left_field, *constraint.left_binding)) {
                constraint.left_field = field_it->second;
                constraint.cached_left_field_path.clear();
            }
        }
        if (constraint.right_bound_field) {
            auto field_it = inline_binding_to_field_.find(constraint.right_bound_field->first);
            if (field_it != inline_binding_to_field_.end()
                && should_replace_field(
                    constraint.right_bound_field->second,
                    constraint.right_bound_field->first)) {
                constraint.right_bound_field->second = field_it->second;
                constraint.cached_right_field_path.clear();
            }
        }
    }
}

std::shared_ptr<ReteNode> BetaNetworkBuilder::create_node_for_pattern(ParsedPattern& pattern, int& pattern_depth) {
    logd("Entering BetaNetworkBuilder::create_node_for_pattern for pattern type: {}, fact_type: {}",
              ENUM_NAME(pattern.type), pattern.fact_type);

    if (pattern.type == PatternType::EVAL) {
        logd("Creating EvalNode");
        return create_eval_node(pattern);
    }
    if (auto* query_call = std::get_if<ParsedQueryCall>(&pattern.source)) {
        return create_query_call_node(pattern, *query_call);
    }
    if (std::holds_alternative<ParsedUnnest>(pattern.source)) {
        auto& un_source = std::get<ParsedUnnest>(pattern.source);
        return create_unnest_node(pattern, un_source);
    }

    std::vector<ParsedConstraint> join_constraints;
    std::vector<std::shared_ptr<ReteNode>> alpha_tails;

    ParsedPattern* pattern_for_alpha = nullptr;
    if (pattern.type == PatternType::STANDARD) {
        pattern_for_alpha = &pattern;
    } else if (pattern.type == PatternType::NOT || pattern.type == PatternType::EXISTS) {
        if (!pattern.nested_patterns.empty()) { pattern_for_alpha = &pattern.nested_patterns.front(); }
    }

    if (auto* acc_source_ptr = std::get_if<ParsedAccumulate>(&pattern.source)) {
        if (acc_source_ptr->source_pattern) { pattern_for_alpha = acc_source_ptr->source_pattern.get(); }
    }

    if (pattern_for_alpha && !pattern_for_alpha->fact_type.empty()) {
        std::string const* entry_point_name = std::get_if<std::string>(&pattern_for_alpha->source);

        std::shared_ptr<ReteNode> entry;
        if (entry_point_name && !entry_point_name->empty()) {
            auto& named_entry = network_.named_entry_points[*entry_point_name][pattern_for_alpha->fact_type];
            if (!named_entry) {
                named_entry = network_.create_node<EntryPointNode>();
                logd("  -> Created new named EntryPointNode (ID {}) for type '{}' in entry-point '{}'",
                     named_entry->id, pattern_for_alpha->fact_type, *entry_point_name);
            }
            entry = named_entry;
        } else {
            auto& default_entry = network_.alpha_entry_points[pattern_for_alpha->fact_type];
            if (!default_entry) {
                default_entry = network_.create_node<EntryPointNode>();
                logd("  -> Created new EntryPointNode (ID {}) for type '{}'", default_entry->id, pattern_for_alpha->fact_type);
            }
            entry = default_entry;
        }
        auto alpha_root = kb_.partition_and_get_alpha_root(*pattern_for_alpha, join_constraints);
        normalize_inline_bound_fields(join_constraints);
        alpha_tails = build_alpha_chain(alpha_root.get(), pattern_for_alpha->fact_type, {entry});

        if (pattern_for_alpha->window_info.has_value()) {
            std::vector<std::shared_ptr<ReteNode>> new_tails;
            for (auto& tail : alpha_tails) {
                auto window_node = network_.create_node<WindowNode>(*pattern_for_alpha->window_info);
                tail->add_child(window_node);
                new_tails.push_back(window_node);
            }
            alpha_tails = new_tails;
            logd("  -> Attached WindowNode to {} alpha tail(s)", new_tails.size());
        }
    }

    if (std::holds_alternative<ParsedAccumulate>(pattern.source)) {
        auto& acc_source = std::get<ParsedAccumulate>(pattern.source);
        return create_accumulate_node(pattern, acc_source, alpha_tails, join_constraints);
    }
    std::string const join_fact_type = pattern_for_alpha ? pattern_for_alpha->fact_type : pattern.fact_type;
    if (pattern.type == PatternType::NOT) { return create_negative_node(pattern, join_fact_type, alpha_tails, join_constraints); }
    if (pattern.type == PatternType::EXISTS) { return create_existential_node(pattern, join_fact_type, alpha_tails, join_constraints); }

    return create_standard_node(pattern_depth, join_fact_type, alpha_tails, join_constraints);
}

std::shared_ptr<ReteNode>
BetaNetworkBuilder::create_standard_node(int pattern_depth,
                                         std::string const& fact_type,
                                         std::vector<std::shared_ptr<ReteNode>> const& alpha_tails,
                                         std::vector<ParsedConstraint> const& join_constraints) {

    std::vector<std::shared_ptr<ReteNode>> parents = alpha_tails;
    if (last_node_) parents.push_back(last_node_);

    std::shared_ptr<BaseJoinNode> join_node;
    auto const field_types = build_field_type_index(kb_.get_parser_state().parsed_declarations);
    auto runtime_predicates = build_runtime_predicates(
        kb_,
        field_types,
        binding_to_fact_type_,
        fact_type,
        join_constraints);

    if (!last_node_ && !is_query_build_) {
        // Initial node logic - always CrossProduct
        detail::BetaCacheKey key;
        key.kind = NodeKind::CrossProductJoin;
        key.constraints = join_constraints;
        key.bindings = binding_to_idx_;
        key.eval_scalar_fields = inline_binding_to_field_;

        logd("  -> Creating/Sharing initial CrossProductJoinNode with {} join constraints", join_constraints.size());
        join_node = std::static_pointer_cast<BaseJoinNode>(
            network_.find_or_create_beta_node<CrossProductJoinNode>(parents, std::move(key),
                join_constraints, binding_to_idx_, inline_binding_to_field_, runtime_predicates)
        );
        return join_node;
    }

    std::optional<std::pair<std::string, int>> left_hash_info;
    std::optional<std::string> right_hash_field;
    for (auto const& join : join_constraints) {
        if (join.op == CompareOp::EQ && join.right_bound_field) {
            if (auto it_bind = binding_to_idx_.find(join.right_bound_field->first); it_bind != binding_to_idx_.end()) {
                right_hash_field = join.left_field;
                left_hash_info = {{join.right_bound_field->second, it_bind->second}};
                break;
            }
        }
    }

    if (left_hash_info && right_hash_field) {
        logd("  -> Creating/Sharing HashedJoinNode with {} join constraints, left_hash: {}.{}, right_hash: {}",
                  join_constraints.size(), left_hash_info->second, left_hash_info->first, *right_hash_field);

        detail::BetaCacheKey key;
        key.kind = NodeKind::HashedJoin;
        key.constraints = join_constraints;
        key.bindings = binding_to_idx_;
        key.eval_scalar_fields = inline_binding_to_field_;
        key.left_hash_info = *left_hash_info;
        key.right_hash_field = *right_hash_field;

        join_node = std::static_pointer_cast<BaseJoinNode>(
            network_.find_or_create_beta_node<HashedJoinNode>(parents, std::move(key),
                join_constraints, binding_to_idx_, inline_binding_to_field_, runtime_predicates,
                *left_hash_info, *right_hash_field)
        );
    } else {
        logd("  -> Creating/Sharing CrossProductJoinNode with {} join constraints", join_constraints.size());

        detail::BetaCacheKey key;
        key.kind = NodeKind::CrossProductJoin;
        key.constraints = join_constraints;
        key.bindings = binding_to_idx_;
        key.eval_scalar_fields = inline_binding_to_field_;

        join_node = std::static_pointer_cast<BaseJoinNode>(
             network_.find_or_create_beta_node<CrossProductJoinNode>(parents, std::move(key),
                 join_constraints, binding_to_idx_, inline_binding_to_field_, runtime_predicates)
        );
    }

    return join_node;
}

std::shared_ptr<ReteNode>
BetaNetworkBuilder::create_negative_node(ParsedPattern& not_pattern,
                                         std::string const& fact_type,
                                         std::vector<std::shared_ptr<ReteNode>> const& alpha_tails,
                                         std::vector<ParsedConstraint> const& join_constraints) {
    logd("  -> Creating/Sharing NotNode with {} join constraints", join_constraints.size());
    std::vector<std::shared_ptr<ReteNode>> parents = alpha_tails;
    if (last_node_) parents.push_back(last_node_);

    detail::BetaCacheKey key;
    key.kind = NodeKind::Not;
    key.constraints = join_constraints;
    key.bindings = binding_to_idx_;
    key.eval_scalar_fields = inline_binding_to_field_;

    auto const field_types = build_field_type_index(kb_.get_parser_state().parsed_declarations);
    auto runtime_predicates = build_runtime_predicates(
        kb_,
        field_types,
        binding_to_fact_type_,
        fact_type,
        join_constraints);
    auto node = network_.find_or_create_beta_node<NotNode>(
        parents,
        std::move(key),
        join_constraints,
        binding_to_idx_,
        inline_binding_to_field_,
        runtime_predicates);
    return node;
}

std::shared_ptr<ReteNode>
BetaNetworkBuilder::create_existential_node(ParsedPattern& exists_pattern,
                                            std::string const& fact_type,
                                            std::vector<std::shared_ptr<ReteNode>> const& alpha_tails,
                                            std::vector<ParsedConstraint> const& join_constraints) {
    logd("  -> Creating/Sharing ExistsNode with {} join constraints", join_constraints.size());
    std::vector<std::shared_ptr<ReteNode>> parents = alpha_tails;
    if (last_node_) parents.push_back(last_node_);

    detail::BetaCacheKey key;
    key.kind = NodeKind::Exists;
    key.constraints = join_constraints;
    key.bindings = binding_to_idx_;
    key.eval_scalar_fields = inline_binding_to_field_;

    auto const field_types = build_field_type_index(kb_.get_parser_state().parsed_declarations);
    auto runtime_predicates = build_runtime_predicates(
        kb_,
        field_types,
        binding_to_fact_type_,
        fact_type,
        join_constraints);
    auto node = network_.find_or_create_beta_node<ExistsNode>(
        parents,
        std::move(key),
        join_constraints,
        binding_to_idx_,
        inline_binding_to_field_,
        runtime_predicates);
    return node;
}

std::shared_ptr<ReteNode> BetaNetworkBuilder::create_eval_node(ParsedPattern& p) {
    std::vector<std::shared_ptr<ReteNode>> parents;
    if (last_node_) parents.push_back(last_node_);

    detail::BetaCacheKey key;
    key.kind = NodeKind::Eval;
    key.eval_expr = p.eval_expression.value_or("");
    key.bindings = binding_to_idx_;
    key.eval_scalar_fields = inline_binding_to_field_;

    std::optional<rulesforge::RuntimePredicateRef> eval_runtime_predicate;
    std::vector<std::string> eval_runtime_variables;
    std::vector<EvalNode::EvalRuntimeArgument> runtime_arguments;
    if (p.eval_expression) {
        if (auto external_call = parse_external_eval_call(*p.eval_expression)) {
            bool supported_arguments = true;
            runtime_arguments.clear();
            runtime_arguments.reserve(external_call->arguments.size());
            for (auto const& argument : external_call->arguments) {
                EvalNode::EvalRuntimeArgument runtime_argument;
                runtime_argument.text = argument.text;
                runtime_argument.literal = argument.literal;
                switch (argument.kind) {
                    case ExternalEvalArgumentKind::Variable:
                        runtime_argument.kind = EvalNode::EvalRuntimeArgument::Kind::Variable;
                        break;
                    case ExternalEvalArgumentKind::Literal:
                        runtime_argument.kind = EvalNode::EvalRuntimeArgument::Kind::Literal;
                        break;
                    case ExternalEvalArgumentKind::NumericExpression:
                        runtime_argument.kind = EvalNode::EvalRuntimeArgument::Kind::NumericExpression;
                        break;
                }
                runtime_arguments.push_back(std::move(runtime_argument));
            }
            if (supported_arguments) {
                eval_runtime_predicate = rulesforge::RuntimePredicateRef{
                    rulesforge::RuntimePredicateBackend::Native,
                    rulesforge::RuntimePredicateKind::External,
                    0,
                    CompareOp::None,
                    TemporalOp::None,
                    0,
                    external_call->predicate_name};
            } else {
                runtime_arguments.clear();
            }
        }
    }

    auto node = network_.find_or_create_beta_node<EvalNode>(parents, std::move(key),
        std::move(p.eval_expression.value_or("")),
        binding_to_idx_,
        inline_binding_to_field_,
        eval_runtime_predicate,
        std::move(eval_runtime_variables),
        std::move(runtime_arguments));

    logd("  -> Created/Shared EvalNode (ID: {}), code: '{}'", node->id, p.eval_expression.value_or(""));
    return node;
}

std::shared_ptr<ReteNode> BetaNetworkBuilder::create_query_call_node(ParsedPattern& p, ParsedQueryCall& call) {
    if (!p.binding.empty()) {
        throw std::runtime_error("query call row binding is not supported; use the query result pattern bindings");
    }
    std::vector<std::string> output_bindings;
    if (auto const* query = find_query_by_name(kb_.get_parser_state().parsed_queries, call.query_name)) {
        for (auto const& output : collect_query_output_bindings(*query)) {
            output_bindings.push_back(output.binding);
        }
    }
    auto node = network_.create_node<QueryCallNode>(call, binding_to_idx_, std::move(output_bindings));
    network_.query_call_nodes.push_back(static_cast<QueryCallNode*>(node.get()));

    if (last_node_) {
        last_node_->add_child(node);
    } else {
        network_.beta_root_nodes.push_back(node.get());
    }

    logd("  -> Created QueryCallNode (ID: {}) for query '{}'", node->id, call.query_name);
    return node;
}

std::shared_ptr<ReteNode>
BetaNetworkBuilder::create_accumulate_node(ParsedPattern& p, ParsedAccumulate& acc,
                                           std::vector<std::shared_ptr<ReteNode>> const& alpha_tails,
                                           std::vector<ParsedConstraint> const& join_constraints) {
    logd("  -> Creating AccumulateNode: function '{}', result type '{}'", acc.function, p.fact_type);
    auto const* prototype = kb_.get_accumulator_registry().get_prototype(acc.function);
    if (!prototype) throw std::runtime_error("Unknown accumulate function: " + acc.function);
    auto node =
        network_.create_node<AccumulateNode>(prototype, std::move(acc), p.fact_type, binding_to_idx_, join_constraints);

    if (last_node_) {
        logd("    -> Attaching AccumulateNode ID {} to previous node ID {}", node->id, last_node_->id);
        last_node_->add_child(node);
    } else {
        network_.beta_root_nodes.push_back(node.get());
        logd("    -> AccumulateNode ID {} has no beta parent, added to beta_root_nodes", node->id);
    }

    for (auto& tail : alpha_tails) {
        logd("    -> Attaching AccumulateNode ID {} to alpha tail node ID {}", node->id, tail->id);
        tail->add_child(node);
    }
    return node;
}

std::shared_ptr<ReteNode> BetaNetworkBuilder::create_unnest_node(ParsedPattern& p, ParsedUnnest& un) {
    logd("  -> Creating UnnestNode from source binding '{}.{}'", un.source_binding, un.source_field);
    std::vector<ParsedConstraint> constraints;
    collect_leaf_constraints(p.constraint_root.get(), constraints);

    auto const field_types = build_field_type_index(kb_.get_parser_state().parsed_declarations);
    auto runtime_predicates = build_runtime_predicates(
        kb_,
        field_types,
        binding_to_fact_type_,
        p.fact_type,
        constraints);

    auto node = network_.create_node<UnnestNode>(
        un,
        p.fact_type,
        binding_to_idx_,
        std::move(constraints),
        std::move(runtime_predicates));

    if (last_node_) {
        logd("    -> Attaching UnnestNode ID {} to previous node ID {}", node->id, last_node_->id);
        last_node_->add_child(node);
    }
    return node;
}
