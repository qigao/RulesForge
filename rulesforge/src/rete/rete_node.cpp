#include "core/logging_control.hpp"

#include "engine/knowledge_base.hpp"
#include "rete/rete_node.hpp"
#include "engine/stateful_session.hpp"
#include "turbo_parser.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iosfwd>
#include <chrono>

#include <sstream>
#include <stdexcept>
#include <typeinfo>
#include <utility>

using namespace rulesforge;

// --- Helper Functions ---
namespace {
    std::atomic<uint64_t> g_alpha_checks{0};
    std::atomic<uint64_t> g_join_checks{0};
    std::atomic<uint64_t> g_compare_calls{0};
    std::atomic<uint64_t> g_field_lookups{0};

    bool changed_fields_contains(rulesforge::ModifiedFieldsHint const* changed_fields,
                                 std::string_view field) {
        if (!changed_fields) return true;
        return changed_fields->contains(field);
    }

    std::optional<double> numeric_value(ConstraintValue const& value) {
        if (auto const* i = std::get_if<int64_t>(&value)) {
            return static_cast<double>(*i);
        }
        if (auto const* d = std::get_if<double>(&value)) {
            return *d;
        }
        return std::nullopt;
    }

    bool evaluate_compare(CompareOp op, ConstraintValue const& lhs, ConstraintValue const& rhs) {
        if (auto lhs_num = numeric_value(lhs)) {
            if (auto rhs_num = numeric_value(rhs)) {
                switch (op) {
                    case CompareOp::EQ: return *lhs_num == *rhs_num;
                    case CompareOp::NE: return *lhs_num != *rhs_num;
                    case CompareOp::GT: return *lhs_num > *rhs_num;
                    case CompareOp::LT: return *lhs_num < *rhs_num;
                    case CompareOp::GE: return *lhs_num >= *rhs_num;
                    case CompareOp::LE: return *lhs_num <= *rhs_num;
                    default: return false;
                }
            }
        }

        auto const* lhs_str = std::get_if<std::string>(&lhs);
        auto const* rhs_str = std::get_if<std::string>(&rhs);
        if (lhs_str && op == CompareOp::LengthIs) {
            if (auto rhs_num = numeric_value(rhs)) {
                return static_cast<double>(lhs_str->size()) == *rhs_num;
            }
            return false;
        }
        if (lhs_str && rhs_str) {
            switch (op) {
                case CompareOp::EQ: return *lhs_str == *rhs_str;
                case CompareOp::NE: return *lhs_str != *rhs_str;
                case CompareOp::GT: return *lhs_str > *rhs_str;
                case CompareOp::LT: return *lhs_str < *rhs_str;
                case CompareOp::GE: return *lhs_str >= *rhs_str;
                case CompareOp::LE: return *lhs_str <= *rhs_str;
                case CompareOp::Contains: return lhs_str->find(*rhs_str) != std::string::npos;
                case CompareOp::NotContains: return lhs_str->find(*rhs_str) == std::string::npos;
                case CompareOp::StartsWith: return lhs_str->rfind(*rhs_str, 0) == 0;
                case CompareOp::EndsWith:
                    return lhs_str->size() >= rhs_str->size()
                        && lhs_str->compare(lhs_str->size() - rhs_str->size(), rhs_str->size(), *rhs_str) == 0;
                default: return false;
            }
        }

        if (op == CompareOp::EQ || op == CompareOp::NE) {
            bool const equal = ConstraintValueEquals{}(lhs, rhs);
            return op == CompareOp::EQ ? equal : !equal;
        }
        return false;
    }

    bool evaluate_temporal(TemporalOp op, std::int64_t lhs, std::int64_t rhs, std::int64_t window_ms) {
        switch (op) {
            case TemporalOp::After:
                return lhs > rhs;
            case TemporalOp::Before:
                return lhs < rhs;
            case TemporalOp::Within:
                return window_ms >= 0 && std::llabs(lhs - rhs) <= window_ms;
            case TemporalOp::Coincides:
                return lhs == rhs;
            case TemporalOp::During:
                return lhs >= rhs;
            case TemporalOp::None:
                return false;
        }
        return false;
    }

    bool is_expression_identifier_char(char c) {
        unsigned char const uc = static_cast<unsigned char>(c);
        return std::isalnum(uc) || c == '_';
    }

    std::string trim_ascii_copy(std::string value) {
        auto const not_space = [](unsigned char ch) { return std::isspace(ch) == 0; };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
        value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
        return value;
    }

    std::vector<std::string> extract_expression_variables(std::string const& expression) {
        std::vector<std::string> variables;
        for (std::size_t pos = 0; pos < expression.size();) {
            char const ch = expression[pos];
            if (ch == '\'' || ch == '"') {
                char const quote = ch;
                ++pos;
                while (pos < expression.size()) {
                    if (expression[pos] == '\\' && pos + 1 < expression.size()) {
                        pos += 2;
                        continue;
                    }
                    if (expression[pos++] == quote) {
                        break;
                    }
                }
                continue;
            }
            if (ch != '$') {
                ++pos;
                continue;
            }

            std::size_t const start = pos++;
            if (pos >= expression.size() || !is_expression_identifier_char(expression[pos])) {
                continue;
            }
            while (pos < expression.size() && is_expression_identifier_char(expression[pos])) {
                ++pos;
            }
            while (pos < expression.size() && expression[pos] == '.') {
                std::size_t const checkpoint = pos++;
                if (pos >= expression.size() || !is_expression_identifier_char(expression[pos])) {
                    pos = checkpoint;
                    break;
                }
                while (pos < expression.size() && is_expression_identifier_char(expression[pos])) {
                    ++pos;
                }
            }

            std::string variable = expression.substr(start, pos - start);
            if (std::find(variables.begin(), variables.end(), variable) == variables.end()) {
                variables.push_back(std::move(variable));
            }
        }
        return variables;
    }

    std::optional<ConstraintValue> resolve_expression_variable(
        std::string const& variable,
        Fact const& current_fact,
        Token const* token,
        std::map<std::string, int> const* bindings,
        std::map<std::string, std::string> const* scalar_binding_fields = nullptr) {
        std::string binding = variable;
        std::string field = "this";
        bool scalar_field_mapped = false;
        if (auto const dot_pos = variable.find('.'); dot_pos != std::string::npos) {
            binding = trim_ascii_copy(variable.substr(0, dot_pos));
            field = trim_ascii_copy(variable.substr(dot_pos + 1));
        } else if (bindings == nullptr && !variable.empty() && variable[0] == '$') {
            field = variable.substr(1);
        } else if (scalar_binding_fields != nullptr) {
            auto field_it = scalar_binding_fields->find(binding);
            if (field_it == scalar_binding_fields->end() && !binding.empty() && binding[0] == '$') {
                field_it = scalar_binding_fields->find(binding.substr(1));
            }
            if (field_it != scalar_binding_fields->end()) {
                field = field_it->second;
                scalar_field_mapped = true;
            }
        }

        Fact const* source_fact = &current_fact;
        if (bindings != nullptr) {
            auto it = bindings->find(binding);
            if (it == bindings->end() && !binding.empty() && binding[0] != '$') {
                it = bindings->find("$" + binding);
            } else if (it == bindings->end() && !binding.empty() && binding[0] == '$') {
                it = bindings->find(binding.substr(1));
            }
            if (it != bindings->end() && token != nullptr) {
                if (auto const* token_fact = token->get_fact_at_depth(it->second)) {
                    source_fact = token_fact;
                } else if (it->second < token->get_depth()) {
                    return std::nullopt;
                }
            } else if (it == bindings->end() && !scalar_field_mapped
                       && binding == variable && !variable.empty() && variable[0] == '$') {
                field = variable.substr(1);
            } else if (it == bindings->end() && binding != variable) {
                return std::nullopt;
            } else if (it == bindings->end() && scalar_field_mapped) {
                return std::nullopt;
            }
        }

        if (field == "this") {
            return ConstraintValue{static_cast<std::int64_t>(source_fact->id)};
        }
        return source_fact->get_field(field);
    }

    std::string trim_expression_text(std::string const& text) {
        std::size_t begin = 0;
        while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
        std::size_t end = text.size();
        while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
        return text.substr(begin, end - begin);
    }

    bool has_wrapping_parentheses(std::string const& expression) {
        if (expression.size() < 2 || expression.front() != '(' || expression.back() != ')') {
            return false;
        }
        bool in_string = false;
        char quote = '\0';
        int depth = 0;
        for (std::size_t index = 0; index < expression.size(); ++index) {
            char const ch = expression[index];
            if (in_string) {
                if (ch == '\\' && index + 1 < expression.size()) {
                    ++index;
                    continue;
                }
                if (ch == quote) in_string = false;
                continue;
            }
            if (ch == '"' || ch == '\'') {
                in_string = true;
                quote = ch;
                continue;
            }
            if (ch == '(') {
                ++depth;
                continue;
            }
            if (ch == ')') {
                --depth;
                if (depth == 0 && index + 1 != expression.size()) {
                    return false;
                }
            }
        }
        return depth == 0;
    }

    std::optional<std::size_t> find_top_level_token(std::string const& expression, std::string_view token) {
        bool in_string = false;
        char quote = '\0';
        int depth = 0;
        for (std::size_t index = 0; index + token.size() <= expression.size(); ++index) {
            char const ch = expression[index];
            if (in_string) {
                if (ch == '\\' && index + 1 < expression.size()) {
                    ++index;
                    continue;
                }
                if (ch == quote) in_string = false;
                continue;
            }
            if (ch == '"' || ch == '\'') {
                in_string = true;
                quote = ch;
                continue;
            }
            if (ch == '(') {
                ++depth;
                continue;
            }
            if (ch == ')') {
                --depth;
                continue;
            }
            if (depth == 0 && expression.compare(index, token.size(), token) == 0) {
                return index;
            }
        }
        return std::nullopt;
    }

    struct RuntimeCompareMatch {
        std::size_t pos = 0;
        std::size_t token_size = 0;
        CompareOp op = CompareOp::None;
    };

    std::optional<RuntimeCompareMatch> find_top_level_compare_op(std::string const& expression) {
        static constexpr std::pair<std::string_view, CompareOp> ops[] = {
            {"==", CompareOp::EQ},
            {"!=", CompareOp::NE},
            {">=", CompareOp::GE},
            {"<=", CompareOp::LE},
            {">", CompareOp::GT},
            {"<", CompareOp::LT},
        };
        for (auto const& op : ops) {
            if (auto pos = find_top_level_token(expression, op.first)) {
                return RuntimeCompareMatch{*pos, op.first.size(), op.second};
            }
        }
        return std::nullopt;
    }

