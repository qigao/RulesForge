#include "parser/decision_table_compiler.hpp"

#include "parser/decision_table_converter.hpp"
#include "core/constraint_types.hpp"
#include "core/parsed_rule.hpp"
#include "rfl_parser_impl.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <regex>
#include <sstream>

namespace {
enum class FallbackReason : uint8_t {
    UnknownPreamble,
    DeclareParse,
    QueryParse,
    SalienceParse,
    ConditionParse
};

enum class PreambleRecordResult : uint8_t {
    Continue,
    FatalError,
    FallbackUnknownPreamble,
    FallbackDeclareParse,
    FallbackQueryParse
};

enum class RowCompileResult : uint8_t {
    Continue,
    Skip,
    FallbackSalienceParse,
    FallbackConditionParse
};

DecisionTableCompileStats g_compile_stats;

void record_fallback_reason(FallbackReason reason) {
    switch (reason) {
        case FallbackReason::UnknownPreamble: g_compile_stats.fallback_unknown_preamble++; break;
        case FallbackReason::DeclareParse: g_compile_stats.fallback_declare_parse++; break;
        case FallbackReason::QueryParse: g_compile_stats.fallback_query_parse++; break;
        case FallbackReason::SalienceParse: g_compile_stats.fallback_salience_parse++; break;
        case FallbackReason::ConditionParse: g_compile_stats.fallback_condition_parse++; break;
    }
}

struct ColumnDefinition {
    enum class Type : uint8_t {
        Condition,
        Action,
        RuleName,
        Salience,
        AgendaGroup,
        Other
    };

