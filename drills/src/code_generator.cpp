#include "code_generator.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

CodeGenerator::CodeGenerator(parser_state const& state) : state_(state) {}

std::string CodeGenerator::map_type_to_cpp(std::string const& drl_type) {
    if (drl_type == "String") return "std::string";
    if (drl_type == "int" || drl_type == "long") return "int64_t";
    if (drl_type == "double") return "double";
    if (drl_type == "boolean") return "bool";
    return drl_type;   // Assume other types are already valid C++ types
}

void CodeGenerator::generate(std::string const& header_path, std::string const& source_path) {
    std::ofstream header_file(header_path);
    header_file << "#pragma once\n\n";
    header_file << "#include <string>\n";
    header_file << "#include <cstdint>\n\n";
    header_file << "class ReteNetwork; // Forward declaration\n\n";

    for (auto const& decl : state_.parsed_declarations) {
        header_file << "struct " << decl.type_name << " {\n";
        for (auto const& field : decl.fields) {
            header_file << "    " << map_type_to_cpp(field.type) << " " << field.name << ";\n";
        }
        header_file << "};\n\n";
    }
    header_file << "// Main function to register all declared types.\n";
    header_file << "void register_generated_types(ReteNetwork& network);\n";

    // --- Generate the Source File (e.g., generated_converters.cpp) ---
    std::ofstream source_file(source_path);
    source_file << "#include \"rete/rete_network.hpp\"\n";
    source_file << "#include \"" << "generated_facts.hpp" << "\"\n\n";   // Assumes a fixed name
    source_file << "void register_generated_types(ReteNetwork& network) {\n";
    source_file << "    auto& registry = network.get_fact_type_registry();\n\n";

    for (auto const& decl : state_.parsed_declarations) {
        source_file << "    registry.register_type<" << decl.type_name << ">(\"" << decl.type_name << "\",\n";
        source_file << "        [](const " << decl.type_name << "& obj, Fact& fact) {\n";
        for (auto const& field : decl.fields) {
            source_file << "            fact.fields[\"" << field.name << "\"] = obj." << field.name << ";\n";
        }
        source_file << "        });\n\n";
    }
    source_file << "}\n";
}