    std::vector<std::string> split_top_level_arguments(std::string const& text) {
        std::vector<std::string> args;
        bool in_string = false;
        char quote = '\0';
        int depth = 0;
        std::size_t begin = 0;
        for (std::size_t index = 0; index < text.size(); ++index) {
            char const ch = text[index];
            if (in_string) {
                if (ch == '\\' && index + 1 < text.size()) {
                    ++index;
                    continue;
                }
                if (ch == quote) in_string = false;
                continue;
            }
            if (ch == '"' || ch == '\'') {
                in_string = true;
                quote = ch;
                continue;
            }
            if (ch == '(') {
                ++depth;
                continue;
            }
            if (ch == ')') {
                --depth;
                continue;
            }
            if (ch == ',' && depth == 0) {
                args.push_back(trim_expression_text(text.substr(begin, index - begin)));
                begin = index + 1;
            }
        }
        args.push_back(trim_expression_text(text.substr(begin)));
        return args;
    }

    std::optional<std::pair<std::string, std::vector<std::string>>> parse_function_call(std::string const& text) {
        std::size_t pos = 0;
        while (pos < text.size()) {
            unsigned char const ch = static_cast<unsigned char>(text[pos]);
            if (!std::isalpha(ch) && text[pos] != '_') break;
            ++pos;
            while (pos < text.size()) {
                unsigned char const next = static_cast<unsigned char>(text[pos]);
                if (!std::isalnum(next) && text[pos] != '_') break;
                ++pos;
            }
            break;
        }
        if (pos == 0 || pos >= text.size() || text[pos] != '(' || text.back() != ')') {
            return std::nullopt;
        }
        std::string wrapped = text.substr(pos);
        if (!has_wrapping_parentheses(wrapped)) {
            return std::nullopt;
        }
        return std::pair<std::string, std::vector<std::string>>{
            text.substr(0, pos),
            split_top_level_arguments(text.substr(pos + 1, text.size() - pos - 2))};
    }

    std::optional<std::pair<std::size_t, char>> find_simple_arithmetic_op(std::string const& expression) {
        bool in_string = false;
        char quote = '\0';
        for (int pass = 0; pass < 2; ++pass) {
            in_string = false;
            int depth = 0;
            for (std::size_t index = 0; index < expression.size(); ++index) {
                char const ch = expression[index];
                if (in_string) {
                    if (ch == '\\' && index + 1 < expression.size()) {
                        ++index;
                        continue;
                    }
                    if (ch == quote) in_string = false;
                    continue;
                }
                if (ch == '"' || ch == '\'') {
                    in_string = true;
                    quote = ch;
                    continue;
                }
                if (ch == '(') {
                    ++depth;
                    continue;
                }
                if (ch == ')') {
                    --depth;
                    continue;
                }
                if (depth != 0) continue;
                bool const match = pass == 0 ? (ch == '+' || ch == '-') : (ch == '*' || ch == '/');
                if (!match) continue;
                if ((ch == '+' || ch == '-') && (index == 0 || expression[index - 1] == 'e' || expression[index - 1] == 'E')) {
                    continue;
                }
                return std::pair<std::size_t, char>{index, ch};
            }
        }
        return std::nullopt;
    }

    std::optional<ConstraintValue> evaluate_simple_runtime_expression(
        std::string const& expression,
        Fact const& current_fact,
        Token const* token,
        std::map<std::string, int> const* bindings,
        std::map<std::string, std::string> const* scalar_binding_fields = nullptr) {
        std::string text = trim_expression_text(expression);
        if (text.empty()) return std::nullopt;
        if (has_wrapping_parentheses(text)) {
            return evaluate_simple_runtime_expression(
                text.substr(1, text.size() - 2),
                current_fact,
                token,
                bindings,
                scalar_binding_fields);
        }
        if (text == "pi") {
            return ConstraintValue{3.14159265358979323846};
        }
        if (auto function = parse_function_call(text)) {
            std::vector<double> values;
            values.reserve(function->second.size());
            for (auto const& arg : function->second) {
                auto value = evaluate_simple_runtime_expression(
                    arg,
                    current_fact,
                    token,
                    bindings,
                    scalar_binding_fields);
                if (!value) return std::nullopt;
                auto number = numeric_value(*value);
                if (!number) return std::nullopt;
                values.push_back(*number);
            }
            auto const& name = function->first;
            if (name == "floor" && values.size() == 1) return ConstraintValue{std::floor(values[0])};
            if (name == "ceil" && values.size() == 1) return ConstraintValue{std::ceil(values[0])};
            if (name == "abs" && values.size() == 1) return ConstraintValue{std::fabs(values[0])};
            if (name == "round" && values.size() == 1) return ConstraintValue{std::round(values[0])};
            if (name == "sqrt" && values.size() == 1) return ConstraintValue{std::sqrt(values[0])};
            if (name == "sin" && values.size() == 1) return ConstraintValue{std::sin(values[0])};
            if (name == "cos" && values.size() == 1) return ConstraintValue{std::cos(values[0])};
            if (name == "tan" && values.size() == 1) return ConstraintValue{std::tan(values[0])};
            if (name == "acos" && values.size() == 1) return ConstraintValue{std::acos(values[0])};
            if (name == "asin" && values.size() == 1) return ConstraintValue{std::asin(values[0])};
            if (name == "atan" && values.size() == 1) return ConstraintValue{std::atan(values[0])};
            if (name == "log" && values.size() == 1) return ConstraintValue{std::log(values[0])};
            if (name == "exp" && values.size() == 1) return ConstraintValue{std::exp(values[0])};
            if (name == "min" && values.size() == 2) return ConstraintValue{std::min(values[0], values[1])};
            if (name == "max" && values.size() == 2) return ConstraintValue{std::max(values[0], values[1])};
            if (name == "pow" && values.size() == 2) return ConstraintValue{std::pow(values[0], values[1])};
            if (name == "fmod" && values.size() == 2) return ConstraintValue{std::fmod(values[0], values[1])};
            return std::nullopt;
        }
        if (auto op = find_simple_arithmetic_op(text)) {
            auto lhs = evaluate_simple_runtime_expression(
                text.substr(0, op->first),
                current_fact,
                token,
                bindings,
                scalar_binding_fields);
            auto rhs = evaluate_simple_runtime_expression(
                text.substr(op->first + 1),
                current_fact,
                token,
                bindings,
                scalar_binding_fields);
            if (!lhs || !rhs) return std::nullopt;
            auto lhs_num = numeric_value(*lhs);
            auto rhs_num = numeric_value(*rhs);
            if (!lhs_num || !rhs_num) return std::nullopt;
            double result = 0.0;
            switch (op->second) {
                case '+': result = *lhs_num + *rhs_num; break;
                case '-': result = *lhs_num - *rhs_num; break;
                case '*': result = *lhs_num * *rhs_num; break;
                case '/':
                    if (*rhs_num == 0.0) return std::nullopt;
                    result = *lhs_num / *rhs_num;
                    break;
                default: return std::nullopt;
            }
            if (std::holds_alternative<int64_t>(*lhs)
                && std::holds_alternative<int64_t>(*rhs)
                && op->second != '/') {
                return ConstraintValue{static_cast<int64_t>(result)};
            }
            return ConstraintValue{result};
        }
        if ((text.front() == '"' && text.back() == '"') || (text.front() == '\'' && text.back() == '\'')) {
            return ConstraintValue{text.substr(1, text.size() - 2)};
        }
        if (!text.empty() && text.front() == '$') {
            return resolve_expression_variable(text, current_fact, token, bindings, scalar_binding_fields);
        }
        int64_t int_value = 0;
        auto [int_ptr, int_ec] = std::from_chars(text.data(), text.data() + text.size(), int_value);
        if (int_ec == std::errc{} && int_ptr == text.data() + text.size()) {
            return ConstraintValue{int_value};
        }
        char* parse_end = nullptr;
        double double_value = std::strtod(text.c_str(), &parse_end);
        if (parse_end != nullptr && *parse_end == '\0') {
            return ConstraintValue{double_value};
        }
        return std::nullopt;
    }

    std::optional<ConstraintValue> evaluate_runtime_expression(
        std::string const& expression,
        Fact const& current_fact,
        Token const* token,
        std::map<std::string, int> const* bindings,
        std::map<std::string, std::string> const* scalar_binding_fields = nullptr) {
        return evaluate_simple_runtime_expression(
            expression,
            current_fact,
            token,
            bindings,
            scalar_binding_fields);
    }

    std::optional<bool> evaluate_runtime_boolean_expression(
        std::string const& expression,
        Fact const& current_fact,
        Token const* token,
        std::map<std::string, int> const* bindings,
        std::map<std::string, std::string> const* scalar_binding_fields = nullptr) {
        std::string text = trim_expression_text(expression);
        if (text.empty()) return std::nullopt;
        if (has_wrapping_parentheses(text)) {
            return evaluate_runtime_boolean_expression(
                text.substr(1, text.size() - 2),
                current_fact,
                token,
                bindings,
                scalar_binding_fields);
        }
        if (auto pos = find_top_level_token(text, "||")) {
            auto lhs = evaluate_runtime_boolean_expression(
                text.substr(0, *pos),
                current_fact,
                token,
                bindings,
                scalar_binding_fields);
            if (lhs && *lhs) return true;
            auto rhs = evaluate_runtime_boolean_expression(
                text.substr(*pos + 2),
                current_fact,
                token,
                bindings,
                scalar_binding_fields);
            if (!lhs || !rhs) return std::nullopt;
            return *lhs || *rhs;
        }
        if (auto pos = find_top_level_token(text, "&&")) {
            auto lhs = evaluate_runtime_boolean_expression(
                text.substr(0, *pos),
                current_fact,
                token,
                bindings,
                scalar_binding_fields);
            if (lhs && !*lhs) return false;
            auto rhs = evaluate_runtime_boolean_expression(
                text.substr(*pos + 2),
                current_fact,
                token,
                bindings,
                scalar_binding_fields);
            if (!lhs || !rhs) return std::nullopt;
            return *lhs && *rhs;
        }
        if (auto compare = find_top_level_compare_op(text)) {
            auto lhs = evaluate_runtime_expression(
                text.substr(0, compare->pos),
                current_fact,
                token,
                bindings,
                scalar_binding_fields);
            auto rhs = evaluate_runtime_expression(
                text.substr(compare->pos + compare->token_size),
                current_fact,
                token,
                bindings,
                scalar_binding_fields);
            if (!lhs || !rhs) return std::nullopt;
            return evaluate_compare(compare->op, *lhs, *rhs);
        }
        auto value = evaluate_runtime_expression(
            text,
            current_fact,
            token,
            bindings,
            scalar_binding_fields);
        if (!value) return std::nullopt;
        if (auto number = numeric_value(*value)) return *number != 0.0;
        if (auto const* str = std::get_if<std::string>(&*value)) return !str->empty() && *str != "false" && *str != "0";
        return !std::holds_alternative<NilValue>(*value);
    }

