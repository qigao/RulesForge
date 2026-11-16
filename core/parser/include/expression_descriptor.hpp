#ifndef EXPRESSION_DESCRIPTOR_HPP
#define EXPRESSION_DESCRIPTOR_HPP

#include <memory>
#include <string>
#include <vector>
#include "model/fact.hpp"

namespace rulesforge {

class ExpressionDescriptor {
public:
    // Immutable access plan. Name/binding are owned; paths are parsed once.
    struct Variable {
        std::string name;
        std::string binding;
        std::string field;
        std::vector<PathSegment> path;
        std::vector<PathSegment> unqualified_path;
    };
    using Resolver = std::optional<ConstraintValue> (*)(void*, Variable const&);
    static constexpr size_t max_operations = 256, max_variables = 64;
    static constexpr size_t max_depth = 64, max_source_bytes = 16384;
    static std::shared_ptr<ExpressionDescriptor> compile(
        std::string const& expr,
        std::string* error_out = nullptr);

    std::vector<std::string> const& variables() const;
    std::string const& expression_string() const;
    static std::shared_ptr<ExpressionDescriptor> compile_required(std::string const& expr);
    // Synchronous, reentrant execution. Native code returns an explicit host
    // request before invoking any allocating/throwing operation, then resumes.
    std::optional<ConstraintValue> evaluate(Resolver resolver, void* context) const;
    static bool truthy(ConstraintValue const& value);

private:
    struct Impl;
    explicit ExpressionDescriptor(std::shared_ptr<Impl> impl);
    std::shared_ptr<Impl> impl_;
};

}  // namespace rulesforge

#endif  // EXPRESSION_DESCRIPTOR_HPP
