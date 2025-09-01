#ifndef CODE_GENERATOR_HPP
#define CODE_GENERATOR_HPP

#include "drools_parser_state.hpp"

#include <string>

class CodeGenerator {
public:
    explicit CodeGenerator(parser_state const& state);
    void generate(std::string const& header_path, std::string const& source_path);

private:
    std::string map_type_to_cpp(std::string const& drl_type);
    parser_state const& state_;
};

#endif   // CODE_GENERATOR_HPP
