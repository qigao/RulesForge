#include "expression_descriptor.hpp"
#include <algorithm>
#include <cctype>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace rulesforge {

namespace {
// Helper to sanitize RFL variable names ($var.field -> _var_field) for backend expressions.
std::string sanitize_name(std::string const& name) {
    std::string s = name;
    if (!s.empty() && s[0] == '$') s[0] = '_';
    std::replace(s.begin(), s.end(), '.', '_');
    return s;
}

bool is_identifier_char(char c) {
    unsigned char uc = static_cast<unsigned char>(c);
    return std::isalnum(uc) || c == '_';
}

std::pair<std::string, size_t> extract_variable_name(std::string const& expr, size_t start) {
    if (start >= expr.size() || expr[start] != '$') {
        return {"", start};
    }

    size_t pos = start + 1;
    while (pos < expr.size() && std::isspace(static_cast<unsigned char>(expr[pos]))) {
        ++pos;
    }

    size_t seg_start = pos;
    while (pos < expr.size() && is_identifier_char(expr[pos])) {
        ++pos;
    }
    if (seg_start == pos) {
        return {"", start + 1};
    }

    std::string result = "$" + expr.substr(seg_start, pos - seg_start);

    while (true) {
        size_t checkpoint = pos;
        while (pos < expr.size() && std::isspace(static_cast<unsigned char>(expr[pos]))) {
            ++pos;
        }
        if (pos >= expr.size() || expr[pos] != '.') {
            pos = checkpoint;
            break;
        }
        ++pos;
        while (pos < expr.size() && std::isspace(static_cast<unsigned char>(expr[pos]))) {
            ++pos;
        }
        size_t next_seg_start = pos;
        while (pos < expr.size() && is_identifier_char(expr[pos])) {
            ++pos;
        }
        if (next_seg_start == pos) {
            pos = checkpoint;
            break;
        }
        result.push_back('.');
        result.append(expr, next_seg_start, pos - next_seg_start);
    }

    return {result, pos};
}

}

struct ExpressionDescriptor::Impl {
    std::string original_expr;
    std::string sanitized_expr;
    std::vector<std::string> original_var_names;
    std::vector<std::string> sanitized_var_names;
};

ExpressionDescriptor::ExpressionDescriptor(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}

std::shared_ptr<ExpressionDescriptor> ExpressionDescriptor::compile(std::string const& expr, std::string* error_out) {
    if (expr.empty()) {
        if (error_out) *error_out = "Empty expression";
        return nullptr;
    }

    auto impl = std::make_shared<Impl>();
    impl->original_expr = expr;

    std::string processed = expr;
    size_t pos = 0;
    std::unordered_map<std::string, std::string> name_map;

    while ((pos = processed.find('$', pos)) != std::string::npos) {
        auto [orig, end] = extract_variable_name(processed, pos);
        if (orig.empty()) {
            ++pos;
            continue;
        }
        std::string sanitized = sanitize_name(orig);

        if (name_map.find(orig) == name_map.end()) {
            name_map[orig] = sanitized;
            impl->original_var_names.push_back(orig);
            impl->sanitized_var_names.push_back(sanitized);
        }

        processed.replace(pos, end - pos, sanitized);
        pos += sanitized.length();
    }
    impl->sanitized_expr = processed;

    if (error_out) {
        error_out->clear();
    }
    return std::shared_ptr<ExpressionDescriptor>(new ExpressionDescriptor(impl));
}

std::vector<std::string> const& ExpressionDescriptor::variables() const {
    return impl_->original_var_names;
}

std::string const& ExpressionDescriptor::expression_string() const {
    return impl_->original_expr;
}

} // namespace rulesforge
