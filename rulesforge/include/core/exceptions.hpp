#ifndef RULEFORGE_EXCEPTIONS_HPP
#define RULEFORGE_EXCEPTIONS_HPP

#include <stdexcept>
#include <string>

// Custom exception for runtime errors during rule execution.
class ReteExecutionException : public std::runtime_error {
public:
    ReteExecutionException(std::string const& message, std::string rule_name) :
        std::runtime_error(message), rule_name_(std::move(rule_name)) {}

    char const* get_rule_name() const noexcept { return rule_name_.c_str(); }

private:
    std::string rule_name_;
};

#endif  // RULEFORGE_EXCEPTIONS_HPP
