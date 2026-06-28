#ifndef EXPRESSION_DESCRIPTOR_HPP
#define EXPRESSION_DESCRIPTOR_HPP

#include <memory>
#include <string>
#include <vector>

namespace rulesforge {

class ExpressionDescriptor {
public:
    static std::shared_ptr<ExpressionDescriptor> compile(
        std::string const& expr,
        std::string* error_out = nullptr);

    std::vector<std::string> const& variables() const;
    std::string const& expression_string() const;

private:
    struct Impl;
    explicit ExpressionDescriptor(std::shared_ptr<Impl> impl);
    std::shared_ptr<Impl> impl_;
};

}  // namespace rulesforge

#endif  // EXPRESSION_DESCRIPTOR_HPP