    Type type = Type::Other;
    std::string template_text;
};

struct ColumnLayout {
    std::vector<ColumnDefinition> columns;
    size_t rule_name_col = static_cast<size_t>(-1);
    size_t salience_col = static_cast<size_t>(-1);
    size_t agenda_group_col = static_cast<size_t>(-1);
};

struct RowCompileContext {
    std::string const& package_name;
    std::vector<std::string> const& imports;
};

bool equals_ci(std::string_view a, std::string_view b);

ColumnDefinition::Type parse_dt_column_type(std::string_view s) {
    if (equals_ci(s, "Rule Name")) return ColumnDefinition::Type::RuleName;
    if (equals_ci(s, "Salience")) return ColumnDefinition::Type::Salience;
    if (equals_ci(s, "agenda-group")) return ColumnDefinition::Type::AgendaGroup;
    return ColumnDefinition::Type::Other;
}

std::string trim(std::string s) {
    auto is_ws = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!s.empty() && is_ws(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && is_ws(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

std::string strip_outer_parens(std::string s) {
    s = trim(std::move(s));
    auto fully_wrapped = [](std::string const& v) -> bool {
        if (v.size() < 2 || v.front() != '(' || v.back() != ')') return false;
        int depth = 0;
        bool in_single_quote = false;
        bool in_double_quote = false;
        for (size_t i = 0; i < v.size(); ++i) {
            char c = v[i];
            if (c == '"' && !in_single_quote) { in_double_quote = !in_double_quote; continue; }
            if (c == '\'' && !in_double_quote) { in_single_quote = !in_single_quote; continue; }
            if (in_single_quote || in_double_quote) continue;
            if (c == '(') depth++;
            else if (c == ')') depth--;
            if (depth == 0 && i != v.size() - 1) return false;
            if (depth < 0) return false;
        }
        return depth == 0;
    };

    while (fully_wrapped(s)) {
        s = trim(s.substr(1, s.size() - 2));
    }
    return s;
}

bool starts_with_ci(std::string const& s, std::string_view prefix) {
    if (s.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        char a = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
        char b = static_cast<char>(std::tolower(static_cast<unsigned char>(prefix[i])));
        if (a != b) return false;
    }
    return true;
}

bool equals_ci(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char ca = static_cast<char>(std::tolower(static_cast<unsigned char>(a[i])));
        char cb = static_cast<char>(std::tolower(static_cast<unsigned char>(b[i])));
        if (ca != cb) return false;
    }
    return true;
}

std::vector<std::string> split_csvish(std::string const& s) {
    std::vector<std::string> out;
    std::string current;
    int depth = 0;
    for (char c : s) {
        if (c == '<') depth++;
        if (c == '>') depth = std::max(0, depth - 1);
        if (c == ',' && depth == 0) {
            out.push_back(trim(current));
            current.clear();
            continue;
        }
        current.push_back(c);
    }
    if (!current.empty()) out.push_back(trim(current));
    return out;
}

std::string substitute(std::string_view template_str, std::string_view value) {
    std::string result;
    size_t pos = template_str.find("$1");
    if (pos == std::string::npos) return std::string(template_str);
    result.reserve(template_str.size() - 2 + value.size());
    result.append(template_str.substr(0, pos));
    result.append(value);
    result.append(template_str.substr(pos + 2));
    return result;
}

std::optional<ConstraintValue> parse_literal(std::string raw) {
    raw = trim(std::move(raw));
    if (raw.empty()) return std::nullopt;

    auto lower_copy = [](std::string s) {
        for (char& c : s) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return s;
    };

    if ((raw.front() == '"' && raw.back() == '"') || (raw.front() == '\'' && raw.back() == '\'')) {
        return ConstraintValue{raw.substr(1, raw.size() - 2)};
    }
    std::string raw_lc = lower_copy(raw);
    if (raw_lc == "true") return ConstraintValue{true};
    if (raw_lc == "false") return ConstraintValue{false};

    try {
        size_t idx = 0;
        long long v = std::stoll(raw, &idx);
        if (idx == raw.size()) return ConstraintValue{static_cast<int64_t>(v)};
    } catch (...) {
    }
    try {
        size_t idx = 0;
        double v = std::stod(raw, &idx);
        if (idx == raw.size()) return ConstraintValue{v};
    } catch (...) {
    }

    return ConstraintValue{raw};
}

std::vector<std::string> split_top_level_logical(std::string const& body, std::string const& logical_kw) {
    std::vector<std::string> out;
    std::string current;
    int depth = 0;
    bool in_single_quote = false;
    bool in_double_quote = false;

    auto flush_current = [&]() {
        std::string t = trim(current);
        if (!t.empty()) out.push_back(std::move(t));
        current.clear();
    };

    auto is_boundary = [](char c) {
        return std::isspace(static_cast<unsigned char>(c)) != 0 || c == '(' || c == ')';
    };

    for (size_t i = 0; i < body.size(); ++i) {
        char c = body[i];
        if (c == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
            current.push_back(c);
            continue;
        }
        if (c == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
            current.push_back(c);
            continue;
        }
        if (!in_single_quote && !in_double_quote) {
            if (c == '(') {
                depth++;
                current.push_back(c);
                continue;
            }
            if (c == ')') {
                depth = std::max(0, depth - 1);
                current.push_back(c);
                continue;
            }
            if (depth == 0 && i + logical_kw.size() <= body.size()
                && [&]() {
                       for (size_t k = 0; k < logical_kw.size(); ++k) {
                           char a = static_cast<char>(std::tolower(static_cast<unsigned char>(body[i + k])));
                           char b = static_cast<char>(std::tolower(static_cast<unsigned char>(logical_kw[k])));
                           if (a != b) return false;
                       }
                       return true;
                   }()) {
                char before = (i == 0) ? ' ' : body[i - 1];
                char after = (i + logical_kw.size() >= body.size()) ? ' ' : body[i + logical_kw.size()];
                if (is_boundary(before) && is_boundary(after)) {
                    flush_current();
                    i += logical_kw.size() - 1;
                    continue;
                }
            }
        }
        current.push_back(c);
    }
    flush_current();
    return out;
}

std::vector<std::string> split_top_level_comma(std::string const& s) {
    std::vector<std::string> out;
    std::string current;
    int depth = 0;
    bool in_single_quote = false;
    bool in_double_quote = false;

    auto flush_current = [&]() {
        std::string t = trim(current);
        if (!t.empty()) out.push_back(std::move(t));
        current.clear();
    };

    for (char c : s) {
        if (c == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
            current.push_back(c);
            continue;
        }
        if (c == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
            current.push_back(c);
            continue;
        }
        if (!in_single_quote && !in_double_quote) {
            if (c == '(') {
                depth++;
                current.push_back(c);
                continue;
            }
            if (c == ')') {
                depth = std::max(0, depth - 1);
                current.push_back(c);
                continue;
            }
            if (depth == 0 && c == ',') {
                flush_current();
                continue;
            }
        }
        current.push_back(c);
    }
    flush_current();
    return out;
}

bool parse_declare_record(std::vector<std::string> const& record,
                          std::string const& package_name,
                          ParsedDeclaration& out_decl) {
    if (record.size() < 3) return false;

    out_decl = ParsedDeclaration{};
    out_decl.type_name = trim(record[1]);
    out_decl.source_package = package_name;

    auto field_defs = split_csvish(record[2]);
    for (auto const& field_def : field_defs) {
        auto colon = field_def.find(':');
        if (colon == std::string::npos) return false;

        ParsedField field;
        field.name = trim(field_def.substr(0, colon));
        std::string type_str = trim(field_def.substr(colon + 1));
        field.type = parse_field_type(type_str);
        if (field.type == FT_Unknown && !type_str.empty()) {
            field.type = FT_Object;
            field.type_params.emplace_back(FT_Object, type_str);
        }
        out_decl.fields.push_back(std::move(field));
    }

    return !out_decl.type_name.empty();
}

bool parse_query_record(std::vector<std::string> const& record,
                        std::string const& package_name,
                        ParsedQuery& out_query) {
    if (record.size() < 3) return false;
    static const std::regex query_pat(
        R"(^\s*(?:(\$[A-Za-z_][A-Za-z0-9_]*)\s*:\s*)?([A-Za-z_][A-Za-z0-9_\.]*)\s*\(\s*\)\s*$)");

    std::smatch m;
    if (!std::regex_match(record[2], m, query_pat)) return false;

    ParsedPattern p;
    p.type = PatternType::STANDARD;
    p.binding = m[1].matched ? m[1].str() : "";
    p.fact_type = m[2].str();

    out_query = ParsedQuery{};
    out_query.name = trim(record[1]);
    out_query.source_package = package_name;
    out_query.patterns.push_back(std::move(p));
    return !out_query.name.empty();
}

CompareOp parse_compare_op_ci(std::string op_text) {
    for (char& c : op_text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return parse_compare_op(op_text);
}

bool parse_rhs_list_values(std::string const& rhs, std::vector<ConstraintValue>& out_values) {
    std::string normalized_rhs = trim(rhs);
    if (normalized_rhs.size() < 2 || normalized_rhs.front() != '(' || normalized_rhs.back() != ')') {
        return false;
    }
    std::string inner = trim(normalized_rhs.substr(1, normalized_rhs.size() - 2));
    if (inner.empty()) return false;

    auto parts = split_top_level_comma(inner);
    if (parts.empty()) return false;

    out_values.clear();
    out_values.reserve(parts.size());
    for (auto const& part : parts) {
        auto v = parse_literal(part);
        if (!v.has_value()) return false;
        out_values.push_back(std::move(*v));
    }
    return true;
}

bool parse_rhs_value(std::string const& rhs, CompareOp op, ParsedConstraint& out_constraint) {
    if (op == CompareOp::In || op == CompareOp::NotIn) {
        std::vector<ConstraintValue> values;
        if (!parse_rhs_list_values(rhs, values)) return false;
        out_constraint.right_value_list = std::move(values);
        out_constraint.right_literal = std::nullopt;
        return true;
    }

    auto lit = parse_literal(rhs);
    if (!lit.has_value()) return false;

    // Mirror Lemon's set_rhs_value behavior for string RHS values.
    if (std::holds_alternative<std::string>(*lit)) {
        std::string const& s = std::get<std::string>(*lit);
        if (!s.empty() && s[0] == '$') {
            if (s.find_first_of("+-*/") != std::string::npos) {
                out_constraint.right_arith_expr = s;
                out_constraint.right_literal = std::nullopt;
                out_constraint.right_bound_field = std::nullopt;
                return true;
            }

            size_t dot_pos = s.find('.');
            if (dot_pos != std::string::npos) {
                out_constraint.right_bound_field = {s.substr(0, dot_pos), s.substr(dot_pos + 1)};
            } else {
                out_constraint.right_bound_field = {s, "this"};
            }
            out_constraint.right_literal = std::nullopt;
            return true;
        }
    }

    out_constraint.right_literal = std::move(lit);
    return true;
}

PreambleRecordResult process_preamble_record(std::vector<std::string> const& record,
                                             std::string const& source_name,
                                             parser_state& state,
                                             std::vector<StructuredError>& errors) {
    if (record.empty()) return PreambleRecordResult::Continue;
    std::string directive = trim(record[0]);

    if (directive == "PACKAGE") {
        if (record.size() < 2) {
            errors.push_back({.file_name = source_name, .message = "PACKAGE requires a name"});
            return PreambleRecordResult::FatalError;
        }
        state.package_name = trim(record[1]);
        return PreambleRecordResult::Continue;
    }
    if (directive == "IMPORT") {
        if (record.size() < 2) {
            errors.push_back({.file_name = source_name, .message = "IMPORT requires a target"});
            return PreambleRecordResult::FatalError;
        }
        state.parsed_imports.push_back(trim(record[1]));
        return PreambleRecordResult::Continue;
    }
    if (directive == "DECLARE") {
        ParsedDeclaration decl;
        if (!parse_declare_record(record, state.package_name, decl)) {
            return PreambleRecordResult::FallbackDeclareParse;
        }
        state.parsed_declarations.push_back(std::move(decl));
        return PreambleRecordResult::Continue;
    }
    if (directive == "QUERY") {
        ParsedQuery query;
        if (!parse_query_record(record, state.package_name, query)) {
            return PreambleRecordResult::FallbackQueryParse;
        }
        state.parsed_queries.push_back(std::move(query));
        return PreambleRecordResult::Continue;
    }
    return PreambleRecordResult::FallbackUnknownPreamble;
}

std::optional<FallbackReason> to_fallback_reason(PreambleRecordResult result) {
    switch (result) {
        case PreambleRecordResult::FallbackUnknownPreamble: return FallbackReason::UnknownPreamble;
        case PreambleRecordResult::FallbackDeclareParse: return FallbackReason::DeclareParse;
        case PreambleRecordResult::FallbackQueryParse: return FallbackReason::QueryParse;
        case PreambleRecordResult::Continue:
        case PreambleRecordResult::FatalError: return std::nullopt;
    }
    return std::nullopt;
}

std::optional<FallbackReason> to_fallback_reason(RowCompileResult result) {
    switch (result) {
        case RowCompileResult::FallbackSalienceParse: return FallbackReason::SalienceParse;
        case RowCompileResult::FallbackConditionParse: return FallbackReason::ConditionParse;
        case RowCompileResult::Continue:
        case RowCompileResult::Skip: return std::nullopt;
    }
    return std::nullopt;
}

parser_state execute_fallback_parse(DecisionTable const& table,
                                    std::string const& source_name,
                                    std::vector<StructuredError>& errors,
                                    FallbackReason reason) {
    record_fallback_reason(reason);
    DecisionTableConverter converter(table);
    std::string generated_rfl = converter.generate_drl();
    parser_state out = generated_rfl.empty() ? parser_state{} : rfl_parse_lemon(generated_rfl, source_name, errors);
    if (errors.empty()) {
        g_compile_stats.fallback_success++;
    }
    return out;
}

std::optional<parser_state> maybe_execute_fallback(DecisionTable const& table,
                                                   std::string const& source_name,
                                                   std::vector<StructuredError>& errors,
                                                   std::optional<FallbackReason> maybe_reason) {
    if (!maybe_reason.has_value()) return std::nullopt;
    return execute_fallback_parse(table, source_name, errors, *maybe_reason);
}

bool parse_constraint_text(std::string const& text, ParsedConstraint& out_constraint) {
    std::string normalized = strip_outer_parens(text);

    static const std::regex binding_only_pat(
        R"(^\s*(\$[A-Za-z_][A-Za-z0-9_]*)\s*:\s*([A-Za-z_][A-Za-z0-9_\.]*)\s*$)");
    static const std::regex binding_rel_pat(
        R"(^\s*(\$[A-Za-z_][A-Za-z0-9_]*)\s*:\s*([A-Za-z_][A-Za-z0-9_\.]*)\s*(==|!=|>=|<=|>|<|contains|not contains|matches|not matches|startsWith|endsWith|lengthIs|memberOf|not memberOf|in|not in|containsKey|not containsKey)\s*(.+)\s*$)",
        std::regex_constants::icase);
    static const std::regex cpat(
        R"(^\s*([A-Za-z_][A-Za-z0-9_\.]*)\s*(==|!=|>=|<=|>|<|contains|not contains|matches|not matches|startsWith|endsWith|lengthIs|memberOf|not memberOf|in|not in|containsKey|not containsKey)\s*(.+)\s*$)",
        std::regex_constants::icase);
    static const std::regex bare_field_pat(
        R"(^\s*([A-Za-z_][A-Za-z0-9_\.]*)\s*$)");
    static const std::regex negated_bare_field_pat(
        R"(^\s*!\s*([A-Za-z_][A-Za-z0-9_\.]*)\s*$)");

    out_constraint = ParsedConstraint{};

    std::smatch m;
    size_t rhs_index = 0;

    if (std::regex_match(normalized, m, binding_only_pat)) {
        out_constraint.field_binding = m[1].str();
        out_constraint.left_field = m[2].str();
        out_constraint.op = CompareOp::None;
        return true;
    }

    if (std::regex_match(normalized, m, binding_rel_pat)) {
        out_constraint.field_binding = m[1].str();
        out_constraint.left_field = m[2].str();
        out_constraint.op = parse_compare_op_ci(m[3].str());
        rhs_index = 4;
    } else if (std::regex_match(normalized, m, cpat)) {
        out_constraint.left_field = m[1].str();
        out_constraint.op = parse_compare_op_ci(m[2].str());
        rhs_index = 3;
    } else if (std::regex_match(normalized, m, bare_field_pat)) {
        out_constraint.left_field = m[1].str();
        out_constraint.op = CompareOp::EQ;
        out_constraint.right_literal = static_cast<int64_t>(1);
        return true;
    } else if (std::regex_match(normalized, m, negated_bare_field_pat)) {
        out_constraint.left_field = m[1].str();
        out_constraint.op = CompareOp::EQ;
        out_constraint.right_literal = static_cast<int64_t>(0);
        return true;
    } else {
        return false;
    }

    if (out_constraint.op == CompareOp::None) return false;
    return parse_rhs_value(m[rhs_index].str(), out_constraint.op, out_constraint);
}

bool parse_constraint_expr_recursive(std::string const& text, std::unique_ptr<ConstraintNode>& out_node) {
    std::string normalized = strip_outer_parens(text);

    auto build_logical_node = [](std::vector<std::string> const& parts,
                                 NodeType type,
                                 std::unique_ptr<ConstraintNode>& out) -> bool {
        if (parts.size() <= 1) return false;
        auto logical_node = std::make_unique<ConstraintNode>(type);
        for (auto const& part : parts) {
            std::unique_ptr<ConstraintNode> child;
            if (!parse_constraint_expr_recursive(part, child) || !child) return false;
            logical_node->children.push_back(std::move(child));
        }
        out = std::move(logical_node);
        return true;
    };

    auto or_parts = split_top_level_logical(normalized, "or");
    if (build_logical_node(or_parts, NodeType::OR, out_node)) return true;

    auto comma_parts = split_top_level_comma(normalized);
    if (build_logical_node(comma_parts, NodeType::AND, out_node)) return true;

    auto and_parts = split_top_level_logical(normalized, "and");
    if (build_logical_node(and_parts, NodeType::AND, out_node)) return true;

    ParsedConstraint c;
    if (!parse_constraint_text(normalized, c)) return false;
    auto leaf = std::make_unique<ConstraintNode>(NodeType::LEAF);
    leaf->constraint = std::move(c);
    out_node = std::move(leaf);
    return true;
}

bool parse_condition_pattern(std::string const& condition_text, ParsedPattern& out_pattern) {
    // Supported direct subset:
    //   Type()
    //   Type(field OP literal)
    //   $b: Type(field OP literal)
    std::string s = trim(condition_text);
    std::string binding;
    PatternType pattern_type = PatternType::STANDARD;

    // Optional unary pattern qualifiers
    if (starts_with_ci(s, "not ")) {
        pattern_type = PatternType::NOT;
        s = trim(s.substr(4));
    } else if (starts_with_ci(s, "exists ")) {
        pattern_type = PatternType::EXISTS;
        s = trim(s.substr(7));
    }

    // Optional binding prefix: $b: Type(...)
    if (!s.empty() && s[0] == '$') {
        size_t colon = s.find(':');
        if (colon == std::string::npos) return false;
        binding = trim(s.substr(0, colon));
        s = trim(s.substr(colon + 1));
    }

    // Parse Type(body) with balanced parentheses.
    size_t open_pos = s.find('(');
    if (open_pos == std::string::npos) return false;
    std::string fact_type = trim(s.substr(0, open_pos));
    if (fact_type.empty()) return false;

    size_t close_pos = std::string::npos;
    int depth = 0;
    bool in_single_quote = false;
    bool in_double_quote = false;
    for (size_t i = open_pos; i < s.size(); ++i) {
        char c = s[i];
        if (c == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
            continue;
        }
        if (c == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
            continue;
        }
        if (in_single_quote || in_double_quote) continue;

        if (c == '(') depth++;
        else if (c == ')') {
            depth--;
            if (depth == 0) {
                close_pos = i;
                break;
            }
            if (depth < 0) return false;
        }
    }
    if (close_pos == std::string::npos) return false;
    if (!trim(s.substr(close_pos + 1)).empty()) return false;

    std::string body = trim(s.substr(open_pos + 1, close_pos - open_pos - 1));

    out_pattern = ParsedPattern{};
    out_pattern.type = pattern_type;
    out_pattern.binding = std::move(binding);
    out_pattern.fact_type = std::move(fact_type);

    if (body.empty()) {
        return true;
    }

    std::unique_ptr<ConstraintNode> root;
    if (!parse_constraint_expr_recursive(body, root) || !root) return false;
    out_pattern.constraint_root = std::move(root);
    return true;
}

bool is_table_empty(parser_state const& state) {
    return state.parsed_rules.empty() && state.parsed_queries.empty() && state.parsed_functions.empty()
           && state.parsed_declarations.empty() && state.parsed_globals.empty()
           && state.parsed_imports.empty() && state.package_name.empty();
}

std::optional<std::string> get_effective_cell(std::vector<std::string> const& row, size_t col_index) {
    if (col_index == static_cast<size_t>(-1) || col_index >= row.size()) return std::nullopt;
    std::string value = trim(row[col_index]);
    if (value.empty() || value == "*") return std::nullopt;
    return value;
}

ColumnLayout build_column_layout(DecisionTable const& table) {
    ColumnLayout layout;
    layout.columns.reserve(table.headers.size());
    for (auto const& header : table.headers) {
        std::string h = trim(header);
        if (starts_with_ci(h, "condition:")) {
            std::string templ = trim(h.substr(std::string_view("condition:").size()));
            layout.columns.push_back({ColumnDefinition::Type::Condition, templ});
        } else if (starts_with_ci(h, "action:")) {
            std::string templ = trim(h.substr(std::string_view("action:").size()));
            layout.columns.push_back({ColumnDefinition::Type::Action, templ});
        } else {
            layout.columns.push_back({parse_dt_column_type(h), ""});
        }
    }

    if (!layout.columns.empty()) {
        bool has_explicit_rule_name = false;
        for (auto const& col : layout.columns) {
            if (col.type == ColumnDefinition::Type::RuleName) {
                has_explicit_rule_name = true;
                break;
            }
        }
        if (!has_explicit_rule_name && trim(table.headers[0]).empty()) {
            layout.columns[0].type = ColumnDefinition::Type::RuleName;
        }
    }

    for (size_t i = 0; i < layout.columns.size(); ++i) {
        if (layout.columns[i].type == ColumnDefinition::Type::RuleName) layout.rule_name_col = i;
        if (layout.columns[i].type == ColumnDefinition::Type::Salience) layout.salience_col = i;
        if (layout.columns[i].type == ColumnDefinition::Type::AgendaGroup) layout.agenda_group_col = i;
    }

    return layout;
}

RowCompileResult compile_row(std::vector<std::string> const& row,
                             size_t row_idx,
                             std::vector<ColumnDefinition> const& cols,
                             size_t rule_name_col,
                             size_t salience_col,
                             size_t agenda_group_col,
                             RowCompileContext const& context,
                             ParsedRule& out_rule) {
    if (row.empty()) return RowCompileResult::Skip;

    ParsedRule rule;
    rule.name = "DecisionTable_Row_" + std::to_string(row_idx + 1);
    rule.source_package = context.package_name;
    rule.source_imports = context.imports;
    rule.condition_groups = {{}};

    if (auto rule_name = get_effective_cell(row, rule_name_col); rule_name.has_value()) {
        rule.name = *rule_name;
    }
    if (auto salience_text = get_effective_cell(row, salience_col); salience_text.has_value()) {
        try {
            rule.salience = std::stoi(*salience_text);
            rule.salience_explicitly_set = true;
        } catch (...) {
            return RowCompileResult::FallbackSalienceParse;
        }
    }
    if (auto agenda_group = get_effective_cell(row, agenda_group_col); agenda_group.has_value()) {
        rule.agenda_group = *agenda_group;
    }

    std::stringstream rhs;
    for (size_t col_idx = 0; col_idx < row.size() && col_idx < cols.size(); ++col_idx) {
        auto const& col = cols[col_idx];
        std::string const val = trim(row[col_idx]);
        if (val.empty() || val == "*") continue;

        if (col.type == ColumnDefinition::Type::Condition) {
            ParsedPattern p;
            if (!parse_condition_pattern(substitute(col.template_text, val), p)) {
                return RowCompileResult::FallbackConditionParse;
            }
            rule.condition_groups[0].push_back(std::move(p));
        } else if (col.type == ColumnDefinition::Type::Action) {
            rhs << substitute(col.template_text, val) << "\n";
        }
    }

    rule.rhs_code = rhs.str();
    out_rule = std::move(rule);
    return RowCompileResult::Continue;
}
}  // namespace

parser_state DirectTableCompiler::compile(DecisionTable const& table,
                                          std::string const& source_name,
                                          std::vector<StructuredError>& errors) {
    parser_state state;

    for (auto const& record : table.preamble_records) {
        PreambleRecordResult result = process_preamble_record(record, source_name, state, errors);
        if (result == PreambleRecordResult::Continue) continue;
        if (result == PreambleRecordResult::FatalError) return parser_state{};
        if (auto fallback_state = maybe_execute_fallback(table, source_name, errors, to_fallback_reason(result));
            fallback_state.has_value()) {
            return *fallback_state;
        }
    }

    ColumnLayout layout = build_column_layout(table);
    RowCompileContext row_context{state.package_name, state.parsed_imports};

    for (size_t row_idx = 0; row_idx < table.data.size(); ++row_idx) {
        auto const& row = table.data[row_idx];
        ParsedRule rule;
        RowCompileResult result = compile_row(
            row,
            row_idx,
            layout.columns,
            layout.rule_name_col,
            layout.salience_col,
            layout.agenda_group_col,
            row_context,
            rule);
        if (result == RowCompileResult::Skip) continue;
        if (auto fallback_state = maybe_execute_fallback(table, source_name, errors, to_fallback_reason(result));
            fallback_state.has_value()) {
            return *fallback_state;
        }
        state.parsed_rules.push_back(std::move(rule));
    }

    g_compile_stats.direct_success++;
    return is_table_empty(state) ? parser_state{} : state;
}

DecisionTableCompileStats DirectTableCompiler::get_stats() {
    return g_compile_stats;
}

void DirectTableCompiler::reset_stats() {
    g_compile_stats = {};
}
