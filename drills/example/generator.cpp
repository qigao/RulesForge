#include "code_generator.hpp"
#include "drools_parser.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

// A simple helper to read a file
std::string read_file(std::string const& path) {
    std::ifstream file(path);
    if (!file) {
        std::cerr << "Error: Could not open file " << path << std::endl;
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// The main function for our code generator tool.
// Usage: ./rule_codegen <input_drl_file> <output_header> <output_source>
int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <input.drl> <output.hpp> <output.cpp>" << std::endl;
        return 1;
    }

    std::string drl_content = read_file(argv[1]);
    if (drl_content.empty()) { return 1; }

    // Now the compiler knows what a ReteNetwork is and can create this object.
    parser_state state;
    ReteNetwork dummy_network;
    ParsingResult result = parse_drools_for_rete(drl_content, state, dummy_network, argv[1]);

    if (!result.success) {
        for (auto const& err : result.errors) { std::cerr << err.to_string() << std::endl; }
        return 1;
    }

    // If parsing was successful, run the code generator.
    CodeGenerator generator(state);
    generator.generate(argv[2], argv[3]);

    std::cout << "Successfully generated " << argv[2] << " and " << argv[3] << std::endl;
    return 0;
}
