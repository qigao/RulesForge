#include "compiled_expression.hpp"
#include "exprtk.hpp"

#include <regex>
#include <unordered_map>

namespace ruleforge {

/**
 * PIMPL implementation that holds exprtk types.
 *
 * Variable handling strategy:
 * - Original variables like "$price", "$order.total" are extracted from the expression
 * - They're mapped to safe names (v0, v1, ...) for exprtk compilation
 * - At evaluation time, the resolver is called with original names, values bound to safe names
 */
struct CompiledExpression::Impl {
    std::string original_expr;
    std::string safe_expr;  // Expression with $vars replaced by v0, v1, etc.
    std::vector<std::string> var_names;  // Original variable names in order
    std::unordered_map<std::string, size_t> var_to_index;  // $varname -> index in var_names

    // exprtk types
    exprtk::symbol_table<double> symbol_table;
    exprtk::expression<double> expression;
    std::vector<double> var_values;  // Mutable storage for variable values during eval
};

CompiledExpression::CompiledExpression() : impl_(std::make_unique<Impl>()) {}

CompiledExpression::~CompiledExpression() = default;

CompiledExpression::CompiledExpression(CompiledExpression&&) noexcept = default;

CompiledExpression& CompiledExpression::operator=(CompiledExpression&&) noexcept = default;

std::shared_ptr<CompiledExpression> CompiledExpression::compile(
    std::string const& expr,
    std::string* error_out)
{
    if (expr.empty()) {
        if (error_out) *error_out = "Empty expression";
        return nullptr;
    }

    auto result = std::shared_ptr<CompiledExpression>(new CompiledExpression());
    auto& impl = *result->impl_;
    impl.original_expr = expr;

    // Extract all $variable references (e.g., $price, $order.total, $x)
    // Pattern: $ followed by identifier, optionally followed by .identifier chains
    std::regex var_regex(R"(\$[a-zA-Z_][a-zA-Z0-9_]*(?:\.[a-zA-Z_][a-zA-Z0-9_]*)*)");

    std::string safe_expr = expr;
    std::sregex_iterator iter(expr.begin(), expr.end(), var_regex);
    std::sregex_iterator end;

    // Collect unique variables and their positions
    std::vector<std::pair<size_t, std::string>> replacements;  // position, original_name

    while (iter != end) {
        std::string var_name = iter->str();
        size_t pos = iter->position();

        if (impl.var_to_index.find(var_name) == impl.var_to_index.end()) {
            size_t idx = impl.var_names.size();
            impl.var_names.push_back(var_name);
            impl.var_to_index[var_name] = idx;
        }

        replacements.push_back({pos, var_name});
        ++iter;
    }

    // Replace variables from end to start to preserve positions
    std::sort(replacements.begin(), replacements.end(),
              [](auto const& a, auto const& b) { return a.first > b.first; });

    for (auto const& [pos, var_name] : replacements) {
        size_t idx = impl.var_to_index[var_name];
        std::string safe_name = "v" + std::to_string(idx);
        safe_expr.replace(pos, var_name.length(), safe_name);
    }

    impl.safe_expr = safe_expr;

    // Set up symbol table with variable storage
    impl.var_values.resize(impl.var_names.size(), 0.0);

    for (size_t i = 0; i < impl.var_names.size(); ++i) {
        std::string safe_name = "v" + std::to_string(i);
        impl.symbol_table.add_variable(safe_name, impl.var_values[i]);
    }

    // Add standard constants (pi, e, etc.)
    impl.symbol_table.add_constants();

    impl.expression.register_symbol_table(impl.symbol_table);

    // Compile the expression
    exprtk::parser<double> parser;

    if (!parser.compile(impl.safe_expr, impl.expression)) {
        if (error_out) {
            *error_out = "Failed to compile expression '" + expr + "': " + parser.error();
        }
        return nullptr;
    }

    return result;
}

double CompiledExpression::evaluate(VariableResolver const& resolver) const {
    // Bind current variable values
    for (size_t i = 0; i < impl_->var_names.size(); ++i) {
        impl_->var_values[i] = resolver(impl_->var_names[i]);
    }

    return impl_->expression.value();
}

std::vector<std::string> const& CompiledExpression::variables() const {
    return impl_->var_names;
}

std::string const& CompiledExpression::expression_string() const {
    return impl_->original_expr;
}

} // namespace ruleforge