    bool alpha_constraint_affected(ParsedConstraint const& c,
                                   rulesforge::ModifiedFieldsHint const* changed_fields) {
        if (!changed_fields) return true;
        if (c.right_arith_expr) return true;  // conservative: expression dependencies are not tracked
        if (c.left_field == "this" || c.left_field.empty()) return true;

        if (!c.cached_left_field_path.empty() && !c.cached_left_field_path[0].name.empty()) {
            return changed_fields_contains(changed_fields, c.cached_left_field_path[0].name);
        }

        size_t end = c.left_field.find_first_of(".[");
        std::string_view root = (end == std::string::npos)
            ? std::string_view(c.left_field)
            : std::string_view(c.left_field.data(), end);
        if (root.empty()) return true;
        return changed_fields_contains(changed_fields, root);
    }

    [[noreturn]] void throw_lhs_runtime_not_lowered(char const* feature) {
        std::ostringstream message;
        message << feature << " reached RETE runtime without native lowering";
        throw std::runtime_error(message.str());
    }

    [[noreturn]] void throw_lhs_runtime_predicate_failed(
        rulesforge::RuntimePredicateRef const& predicate,
        char const* feature) {
        if (predicate.backend == rulesforge::RuntimePredicateBackend::Native) {
            std::ostringstream message;
            message << feature << " native";
            if (!predicate.external_name.empty()) {
                message << " '" << predicate.external_name << "'";
            }
            message << " predicate failed at runtime";
            throw std::runtime_error(message.str());
        }
        throw_lhs_runtime_not_lowered(feature);
    }

    void print_join_constraints_with_runtime_predicates(
        std::ostream& os,
        std::vector<ParsedConstraint> const& joins,
        std::vector<std::optional<rulesforge::RuntimePredicateRef>> const& runtime_predicates) {
        for (std::size_t index = 0; index < joins.size(); ++index) {
            os << "\\n" << constraint_to_string(joins[index]);
            if (index < runtime_predicates.size() && runtime_predicates[index]) {
                auto const& predicate = *runtime_predicates[index];
                os << " [mir#" << static_cast<int>(predicate.kind) << ":" << predicate.predicate_id << "]";
            }
        }
    }

    bool check_all_join_conditions(StatefulSession& session, Token const& token, Fact const& fact,
                                   std::vector<ParsedConstraint> const& joins,
                                   std::map<std::string, int> const& bindings,
                                   std::map<std::string, std::string> const& scalar_binding_fields = {},
                                   std::vector<std::optional<rulesforge::RuntimePredicateRef>> const* runtime_predicates = nullptr) {
        g_join_checks.fetch_add(1, std::memory_order_relaxed);
        if (joins.empty()) { return true; }

        for (std::size_t join_index = 0; join_index < joins.size(); ++join_index) {
            auto const& join = joins[join_index];
            auto const* runtime_predicate = (runtime_predicates != nullptr
                && join_index < runtime_predicates->size())
                ? &(*runtime_predicates)[join_index]
                : nullptr;
            if (join.temporal_constraint) {
                auto const& tc = *join.temporal_constraint;
                auto lhs_val_opt = fact.get_field(tc.lhs_field);
                g_field_lookups.fetch_add(1, std::memory_order_relaxed);
                auto it = bindings.find(tc.rhs_binding_and_field.first);
                if (it == bindings.end()) return false;

                auto bound_fact = token.get_fact_at_depth(it->second);   // Get fact directly from token
                if (!bound_fact) return false;
                auto rhs_val_opt = bound_fact->get_field(tc.rhs_binding_and_field.second);
                g_field_lookups.fetch_add(1, std::memory_order_relaxed);

                if (!lhs_val_opt || !rhs_val_opt) return false;
                auto lhs_ts = std::get_if<int64_t>(&*lhs_val_opt);
                auto rhs_ts = std::get_if<int64_t>(&*rhs_val_opt);
                if (!lhs_ts || !rhs_ts) return false;
                if (!evaluate_temporal(tc.op, *lhs_ts, *rhs_ts, tc.window_ms)) return false;
                continue;
            }
            std::optional<ConstraintValue> lhs_val_opt;
            std::optional<ConstraintValue> rhs_val_opt;

            if (join.left_binding) {
                auto it = bindings.find(*join.left_binding);
                if (it == bindings.end()) return false;

                auto bound_fact = token.get_fact_at_depth(it->second);
                if (!bound_fact) {
                    // Current-pattern binding can legally point past current token depth.
                    // In that case, resolve it against the currently tested fact.
                    if (it->second >= token.get_depth()) {
                        bound_fact = const_cast<Fact*>(&fact);
                    } else {
                        return false;
                    }
                }

                // Use cached path if available
                if (!join.cached_left_field_path.empty()) {
                    lhs_val_opt = bound_fact->get_field(join.cached_left_field_path);
                } else {
                    lhs_val_opt = bound_fact->get_field(join.left_field);
                }
                g_field_lookups.fetch_add(1, std::memory_order_relaxed);

            } else {
                if (!join.cached_left_field_path.empty()) {
                    lhs_val_opt = fact.get_field(join.cached_left_field_path);
                } else {
                    lhs_val_opt = fact.get_field(join.left_field);
                }
                g_field_lookups.fetch_add(1, std::memory_order_relaxed);
            }

            if (join.right_bound_field) {
                auto it = bindings.find(join.right_bound_field->first);
                if (it == bindings.end()) return false;

                auto bound_fact = token.get_fact_at_depth(it->second);
                if (!bound_fact) {
                    // Match LHS binding resolution: allow current-pattern binding on RHS.
                    if (it->second >= token.get_depth()) {
                        bound_fact = const_cast<Fact*>(&fact);
                    } else {
                        return false;
                    }
                }

                // Use cached RHS path if available
                if (!join.cached_right_field_path.empty()) {
                    rhs_val_opt = bound_fact->get_field(join.cached_right_field_path);
                } else {
                    rhs_val_opt = bound_fact->get_field(join.right_bound_field->second);
                }
                g_field_lookups.fetch_add(1, std::memory_order_relaxed);

            } else if (join.right_arith_expr) {
                if (!lhs_val_opt) {
                    return false;
                }
                auto rhs_expr_value = evaluate_runtime_expression(
                    *join.right_arith_expr,
                    fact,
                    &token,
                    &bindings,
                    &scalar_binding_fields);
                if (!rhs_expr_value) {
                    return false;
                }
                rhs_val_opt = std::move(rhs_expr_value);
            } else if (join.right_literal) {
                rhs_val_opt = join.right_literal;
            } else {
                continue;   // Should not happen for a join constraint
            }

            if (!lhs_val_opt || !rhs_val_opt) {
                return false;
            }
            ConstraintValue lhs = *lhs_val_opt;
            ConstraintValue rhs = *rhs_val_opt;
            (void)runtime_predicate;
            if (!evaluate_compare(join.op, lhs, rhs)) {
                return false;
            }
        }

        return true;
    }

    std::optional<ConstraintValue> resolve_constraint_field(
        ParsedConstraint const& constraint,
        bool left_side,
        Fact const& current_fact,
        Token const& token,
        std::map<std::string, int> const& bindings) {
        std::optional<std::string> binding;
        std::string field;
        std::vector<PathSegment> const* cached_path = nullptr;

        if (left_side) {
            binding = constraint.left_binding;
            field = constraint.left_field;
            cached_path = &constraint.cached_left_field_path;
        } else if (constraint.right_bound_field) {
            binding = constraint.right_bound_field->first;
            field = constraint.right_bound_field->second;
            cached_path = &constraint.cached_right_field_path;
        } else {
            return std::nullopt;
        }

        Fact const* source_fact = &current_fact;
        if (binding) {
            auto it = bindings.find(*binding);
            if (it == bindings.end()) {
                return std::nullopt;
            }
            auto bound_fact = token.get_fact_at_depth(it->second);
            if (bound_fact) {
                source_fact = bound_fact;
            } else if (it->second < token.get_depth()) {
                return std::nullopt;
            }
        }

        if (field == "this") {
            return ConstraintValue{static_cast<int64_t>(source_fact->id)};
        }
        if (cached_path != nullptr && !cached_path->empty()) {
            return source_fact->get_field(*cached_path);
        }
        return source_fact->get_field(field);
    }

