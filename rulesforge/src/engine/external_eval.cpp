#include "engine/external_eval.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <string_view>
#include <system_error>

namespace rulesforge {
namespace {

std::string trim_copy(std::string value) {
    auto not_space = [](unsigned char ch) { return std::isspace(ch) == 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

bool is_identifier_segment(std::string_view value) {
    if (value.empty()) {
        return false;
    }
    if (std::isalpha(static_cast<unsigned char>(value.front())) == 0 && value.front() != '_') {
        return false;
    }
    return std::all_of(value.begin() + 1, value.end(), [](char ch) {
        return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
    });
}

std::optional<std::string> normalize_external_eval_variable(std::string const& token) {
    if (token.size() < 2 || token.front() != '$') {
        return std::nullopt;
    }
    std::string normalized{"$"};
    std::size_t begin = 1;
    bool first = true;
    while (begin < token.size()) {
        auto dot = token.find('.', begin);
        auto const end = dot == std::string::npos ? token.size() : dot;
        std::string segment = trim_copy(token.substr(begin, end - begin));
        if (!is_identifier_segment(segment)) {
            return std::nullopt;
        }
        if (!first) {
            normalized.push_back('.');
        }
        normalized += segment;
        if (dot == std::string::npos) {
            return normalized;
        }
        first = false;
        begin = dot + 1;
    }
    return std::nullopt;
}

} // namespace

bool parse_external_eval_helper_name(std::string const& raw_name, std::string& predicate_name) {
    std::vector<std::string> segments;
    std::size_t begin = 0;
    while (begin <= raw_name.size()) {
        auto const dot = raw_name.find('.', begin);
        auto const end = dot == std::string::npos ? raw_name.size() : dot;
        std::string segment = trim_copy(raw_name.substr(begin, end - begin));
        if (!is_identifier_segment(segment)) {
            return false;
        }
        segments.push_back(std::move(segment));
        if (dot == std::string::npos) {
            break;
        }
        begin = dot + 1;
    }

    if (segments.size() < 2 || segments.front() != "native") {
        return false;
    }

    predicate_name.clear();
    for (std::size_t index = 1; index < segments.size(); ++index) {
        if (index != 1) {
            predicate_name.push_back('.');
        }
        predicate_name += segments[index];
    }
    return true;
}

bool split_external_eval_arg_text(std::string const& arg_text, std::vector<std::string>& out) {
    out.clear();
    std::string current;
    bool in_string = false;
    bool escaped = false;
    int depth = 0;
    for (char const ch : arg_text) {
        if (escaped) {
            current.push_back(ch);
            escaped = false;
            continue;
        }
        if (ch == '\\' && in_string) {
            current.push_back(ch);
            escaped = true;
            continue;
        }
        if (ch == '"') {
            in_string = !in_string;
            current.push_back(ch);
            continue;
        }
        if (!in_string && ch == '(') {
            ++depth;
            current.push_back(ch);
            continue;
        }
        if (!in_string && ch == ')') {
            --depth;
            if (depth < 0) {
                return false;
            }
            current.push_back(ch);
            continue;
        }
        if (ch == ',' && !in_string && depth == 0) {
            out.push_back(trim_copy(current));
            current.clear();
            continue;
        }
        current.push_back(ch);
    }
    if (in_string || escaped || depth != 0) {
        return false;
    }
    if (!current.empty() || !arg_text.empty()) {
        out.push_back(trim_copy(current));
    }
    return true;
}

std::optional<ConstraintValue> parse_external_eval_literal(std::string const& token) {
    if (token.size() >= 2 && token.front() == '"' && token.back() == '"') {
        std::string value;
        value.reserve(token.size() - 2);
        bool escaped = false;
        for (std::size_t index = 1; index + 1 < token.size(); ++index) {
            char ch = token[index];
            if (escaped) {
                value.push_back(ch);
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else {
                value.push_back(ch);
            }
        }
        if (escaped) {
            return std::nullopt;
        }
        return ConstraintValue{std::move(value)};
    }
    if (token == "true") {
        return ConstraintValue{true};
    }
    if (token == "false") {
        return ConstraintValue{false};
    }
    if (token == "nil" || token == "null") {
        return ConstraintValue{NilValue{}};
    }
    int64_t int_value = 0;
    auto [int_ptr, int_ec] = std::from_chars(token.data(), token.data() + token.size(), int_value);
    if (int_ec == std::errc() && int_ptr == token.data() + token.size()) {
        return ConstraintValue{int_value};
    }
    char* double_end = nullptr;
    errno = 0;
    double const double_value = std::strtod(token.c_str(), &double_end);
    if (errno == 0 && double_end != token.c_str() && double_end != nullptr && *double_end == '\0') {
        return ConstraintValue{double_value};
    }
    return std::nullopt;
}

std::optional<ExternalEvalCall> parse_external_eval_call(std::string const& expression) {
    std::string const trimmed = trim_copy(expression);
    auto const open = trimmed.find('(');
    if (open == std::string::npos || trimmed.empty() || trimmed.back() != ')') {
        return std::nullopt;
    }
    std::string predicate_name;
    if (!parse_external_eval_helper_name(trim_copy(trimmed.substr(0, open)), predicate_name)) {
        return std::nullopt;
    }

    ExternalEvalCall call;
    call.predicate_name = std::move(predicate_name);
    std::string const arg_text = trimmed.substr(open + 1, trimmed.size() - open - 2);
    if (trim_copy(arg_text).empty()) {
        return call;
    }
    std::vector<std::string> raw_args;
    if (!split_external_eval_arg_text(arg_text, raw_args)) {
        return std::nullopt;
    }
    for (auto const& raw_arg : raw_args) {
        if (raw_arg.empty()) {
            return std::nullopt;
        }
        if (auto variable = normalize_external_eval_variable(raw_arg)) {
            call.arguments.push_back(ExternalEvalArgument{
                ExternalEvalArgumentKind::Variable,
                std::move(*variable),
                NilValue{}});
            continue;
        }
        auto literal = parse_external_eval_literal(raw_arg);
        if (literal) {
            call.arguments.push_back(ExternalEvalArgument{
                ExternalEvalArgumentKind::Literal,
                raw_arg,
                std::move(*literal)});
            continue;
        }
        call.arguments.push_back(ExternalEvalArgument{
            ExternalEvalArgumentKind::NumericExpression,
            raw_arg,
            NilValue{}});
    }
    return call;
}

} // namespace rulesforge