    bool check_all_runtime_constraints(
        StatefulSession& session,
        Token const& token,
        Fact const& fact,
        std::vector<ParsedConstraint> const& constraints,
        std::map<std::string, int> const& bindings,
        std::vector<std::optional<rulesforge::RuntimePredicateRef>> const* runtime_predicates) {
        if (constraints.empty()) {
            return true;
        }

        auto kb = session.get_knowledge_base();

        for (std::size_t index = 0; index < constraints.size(); ++index) {
            auto const& constraint = constraints[index];
            if (constraint.op == CompareOp::None) {
                continue;
            }
            auto const* runtime_predicate = (runtime_predicates != nullptr
                && index < runtime_predicates->size())
                ? &(*runtime_predicates)[index]
                : nullptr;
            bool const can_use_runtime_predicate =
                runtime_predicate != nullptr && *runtime_predicate && kb && !(*runtime_predicate)->external_name.empty();

            if (constraint.temporal_constraint) {
                auto const& temporal = *constraint.temporal_constraint;
                auto lhs = fact.get_field(temporal.lhs_field);
                auto it = bindings.find(temporal.rhs_binding_and_field.first);
                if (!lhs || it == bindings.end()) {
                    return false;
                }
                auto bound_fact = token.get_fact_at_depth(it->second);
                if (!bound_fact && it->second >= token.get_depth()) {
                    bound_fact = const_cast<Fact*>(&fact);
                }
                if (!bound_fact) {
                    return false;
                }
                auto rhs = bound_fact->get_field(temporal.rhs_binding_and_field.second);
                if (!rhs) {
                    return false;
                }
                if (can_use_runtime_predicate) {
                    auto result = kb->runtime_predicate(*(*runtime_predicate), {*lhs, *rhs});
                    if (!result) {
                        throw_lhs_runtime_predicate_failed(*(*runtime_predicate), "unnest temporal predicate");
                    }
                    if (!*result) {
                        return false;
                    }
                    continue;
                }
                auto const* lhs_ts = std::get_if<std::int64_t>(&*lhs);
                auto const* rhs_ts = std::get_if<std::int64_t>(&*rhs);
                if (!lhs_ts || !rhs_ts) {
                    return false;
                }
                if (!evaluate_temporal(temporal.op, *lhs_ts, *rhs_ts, temporal.window_ms)) return false;
                continue;
            }

            auto lhs = resolve_constraint_field(constraint, true, fact, token, bindings);
            if (!lhs) {
                return false;
            }

            if (can_use_runtime_predicate) {
                switch ((*runtime_predicate)->kind) {
                    case rulesforge::RuntimePredicateKind::NumericLiteral:
                    case rulesforge::RuntimePredicateKind::ValueList: {
                        auto result = kb->runtime_predicate(*(*runtime_predicate), {*lhs});
                        if (!result) {
                            throw_lhs_runtime_predicate_failed(*(*runtime_predicate), "unnest unary predicate");
                        }
                        if (!*result) {
                            return false;
                        }
                        continue;
                    }
                    case rulesforge::RuntimePredicateKind::CollectionContains:
                        if (constraint.right_value_list) {
                            auto rhs_list = make_typed_list(*constraint.right_value_list);
                            auto result = kb->runtime_predicate(*(*runtime_predicate), {*lhs, rhs_list});
                            if (!result) {
                                throw_lhs_runtime_predicate_failed(
                                    *(*runtime_predicate),
                                    "unnest collection value-list predicate");
                            }
                            if (!*result) {
                                return false;
                            }
                            continue;
                        }
                        break;
                    default:
                        break;
                }
            }

            std::optional<ConstraintValue> rhs;
            if (constraint.right_bound_field) {
                rhs = resolve_constraint_field(constraint, false, fact, token, bindings);
            } else if (constraint.right_arith_expr) {
                rhs = evaluate_runtime_expression(*constraint.right_arith_expr, fact, &token, &bindings);
            } else if (constraint.right_literal) {
                rhs = constraint.right_literal;
            } else {
                rhs = ConstraintValue{NilValue{}};
            }
            if (!rhs) {
                return false;
            }

            if (can_use_runtime_predicate) {
                auto result = kb->runtime_predicate(*(*runtime_predicate), {*lhs, *rhs});
                if (!result) {
                    throw_lhs_runtime_predicate_failed(*(*runtime_predicate), "unnest binary predicate");
                }
                if (!*result) {
                    return false;
                }
                continue;
            }
            if (!evaluate_compare(constraint.op, *lhs, *rhs)) {
                return false;
            }
        }

        return true;
    }

    template <typename T>
    void remove_from_vector(std::vector<T>& vec, T const& item) {
        auto it = std::find(vec.begin(), vec.end(), item);
        if (it != vec.end()) {
            *it = std::move(vec.back());  // O(1) swap-and-pop instead of O(n) shift
            vec.pop_back();
        }
    }
}   // namespace

namespace rulesforge::rete_prof {
void reset_stats() {
    g_alpha_checks.store(0, std::memory_order_relaxed);
    g_join_checks.store(0, std::memory_order_relaxed);
    g_compare_calls.store(0, std::memory_order_relaxed);
    g_field_lookups.store(0, std::memory_order_relaxed);
}

Stats get_stats() {
    Stats s;
    s.alpha_checks = g_alpha_checks.load(std::memory_order_relaxed);
    s.join_checks = g_join_checks.load(std::memory_order_relaxed);
    s.compare_calls = g_compare_calls.load(std::memory_order_relaxed);
    s.field_lookups = g_field_lookups.load(std::memory_order_relaxed);
    return s;
}
}  // namespace rulesforge::rete_prof

// --- ReteNode ---
void ReteNode::add_child(std::shared_ptr<ReteNode> const& child) {
    if (child) {
        children.push_back(child);
        children_raw.push_back(child.get());
        child->parents.push_back(shared_from_this());
    }
}

void ReteNode::add_parent(std::shared_ptr<ReteNode> const& parent) {
    if (parent) {
        parents.push_back(parent);
    }
}

// --- BetaConditionNode ---
BetaConditionNode::BetaConditionNode(NodeKind k, std::vector<ParsedConstraint> const& joins,
                                     std::map<std::string, int> const& bindings,
                                     std::map<std::string, std::string> scalar_binding_fields,
                                     std::vector<std::optional<rulesforge::RuntimePredicateRef>> runtime_predicates_) :
    ReteNode(k),
    join_constraints(joins),
    binding_to_token_idx(bindings),
    scalar_binding_to_field(std::move(scalar_binding_fields)),
    runtime_predicates(std::move(runtime_predicates_)) {
        // Pre-parse paths for join constraints
        for (auto& join : join_constraints) {
            if (join.left_field.find('.') != std::string::npos || join.left_field.find('[') != std::string::npos) {
                join.cached_left_field_path = parse_field_path(join.left_field);
            }
            if (join.right_bound_field) {
                auto const& r_field = join.right_bound_field->second;
                if (r_field.find('.') != std::string::npos || r_field.find('[') != std::string::npos) {
                    join.cached_right_field_path = parse_field_path(r_field);
                }
                if (!left_hash_key_ && join.op == CompareOp::EQ) {
                    auto const binding = binding_to_token_idx.find(join.right_bound_field->first);
                    if (binding != binding_to_token_idx.end()) {
                        left_hash_key_ = std::pair{r_field, binding->second};
                        right_hash_key_ = join.left_field;
                    }
                }
            }
        }
    }

std::optional<ConstraintValue> BetaConditionNode::get_hash_key(Token const& token) const {
    if (!left_hash_key_) return std::nullopt;
    auto const* fact = token.get_fact_at_depth(left_hash_key_->second);
    if (!fact) return std::nullopt;
    return fact->get_field(left_hash_key_->first);
}

std::optional<ConstraintValue> BetaConditionNode::get_hash_key(Fact const* fact) const {
    if (!left_hash_key_ || !fact) return std::nullopt;
    return fact->get_field(right_hash_key_);
}

void BetaConditionNode::propagate_condition_transition(StatefulSession& session,
                                                        TokenWME const* wme,
                                                        size_t old_match_count,
                                                        size_t new_match_count) {
    bool const passed_before = was_passing(old_match_count);
    bool const passes_now = condition_passes(new_match_count);
    if (passed_before && !passes_now) {
        Token token{wme, PropagationType::RETRACT};
        for (auto* child : children_raw) { child->left_activate(session, token); }
    } else if (!passed_before && passes_now) {
        Token token{wme, PropagationType::ASSERT};
        for (auto* child : children_raw) { child->left_activate(session, token); }
    }
}

void BetaConditionNode::left_activate(StatefulSession& session, Token const& token) {
    auto& mem = session.net_mem().beta_condition[mem_slot];

    if (token.type == PropagationType::RETRACT) {
        auto const item = mem.left.find(token.wme);
        if (item != mem.left.end()) {
            auto const key = mem.left_keys.find(token.wme);
            if (key != mem.left_keys.end()) {
                auto bucket = mem.left_index.find(key->second);
                if (bucket != mem.left_index.end()) {
                    remove_from_vector(bucket->second, token.wme);
                    if (bucket->second.empty()) mem.left_index.erase(bucket);
                }
                mem.left_keys.erase(key);
            } else {
                remove_from_vector(mem.left_unindexed, token.wme);
            }
            mem.left.erase(item);
            for (auto* child : children_raw) { child->left_activate(session, token); }
        }
        return;
    }

    NetworkMemory::BetaConditionMem::LeftMemoryItem new_item;
    new_item.wme = token.wme;
    auto const key = get_hash_key(token);

    auto record_match = [&](Fact* fact) {
        if (check_all_join_conditions(session, token, *fact, join_constraints, binding_to_token_idx,
                                      scalar_binding_to_field, &runtime_predicates)) {
            new_item.matched_fact_ids.insert(fact->id);
        }
    };

    if (left_hash_key_ && key) {
        if (auto const bucket = mem.right_index.find(*key); bucket != mem.right_index.end()) {
            for (auto* fact : bucket->second) record_match(fact);
        }
        for (auto* fact : mem.right_unindexed) record_match(fact);
    } else {
        for (auto const& [fact_id, fact] : mem.right) record_match(fact);
    }

    auto const match_count = new_item.matched_fact_ids.size();
    mem.left[token.wme] = std::move(new_item);
    if (left_hash_key_ && key) {
        mem.left_index[*key].push_back(token.wme);
        mem.left_keys.emplace(token.wme, *key);
    } else if (left_hash_key_) {
        mem.left_unindexed.push_back(token.wme);
    }

    if (condition_passes(match_count)) {
        for (auto* child : children_raw) { child->left_activate(session, token); }
    }
}

void BetaConditionNode::right_activate(StatefulSession& session, Fact* fact, PropagationType p_type) {
    auto& mem = session.net_mem().beta_condition[mem_slot];
    if (!fact) return;

    bool const existed = mem.right.find(fact->id) != mem.right.end();
    if (p_type == PropagationType::RETRACT && !existed) return;

    std::optional<ConstraintValue> old_key;
    if (auto const key = mem.right_keys.find(fact->id); key != mem.right_keys.end()) {
        old_key = key->second;
    }
    auto const new_key = p_type == PropagationType::RETRACT ? std::nullopt : get_hash_key(fact);

    auto append_candidates = [&](std::optional<ConstraintValue> const& key,
                                 std::unordered_set<TokenWME const*>& out) {
        if (!left_hash_key_ || !key) {
            for (auto const& [wme, item] : mem.left) out.insert(wme);
            return;
        }
        if (auto const bucket = mem.left_index.find(*key); bucket != mem.left_index.end()) {
            out.insert(bucket->second.begin(), bucket->second.end());
        }
        out.insert(mem.left_unindexed.begin(), mem.left_unindexed.end());
    };

    std::unordered_set<TokenWME const*> old_candidates;
    std::unordered_set<TokenWME const*> new_candidates;
    if (existed) append_candidates(old_key, old_candidates);
    if (p_type != PropagationType::RETRACT) append_candidates(new_key, new_candidates);
    auto affected = old_candidates;
    affected.insert(new_candidates.begin(), new_candidates.end());

    for (auto const* wme : affected) {
        auto item_it = mem.left.find(wme);
        if (item_it == mem.left.end()) continue;
        auto& item = item_it->second;
        size_t const old_match_count = item.matched_fact_ids.size();
        if (existed) item.matched_fact_ids.erase(fact->id);
        if (p_type != PropagationType::RETRACT && new_candidates.contains(wme)) {
            Token token{item.wme, PropagationType::ASSERT};
            if (check_all_join_conditions(session, token, *fact, join_constraints,
                                          binding_to_token_idx, scalar_binding_to_field, &runtime_predicates)) {
                item.matched_fact_ids.insert(fact->id);
            }
        }
        propagate_condition_transition(session, item.wme, old_match_count,
                                       item.matched_fact_ids.size());
    }

    if (existed) {
        if (old_key) {
            auto bucket = mem.right_index.find(*old_key);
            if (bucket != mem.right_index.end()) {
                remove_from_vector(bucket->second, fact);
                if (bucket->second.empty()) mem.right_index.erase(bucket);
            }
            mem.right_keys.erase(fact->id);
        } else if (left_hash_key_) {
            remove_from_vector(mem.right_unindexed, fact);
        }
    }

    if (p_type == PropagationType::RETRACT) {
        mem.right.erase(fact->id);
    } else {
        mem.right[fact->id] = fact;
        if (left_hash_key_ && new_key) {
            mem.right_index[*new_key].push_back(fact);
            mem.right_keys.emplace(fact->id, *new_key);
        } else if (left_hash_key_) {
            mem.right_unindexed.push_back(fact);
        }
    }
}

void BetaConditionNode::right_activate_batch(StatefulSession& session, std::vector<Fact*>& facts, PropagationType p_type) {
    for (auto* fact : facts) right_activate(session, fact, p_type);
}

void BetaConditionNode::right_activate_deferred(StatefulSession& session, Fact* fact, PropagationType p_type) {
    auto& mem = session.net_mem().beta_condition[mem_slot];
    mem.pending_facts.push_back(fact);
    mem.dirty = true;
}

void BetaConditionNode::right_activate_batch_deferred(StatefulSession& session, std::vector<Fact*>& facts, PropagationType p_type) {
    auto& mem = session.net_mem().beta_condition[mem_slot];
    mem.pending_facts.insert(mem.pending_facts.end(), facts.begin(), facts.end());
    mem.dirty = true;
}

bool BetaConditionNode::flush_pending(StatefulSession& session) {
    auto& mem = session.net_mem().beta_condition[mem_slot];
    if (!mem.dirty) return false;
    mem.dirty = false;
    auto pending = std::move(mem.pending_facts);
    mem.pending_facts.clear();
    right_activate_batch(session, pending, PropagationType::ASSERT);
    return true;
}

// --- AlphaNode ---
AlphaNode::AlphaNode(ParsedConstraint const& c) :
    AlphaNode(c, std::nullopt) {}

AlphaNode::AlphaNode(ParsedConstraint const& c,
                     std::optional<rulesforge::RuntimePredicateRef> runtime_predicate_ref) :
    ReteNode(NodeKind::Alpha), constraint(c),
    runtime_predicate_ref_(std::move(runtime_predicate_ref)) {
    // Pre-parse path if complex
    if (constraint.left_field.find('.') != std::string::npos || constraint.left_field.find('[') != std::string::npos) {
        constraint.cached_left_field_path = parse_field_path(constraint.left_field);
    }
}

void AlphaNode::left_activate(StatefulSession&, Token const&) {}

void AlphaNode::right_activate(StatefulSession& session, Fact* fact, PropagationType p_type) {
    if (!fact) return;

    auto& mem = session.net_mem().alpha[mem_slot];
    bool const was_passing = mem.passing_facts.count(fact->id) > 0;

    if (p_type == PropagationType::RETRACT) {
        if (!was_passing) return;
        mem.passing_facts.erase(fact->id);
        for (auto* child : children_raw) { child->right_activate(session, fact, PropagationType::RETRACT); }
        return;
    }

    if (p_type == PropagationType::MODIFY) {
        auto const* changed_fields = session.current_modified_fields();
        if (changed_fields && !alpha_constraint_affected(constraint, changed_fields)) {
            if (!was_passing) return;
            for (auto* child : children_raw) { child->right_activate(session, fact, PropagationType::MODIFY); }
            return;
        }
    }

    bool const passes = check_constraint(session, *fact);
    if (!passes) {
        if (!was_passing) return;
        mem.passing_facts.erase(fact->id);
        for (auto* child : children_raw) { child->right_activate(session, fact, PropagationType::RETRACT); }
        return;
    }

    mem.passing_facts.insert(fact->id);
    PropagationType child_type = p_type;
    if (p_type == PropagationType::MODIFY && !was_passing) {
        child_type = PropagationType::ASSERT;
    }
    for (auto* child : children_raw) { child->right_activate(session, fact, child_type); }
}

void AlphaNode::right_activate_batch(StatefulSession& session, std::vector<Fact*>& facts, PropagationType p_type) {
    if (p_type != PropagationType::ASSERT) {
        for (auto* fact : facts) {
            right_activate(session, fact, p_type);
        }
        return;
    }
    if (children_raw.size() == 1) {
        // Fast path: single child — filter in-place, zero allocation
        auto& mem = session.net_mem().alpha[mem_slot];
        size_t write = 0;
        for (size_t read = 0; read < facts.size(); ++read) {
            if (check_constraint(session, *facts[read])) {
                mem.passing_facts.insert(facts[read]->id);
                facts[write++] = facts[read];
            }
        }
        if (write == 0) return;
        facts.resize(write);
        children_raw[0]->right_activate_batch(session, facts, p_type);
    } else {
        // Multiple children: need a copy
        auto& mem = session.net_mem().alpha[mem_slot];
        std::vector<Fact*> survivors;
        survivors.reserve(facts.size());
        for (auto* fact : facts) {
            if (check_constraint(session, *fact)) {
                mem.passing_facts.insert(fact->id);
                survivors.push_back(fact);
            }
        }
        if (survivors.empty()) return;
        for (auto* child : children_raw) { child->right_activate_batch(session, survivors, p_type); }
    }
}

void AlphaNode::right_activate_deferred(StatefulSession& session, Fact* fact, PropagationType p_type) {
    if (p_type != PropagationType::ASSERT) {
        right_activate(session, fact, p_type);
        return;
    }
    if (check_constraint(session, *fact)) {
        session.net_mem().alpha[mem_slot].passing_facts.insert(fact->id);
        for (auto* child : children_raw) { child->right_activate_deferred(session, fact, p_type); }
    }
}

void AlphaNode::right_activate_batch_deferred(StatefulSession& session, std::vector<Fact*>& facts, PropagationType p_type) {
    if (p_type != PropagationType::ASSERT) {
        for (auto* fact : facts) {
            right_activate(session, fact, p_type);
        }
        return;
    }

    std::vector<Fact*> survivors;
    survivors.reserve(facts.size());
    auto& mem = session.net_mem().alpha[mem_slot];
    for (auto* fact : facts) {
        if (check_constraint(session, *fact)) {
            mem.passing_facts.insert(fact->id);
            survivors.push_back(fact);
        }
    }
    if (survivors.empty()) return;
    for (auto* child : children_raw) { child->right_activate_batch_deferred(session, survivors, p_type); }
}

bool AlphaNode::check_constraint(StatefulSession const& session, Fact const& fact) const {
    g_alpha_checks.fetch_add(1, std::memory_order_relaxed);
    // A constraint with an empty operator is a pure binding (like `$id: id`)
    // or an existence check (`name`).
    if (constraint.op == CompareOp::None) {
        // If there's no right literal, it's a pure binding. It should always pass
        // the alpha check, as the binding itself is handled elsewhere.
        if (!constraint.right_literal.has_value()) {
            return true; // Fast return (removed logd for perf)
        }
        // Otherwise, it's an existence check that was transformed to `field == 1`.
        // This will be handled by the main comparison logic below.
    }

    ConstraintValue const* lhs_ptr = nullptr;
    std::optional<ConstraintValue> temp_lhs; // Keep alive if returned by value from complex get_field

    if (constraint.cached_left_field_path.empty()) {
        // Fast path for simple fields
        if (constraint.left_field == "this") {
            temp_lhs = static_cast<int64_t>(fact.id);
            lhs_ptr = &*temp_lhs;
        } else {
            // Direct map lookup - zero copy, no string scanning
            auto it = fact.fields.find(constraint.left_field);
            g_field_lookups.fetch_add(1, std::memory_order_relaxed);
            if (it != fact.fields.end()) {
                lhs_ptr = &it->second;
            }
        }
    } else {
        // Complex path
        temp_lhs = fact.get_field(constraint.cached_left_field_path);
        g_field_lookups.fetch_add(1, std::memory_order_relaxed);
        if (temp_lhs) lhs_ptr = &*temp_lhs;
    }

    if (!lhs_ptr) {
        // Field not found - fail
        return false;
    }

    ConstraintValue const& lhs = *lhs_ptr;
    auto kb = session.get_knowledge_base();

    if (!runtime_predicate_ref_) {
        static const ConstraintValue nil_value{NilValue{}};
        std::optional<ConstraintValue> rhs_expr;
        if (constraint.right_arith_expr) {
            rhs_expr = evaluate_runtime_expression(*constraint.right_arith_expr, fact, nullptr, nullptr);
            if (!rhs_expr) return false;
        }
        ConstraintValue const& rhs = rhs_expr.has_value()
            ? *rhs_expr
            : (constraint.right_literal.has_value() ? *constraint.right_literal : nil_value);
        return evaluate_compare(constraint.op, lhs, rhs);
    }

    if (!kb || runtime_predicate_ref_->external_name.empty()) {
        static const ConstraintValue nil_value{NilValue{}};
        std::optional<ConstraintValue> rhs_expr;
        if (constraint.right_arith_expr) {
            rhs_expr = evaluate_runtime_expression(*constraint.right_arith_expr, fact, nullptr, nullptr);
            if (!rhs_expr) return false;
        }
        ConstraintValue const& rhs = rhs_expr.has_value()
            ? *rhs_expr
            : (constraint.right_literal.has_value() ? *constraint.right_literal : nil_value);
        return evaluate_compare(constraint.op, lhs, rhs);
    }

    switch (runtime_predicate_ref_->kind) {
        case rulesforge::RuntimePredicateKind::NumericLiteral:
        case rulesforge::RuntimePredicateKind::ValueList: {
            auto result = kb->runtime_predicate(*runtime_predicate_ref_, {lhs});
            if (!result) {
                throw_lhs_runtime_predicate_failed(*runtime_predicate_ref_, "alpha unary predicate");
            }
            return *result;
        }
        case rulesforge::RuntimePredicateKind::CollectionContains:
            if (constraint.right_value_list) {
                auto rhs_list = make_typed_list(*constraint.right_value_list);
                auto result = kb->runtime_predicate(*runtime_predicate_ref_, {lhs, rhs_list});
                if (!result) {
                    throw_lhs_runtime_predicate_failed(
                        *runtime_predicate_ref_,
                        "alpha collection value-list predicate");
                }
                return *result;
            }
            {
                static const ConstraintValue nil_value{NilValue{}};
                ConstraintValue const& rhs = constraint.right_literal.has_value() ? *constraint.right_literal : nil_value;
                auto result = kb->runtime_predicate(*runtime_predicate_ref_, {lhs, rhs});
                if (!result) {
                    throw_lhs_runtime_predicate_failed(*runtime_predicate_ref_, "alpha binary predicate");
                }
                return *result;
            }
        default: {
            static const ConstraintValue nil_value{NilValue{}};
            ConstraintValue const& rhs = constraint.right_literal.has_value() ? *constraint.right_literal : nil_value;
            auto result = kb->runtime_predicate(*runtime_predicate_ref_, {lhs, rhs});
            if (!result) {
                throw_lhs_runtime_predicate_failed(*runtime_predicate_ref_, "alpha binary predicate");
            }
            return *result;
        }
    }
}

void AlphaNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"AlphaNode (" << id << ")\\n";
    if (!constraint.left_field.empty()) {
        os << constraint.left_field << " " << compare_op_str(constraint.op) << " "
           << ::to_string(constraint.right_literal.value_or(NilValue{}));
        if (runtime_predicate_ref_) {
            os << "\\nRuntime predicate: " << static_cast<int>(runtime_predicate_ref_->kind)
               << "#" << runtime_predicate_ref_->predicate_id;
        }
    } else {
        os << "(No Constraint)";
    }
    os << "\", shape=ellipse, style=filled, fillcolor=orange];";
}

// --- EntryPointNode ---
void EntryPointNode::left_activate(StatefulSession&, Token const&) {
    // An EntryPointNode is the start of an alpha chain. It does not receive left activations.
}

void EntryPointNode::right_activate(StatefulSession& session, Fact* fact, PropagationType p_type) {
    for (auto* child : children_raw) { child->right_activate(session, fact, p_type); }
}

void EntryPointNode::right_activate_batch(StatefulSession& session, std::vector<Fact*>& facts, PropagationType p_type) {
    for (auto* child : children_raw) { child->right_activate_batch(session, facts, p_type); }
}

void EntryPointNode::right_activate_deferred(StatefulSession& session, Fact* fact, PropagationType p_type) {
    for (auto* child : children_raw) { child->right_activate_deferred(session, fact, p_type); }
}

void EntryPointNode::right_activate_batch_deferred(StatefulSession& session, std::vector<Fact*>& facts, PropagationType p_type) {
    for (auto* child : children_raw) { child->right_activate_batch_deferred(session, facts, p_type); }
}

void EntryPointNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"Entry Point (" << id << ")\", shape=house, style=filled, fillcolor=yellow];";
}

// --- BaseJoinNode ---
BaseJoinNode::BaseJoinNode(NodeKind k,
                           std::vector<ParsedConstraint> joins,
                           std::map<std::string, int> bindings,
                           std::map<std::string, std::string> scalar_binding_fields,
                           std::vector<std::optional<rulesforge::RuntimePredicateRef>> runtime_predicates) :
    ReteNode(k),
    join_constraints_(std::move(joins)),
    binding_to_token_idx_(std::move(bindings)),
    scalar_binding_to_field_(std::move(scalar_binding_fields)),
    runtime_predicates_(std::move(runtime_predicates)) {}

void BaseJoinNode::propagate_assert(StatefulSession& session, Token const& token, Fact* fact,
                                    ChildMap& left_to_children,
                                    RightChildMap& /*right_to_children*/) {
    auto new_wme = session.get_or_create_wme(token.wme, fact);
    left_to_children[token.wme].push_back(new_wme);
    // PHREAK: skip right_to_children — retract finds WMEs via left_to_children scan
    Token new_token{new_wme, PropagationType::ASSERT};
    for (auto* c : children_raw) { c->left_activate(session, new_token); }
}

void BaseJoinNode::propagate_retract(StatefulSession& session, TokenWME const* wme, Fact* fact,
                                     ChildMap& left_to_children,
                                     RightChildMap& /*right_to_children*/) {
    auto it_left = left_to_children.find(wme);
    if (it_left == left_to_children.end()) return;

    TokenWME const* child_to_retract = nullptr;
    for (auto const& child_wme : it_left->second) {
        if (child_wme->fact->id == fact->id) {
            child_to_retract = child_wme;
            break;
        }
    }

    if (child_to_retract) {
        Token retract_token{child_to_retract, PropagationType::RETRACT};
        for (auto* c : children_raw) { c->left_activate(session, retract_token); }
        remove_from_vector(it_left->second, child_to_retract);
        if (it_left->second.empty()) left_to_children.erase(it_left);

        // Invalidate WME cache so that subsequent ASSERT can create a fresh WME
        session.invalidate_wme_cache(child_to_retract->hash);
    }
}

// --- HashedJoinNode ---
HashedJoinNode::HashedJoinNode(std::vector<ParsedConstraint> joins,
                               std::map<std::string, int> bindings,
                               std::map<std::string, std::string> scalar_binding_fields,
                               std::vector<std::optional<rulesforge::RuntimePredicateRef>> runtime_predicates,
                               std::pair<std::string, int> left_hash_key, std::string right_hash_key) :
    BaseJoinNode(NodeKind::HashedJoin,
                 std::move(joins),
                 std::move(bindings),
                 std::move(scalar_binding_fields),
                 std::move(runtime_predicates)),
    left_hash_key_(std::move(left_hash_key)),
    right_hash_key_(std::move(right_hash_key)) {}

std::optional<ConstraintValue> HashedJoinNode::get_key(Token const& token) const {
    auto fact_at_depth = token.get_fact_at_depth(left_hash_key_.second);
    if (!fact_at_depth) return std::nullopt;
    return fact_at_depth->get_field(left_hash_key_.first);
}

std::optional<ConstraintValue> HashedJoinNode::get_key(Fact const* fact) const {
    return fact->get_field(right_hash_key_);
}

void HashedJoinNode::left_activate(StatefulSession& session, Token const& token) {
    auto& mem = session.net_mem().hashed_join[mem_slot];
    auto key_opt = get_key(token);
    if (!key_opt) return;
    auto const& key = *key_opt;

    if (token.type == PropagationType::RETRACT) {
        auto mem_it = mem.left.find(key);
        if (mem_it != mem.left.end()) {
            remove_from_vector(mem_it->second, token.wme);
            if (mem_it->second.empty()) mem.left.erase(mem_it);
        }
        auto fact_it = mem.right.find(key);
        if (fact_it != mem.right.end()) {
            for (auto const& fact : fact_it->second) { propagate_retract(session, token.wme, fact, mem.left_to_children, mem.right_to_children); }
        }
        return;
    }

    mem.left[key].push_back(token.wme);
    auto it_right = mem.right.find(key);
    if (it_right != mem.right.end()) {
        for (auto const& fact : it_right->second) {
            if (check_all_join_conditions(session, token, *fact, join_constraints_, binding_to_token_idx_,
                                          scalar_binding_to_field_, &runtime_predicates_)) {
                propagate_assert(session, token, fact, mem.left_to_children, mem.right_to_children);
            }
        }
    }
}

void HashedJoinNode::right_activate(StatefulSession& session, Fact* fact, PropagationType p_type) {
    auto& mem = session.net_mem().hashed_join[mem_slot];
    auto key_opt = get_key(fact);
    if (!key_opt) return;
    auto const& key = *key_opt;

    if (p_type == PropagationType::RETRACT) {
        auto mem_it = mem.right.find(key);
        if (mem_it != mem.right.end()) {
            remove_from_vector(mem_it->second, fact);
            if (mem_it->second.empty()) mem.right.erase(mem_it);
        }
        auto token_it = mem.left.find(key);
        if (token_it != mem.left.end()) {
            for (auto const& wme : token_it->second) { propagate_retract(session, wme, fact, mem.left_to_children, mem.right_to_children); }
        }
        return;
    }

    // For MODIFY: first retract old matches, then assert new ones
    if (p_type == PropagationType::MODIFY) {
        auto mem_it = mem.right.find(key);
        if (mem_it != mem.right.end()) {
            // Check if fact is in the memory for this key
            auto fact_it = std::find(mem_it->second.begin(), mem_it->second.end(), fact);
            if (fact_it != mem_it->second.end()) {
                auto token_it = mem.left.find(key);
                if (token_it != mem.left.end()) {
                    for (auto const& wme : token_it->second) { propagate_retract(session, wme, fact, mem.left_to_children, mem.right_to_children); }
                }
                // Remove from memory - will be re-added below
                remove_from_vector(mem_it->second, fact);
                if (mem_it->second.empty()) mem.right.erase(mem_it);
            }
        }
    }

    mem.right[key].push_back(fact);
    auto it_left = mem.left.find(key);
    if (it_left != mem.left.end()) {
        for (auto const& wme : it_left->second) {
            Token token{wme, PropagationType::ASSERT};
            if (check_all_join_conditions(session, token, *fact, join_constraints_, binding_to_token_idx_,
                                          scalar_binding_to_field_, &runtime_predicates_)) {
                propagate_assert(session, token, fact, mem.left_to_children, mem.right_to_children);
            }
        }
    }
}

void HashedJoinNode::right_activate_batch(StatefulSession& session, std::vector<Fact*>& facts, PropagationType p_type) {
    auto& mem = session.net_mem().hashed_join[mem_slot];

    // Index all incoming facts by hash key, store into right memory
    std::unordered_map<ConstraintValue, std::vector<Fact*>, ConstraintValueHasher, ConstraintValueEquals> facts_by_key;
    for (auto& fact : facts) {
        auto key_opt = get_key(fact);
        if (!key_opt) continue;
        mem.right[*key_opt].push_back(fact);
        facts_by_key[*key_opt].push_back(fact);
    }

    // Single pass over left memory per key bucket
    for (auto& [key, keyed_facts] : facts_by_key) {
        auto it_left = mem.left.find(key);
        if (it_left == mem.left.end()) continue;
        for (auto const& wme : it_left->second) {
            Token token{wme, PropagationType::ASSERT};
            for (auto& fact : keyed_facts) {
                if (check_all_join_conditions(session, token, *fact, join_constraints_, binding_to_token_idx_,
                                              scalar_binding_to_field_, &runtime_predicates_)) {
                    // Inline: uncached WME + skip right_to_children
                    auto new_wme = session.create_wme_uncached(token.wme, fact);
                    mem.left_to_children[token.wme].push_back(new_wme);
                    Token new_token{new_wme, PropagationType::ASSERT};
                    for (auto* c : children_raw) { c->left_activate(session, new_token); }
                }
            }
        }
    }
}

void HashedJoinNode::right_activate_deferred(StatefulSession& session, Fact* fact, PropagationType p_type) {
    auto& mem = session.net_mem().hashed_join[mem_slot];
    mem.pending_facts.push_back(fact);
    mem.dirty = true;
}

void HashedJoinNode::right_activate_batch_deferred(StatefulSession& session, std::vector<Fact*>& facts, PropagationType p_type) {
    auto& mem = session.net_mem().hashed_join[mem_slot];
    mem.pending_facts.insert(mem.pending_facts.end(), facts.begin(), facts.end());
    mem.dirty = true;
}

bool HashedJoinNode::flush_pending(StatefulSession& session) {
    auto& mem = session.net_mem().hashed_join[mem_slot];
    if (!mem.dirty) return false;
    mem.dirty = false;
    auto pending = std::move(mem.pending_facts);
    mem.pending_facts.clear();
    right_activate_batch(session, pending, PropagationType::ASSERT);
    return true;
}

void HashedJoinNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"HashedJoinNode (" << id << ")\\nIndex: " << right_hash_key_ << " == $"
       << left_hash_key_.second << "." << left_hash_key_.first;
    if (!join_constraints_.empty()) {
        os << "\\nJoins:";
        print_join_constraints_with_runtime_predicates(os,
                                            join_constraints_,
                                            runtime_predicates_);
    }
    os << "\", shape=box, style=filled, fillcolor=lightblue];";
}

// --- CrossProductJoinNode ---
CrossProductJoinNode::CrossProductJoinNode(std::vector<ParsedConstraint> joins,
                                           std::map<std::string, int> bindings,
                                           std::map<std::string, std::string> scalar_binding_fields,
                                           std::vector<std::optional<rulesforge::RuntimePredicateRef>> runtime_predicates) :
    BaseJoinNode(NodeKind::CrossProductJoin, std::move(joins), std::move(bindings), std::move(scalar_binding_fields),
                 std::move(runtime_predicates)) {}

void CrossProductJoinNode::left_activate(StatefulSession& session, Token const& token) {
    auto& mem = session.net_mem().cross_product_join[mem_slot];
    if (token.type == PropagationType::RETRACT) {
        if (mem.left.erase(token.wme) > 0) {
            for (auto const& [fact_id, fact] : mem.right) { propagate_retract(session, token.wme, fact, mem.left_to_children, mem.right_to_children); }
        }
        return;
    }

    mem.left[token.wme] = token.wme;
    for (auto const& [fact_id, fact] : mem.right) {
        if (check_all_join_conditions(session, token, *fact, join_constraints_, binding_to_token_idx_,
                                      scalar_binding_to_field_, &runtime_predicates_)) {
            propagate_assert(session, token, fact, mem.left_to_children, mem.right_to_children);
        }
    }
}

void CrossProductJoinNode::right_activate(StatefulSession& session, Fact* fact,
                                          PropagationType p_type) {
    auto& mem = session.net_mem().cross_product_join[mem_slot];

    if (p_type == PropagationType::RETRACT) {
        if (mem.right.erase(fact->id) > 0) {
            for (auto const& [ptr, wme] : mem.left) { propagate_retract(session, wme, fact, mem.left_to_children, mem.right_to_children); }
        }
        return;
    }

    // For MODIFY: first retract old matches, then assert new ones
    if (p_type == PropagationType::MODIFY && mem.right.count(fact->id) > 0) {
        for (auto const& [ptr, wme] : mem.left) { propagate_retract(session, wme, fact, mem.left_to_children, mem.right_to_children); }
    }

    mem.right[fact->id] = fact;
    for (auto const& [ptr, wme] : mem.left) {
        Token token{wme, PropagationType::ASSERT};
        if (check_all_join_conditions(session, token, *fact, join_constraints_, binding_to_token_idx_,
                                      scalar_binding_to_field_, &runtime_predicates_)) {
            propagate_assert(session, token, fact, mem.left_to_children, mem.right_to_children);
        }
    }
}

void CrossProductJoinNode::right_activate_batch(StatefulSession& session, std::vector<Fact*>& facts, PropagationType p_type) {
    auto& mem = session.net_mem().cross_product_join[mem_slot];

    // Store all facts into right memory first
    mem.right.reserve(mem.right.size() + facts.size());
    for (auto& fact : facts) {
        mem.right[fact->id] = fact;
    }

    // PHREAK: set-oriented propagation — batch WME creation + batch left_activate
    for (auto const& [ptr, wme] : mem.left) {
        Token token{wme, PropagationType::ASSERT};

        // Pre-reserve left_to_children vector for this token
        auto& child_vec = mem.left_to_children[token.wme];
        child_vec.reserve(child_vec.size() + facts.size());

        // Collect all matching tokens for batch propagation
        std::vector<Token> batch_tokens;
        batch_tokens.reserve(facts.size());

        for (auto& fact : facts) {
            if (check_all_join_conditions(session, token, *fact, join_constraints_, binding_to_token_idx_,
                                          scalar_binding_to_field_, &runtime_predicates_)) {
                auto new_wme = session.create_wme_uncached(token.wme, fact);
                child_vec.push_back(new_wme);
                batch_tokens.push_back(Token{new_wme, PropagationType::ASSERT});
            }
        }

        // Batch propagate to children
        if (!batch_tokens.empty()) {
            for (auto* c : children_raw) { c->left_activate_batch(session, batch_tokens); }
        }
    }
}

void CrossProductJoinNode::right_activate_deferred(StatefulSession& session, Fact* fact, PropagationType p_type) {
    auto& mem = session.net_mem().cross_product_join[mem_slot];
    mem.pending_facts.push_back(fact);
    mem.dirty = true;
}

void CrossProductJoinNode::right_activate_batch_deferred(StatefulSession& session, std::vector<Fact*>& facts, PropagationType p_type) {
    auto& mem = session.net_mem().cross_product_join[mem_slot];
    mem.pending_facts.insert(mem.pending_facts.end(), facts.begin(), facts.end());
    mem.dirty = true;
}

bool CrossProductJoinNode::flush_pending(StatefulSession& session) {
    auto& mem = session.net_mem().cross_product_join[mem_slot];
    if (!mem.dirty) return false;
    mem.dirty = false;
    auto pending = std::move(mem.pending_facts);
    mem.pending_facts.clear();
    right_activate_batch(session, pending, PropagationType::ASSERT);
    return true;
}

void CrossProductJoinNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"CrossProductJoinNode (" << id << ")";
    if (!join_constraints_.empty()) {
        os << "\\nJoins:";
        print_join_constraints_with_runtime_predicates(os,
                                            join_constraints_,
                                            runtime_predicates_);
    }
    os << "\", shape=box, style=filled, fillcolor=lightgrey];";
}

// --- NotNode ---
NotNode::NotNode(std::vector<ParsedConstraint> const& joins,
                 std::map<std::string, int> const& bindings,
                 std::map<std::string, std::string> scalar_binding_fields,
                 std::vector<std::optional<rulesforge::RuntimePredicateRef>> runtime_predicates_) :
    BetaConditionNode(NodeKind::Not,
                      joins,
                      bindings,
                      std::move(scalar_binding_fields),
                      std::move(runtime_predicates_)) {}

void NotNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"NotNode (" << id << ")";
    if (!join_constraints.empty()) {
        os << "\\nJoins:";
        print_join_constraints_with_runtime_predicates(os,
                                            join_constraints,
                                            runtime_predicates);
    }
    os << "\", shape=octagon, style=filled, fillcolor=salmon];";
}

// --- ExistsNode ---
ExistsNode::ExistsNode(std::vector<ParsedConstraint> const& joins,
                       std::map<std::string, int> const& bindings,
                       std::map<std::string, std::string> scalar_binding_fields,
                       std::vector<std::optional<rulesforge::RuntimePredicateRef>> runtime_predicates_) :
    BetaConditionNode(NodeKind::Exists,
                      joins,
                      bindings,
                      std::move(scalar_binding_fields),
                      std::move(runtime_predicates_)) {}

void ExistsNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"ExistsNode (" << id << ")";
    if (!join_constraints.empty()) {
        os << "\\nJoins:";
        print_join_constraints_with_runtime_predicates(os,
                                            join_constraints,
                                            runtime_predicates);
    }
    os << "\", shape=octagon, style=filled, fillcolor=khaki];";
}

// --- AccumulateNode ---
namespace {
    std::optional<ConstraintValue> get_accumulate_value(
        StatefulSession& session,
        ParsedAccumulate const& info,
        Fact const& fact,
        Token const* token,
        std::map<std::string, int> const* bindings)
    {
        if (info.function == "count" && !info.uses_runtime_value_expression && info.accumulate_field_name.empty()) {
            return ConstraintValue{int64_t{1}};
        }
        if (info.uses_runtime_value_expression) {
            return evaluate_runtime_expression(info.field, fact, token, bindings, &info.inline_binding_to_field);
        }
        // Otherwise use simple field lookup
        if (!info.accumulate_field_name.empty()) {
            return fact.get_field(info.accumulate_field_name);
        }
        return std::nullopt;
    }

    bool uses_numeric_accumulate(ParsedAccumulate const& info) {
        if (info.function == "count" && !info.uses_runtime_value_expression) {
            return true;
        }
        if (info.function != "sum"
            && info.function != "average"
            && info.function != "min"
            && info.function != "max") {
            return false;
        }
        return info.uses_runtime_value_expression || !info.accumulate_field_name.empty();
    }

    bool uses_collect_accumulate(ParsedAccumulate const& info) {
        return !info.uses_runtime_value_expression
            && (info.function == "collect" || info.function == "collectList" || info.function == "collectSet");
    }

    void reset_collect_accumulate(NetworkMemory::AccumulateMem::LeftMemoryItem& item) {
        item.contributing_facts_list.clear();
        item.contributing_facts_set.clear();
    }

    bool apply_collect_accumulate(
        StatefulSession& session,
        ParsedAccumulate const& info,
        NetworkMemory::AccumulateMem::LeftMemoryItem& item,
        Fact* fact,
        std::int64_t direction) {
        (void)session;
        if (info.function == "collectSet") {
            if (direction >= 0) {
                return item.contributing_facts_set.insert(fact).second;
            }
            return item.contributing_facts_set.erase(fact) > 0;
        } else if (info.function == "collect" || info.function == "collectList") {
            if (direction >= 0) {
                item.contributing_facts_list.push_back(fact);
                return true;
            }
            auto it = std::find(item.contributing_facts_list.begin(), item.contributing_facts_list.end(), fact);
            if (it == item.contributing_facts_list.end()) {
                return false;
            }
            item.contributing_facts_list.erase(it);
            return true;
        } else {
            throw_lhs_runtime_not_lowered("collect accumulate kernel");
        }
    }

    FactList collect_accumulate_result(
        StatefulSession& session,
        ParsedAccumulate const& info,
        NetworkMemory::AccumulateMem::LeftMemoryItem const& item) {
        (void)session;
        FactList result;
        if (info.function == "collectSet") {
            result.facts.assign(item.contributing_facts_set.begin(), item.contributing_facts_set.end());
        } else if (info.function == "collect" || info.function == "collectList") {
            result.facts = item.contributing_facts_list;
        } else {
            throw_lhs_runtime_not_lowered("collect accumulate result");
        }
        return result;
    }

    void reset_numeric_accumulate(NetworkMemory::AccumulateMem::LeftMemoryItem& item) {
        item.numeric_sum = 0.0;
        item.numeric_extreme = 0.0;
        item.aggregate_count = 0;
        item.sum_is_double = false;
        item.numeric_values.clear();
    }

    bool apply_numeric_accumulate(
        StatefulSession& session,
        ParsedAccumulate const& info,
        NetworkMemory::AccumulateMem::LeftMemoryItem& item,
        Fact const& fact,
        Token const* token,
        std::map<std::string, int> const* bindings,
        std::int64_t direction) {
        if (info.function == "count") {
            item.aggregate_count += direction;
            return true;
        }

        if (info.function == "sum" || info.function == "average") {
            auto value_opt = get_accumulate_value(session, info, fact, token, bindings);
            if (!value_opt) {
                return false;
            }
            if (!std::holds_alternative<int64_t>(*value_opt) && !std::holds_alternative<double>(*value_opt)) {
                return false;
            }
            double numeric_value = std::holds_alternative<int64_t>(*value_opt)
                ? static_cast<double>(std::get<int64_t>(*value_opt))
                : std::get<double>(*value_opt);
            item.numeric_sum += numeric_value * static_cast<double>(direction);
            if (info.function == "average") {
                item.aggregate_count += direction;
            }
            if (std::holds_alternative<double>(*value_opt)) {
                item.sum_is_double = true;
            }
            return true;
        }

        if (info.function == "min" || info.function == "max") {
            auto value_opt = get_accumulate_value(session, info, fact, token, bindings);
            if (!value_opt) {
                return false;
            }
            double numeric_value = 0.0;
            if (std::holds_alternative<int64_t>(*value_opt)) {
                numeric_value = static_cast<double>(std::get<int64_t>(*value_opt));
            } else if (std::holds_alternative<double>(*value_opt)) {
                numeric_value = std::get<double>(*value_opt);
                item.sum_is_double = true;
            } else {
                return false;
            }

            if (direction >= 0) {
                item.numeric_values.insert(numeric_value);
            } else {
                auto it = item.numeric_values.find(numeric_value);
                if (it != item.numeric_values.end()) {
                    item.numeric_values.erase(it);
                }
            }

            if (item.numeric_values.empty()) {
                item.numeric_extreme = 0.0;
                return true;
            }

            double result = *item.numeric_values.begin();
            for (auto it = std::next(item.numeric_values.begin());
                 it != item.numeric_values.end();
                 ++it) {
                result = (info.function == "min") ? std::min(result, *it) : std::max(result, *it);
            }
            item.numeric_extreme = result;
            return true;
        }

        throw_lhs_runtime_not_lowered("accumulate kernel");
    }

    ConstraintValue numeric_accumulate_result(
        ParsedAccumulate const& info,
        NetworkMemory::AccumulateMem::LeftMemoryItem const& item) {
        if (info.function == "count") {
            return item.aggregate_count;
        }
        if (info.function == "average") {
            if (item.aggregate_count == 0) {
                return 0.0;
            }
            return item.numeric_sum / static_cast<double>(item.aggregate_count);
        }
        if (info.function == "min" || info.function == "max") {
            if (item.sum_is_double) {
                return item.numeric_extreme;
            }
            return static_cast<int64_t>(item.numeric_extreme);
        }
        if (item.sum_is_double) {
            return item.numeric_sum;
        }
        return static_cast<int64_t>(item.numeric_sum);
    }
}

#include "rete_node_accumulate.inc"

// =========================================================================
// === WINDOW NODE =========================================================
// =========================================================================

WindowNode::WindowNode(ParsedWindow const& window_info)
    : ReteNode(NodeKind::Window), info(window_info) {}

void WindowNode::right_activate(StatefulSession& session, Fact* fact, PropagationType p_type) {
    if (mem_slot < 0 || static_cast<size_t>(mem_slot) >= session.net_mem().window.size()) {
        throw std::runtime_error("WindowNode memory slot is not initialized");
    }
    auto& mem = session.net_mem().window[mem_slot];

    if (p_type == PropagationType::RETRACT) {
        auto position = std::find(mem.facts.begin(), mem.facts.end(), fact);
        if (position != mem.facts.end()) {
            mem.facts.erase(position);
            for (auto* child : children_raw) {
                child->right_activate(session, fact, p_type);
            }
        }
        return;
    }

    if (p_type == PropagationType::ASSERT) {
        if (info.type == WindowType::TIME) {
            auto timestamp_of = [](Fact const* item) {
                auto ts = item ? item->get_field("timestamp") : std::nullopt;
                return ts && std::holds_alternative<int64_t>(*ts)
                    ? std::get<int64_t>(*ts)
                    : std::numeric_limits<int64_t>::max();
            };
            auto const fact_ts = session.fact_event_time(fact).value_or(timestamp_of(fact));
            auto position = std::upper_bound(
                mem.facts.begin(), mem.facts.end(), fact_ts,
                [&](int64_t timestamp, Fact const* item) {
                    return timestamp < session.fact_event_time(item).value_or(timestamp_of(item));
                });
            mem.facts.insert(position, fact);
        } else {
            mem.facts.push_back(fact);
        }
        evaluate_expiration(session);
        if (std::find(mem.facts.begin(), mem.facts.end(), fact) != mem.facts.end()) {
            for (auto* child : children_raw) {
                child->right_activate(session, fact, p_type);
            }
        }
    } else if (p_type == PropagationType::MODIFY) {
        evaluate_expiration(session);
        for (auto* child : children_raw) {
            child->right_activate(session, fact, p_type);
        }
    }
}

std::size_t WindowNode::evaluate_expiration(StatefulSession& session) {
    if (mem_slot < 0 || static_cast<size_t>(mem_slot) >= session.net_mem().window.size()) {
        throw std::runtime_error("WindowNode memory slot is not initialized");
    }
    auto& mem = session.net_mem().window[mem_slot];
    std::size_t expired_count = 0;

    if (info.type == WindowType::LENGTH) {
        while (mem.facts.size() > static_cast<size_t>(info.size)) {
            Fact* expired_fact = mem.facts.front();
            mem.facts.erase(mem.facts.begin());
            for (auto* child : children_raw) {
                child->right_activate(session, expired_fact, PropagationType::RETRACT);
            }
            ++expired_count;
        }
    } else if (info.type == WindowType::TIME) {
        if (session.is_event_time_mode() && !session.event_time_watermark()) {
            return expired_count;
        }
        int64_t current_time = session.event_time_watermark().value_or(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());

        auto it = mem.facts.begin();
        while (it != mem.facts.end()) {
            Fact* f = *it;
            // Eager eviction relying on an implicit "timestamp" field.
            auto event_ts = session.fact_event_time(f);
            auto ts_opt = f->get_field("timestamp");
            int64_t fact_ts = current_time;
            if (event_ts) {
                fact_ts = *event_ts;
            } else if (ts_opt && std::holds_alternative<int64_t>(*ts_opt)) {
                fact_ts = std::get<int64_t>(*ts_opt);
            }

            if (current_time - fact_ts > info.size) {
                for (auto* child : children_raw) {
                    child->right_activate(session, f, PropagationType::RETRACT);
                }
                it = mem.facts.erase(it);
                ++expired_count;
            } else {
                break;
            }
        }
    }
    return expired_count;
}

void WindowNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"WindowNode (" << id << ")\\n"
       << (info.type == WindowType::TIME ? "TIME " : "LENGTH ") << info.size
       << "\", shape=box3d, style=filled, fillcolor=lightblue];";
}

#include "rete_node_unnest.inc"

#include "rete_node_eval.inc"

#include "rete_node_terminal.inc"

#include "rete_node_query_terminal.inc"

#include "rete_node_query_input.inc"

#include "rete_node_query_call.inc"
