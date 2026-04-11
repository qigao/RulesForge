#include "compiler_core.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

bool write_text(std::string const& path, std::string const& text) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << text;
    return out.good();
}

void print_errors(ParsingResult const& result) {
    for (auto const& error : result.errors) {
        std::cerr << error.to_string() << "\n";
    }
}

bool parse_language(std::string const& value, rulesforge_compiler::OutputLanguage& out_language) {
    if (value == "c") {
        out_language = rulesforge_compiler::OutputLanguage::C;
        return true;
    }
    if (value == "cpp" || value == "cxx" || value == "c++") {
        out_language = rulesforge_compiler::OutputLanguage::Cpp;
        return true;
    }
    if (value == "go") {
        out_language = rulesforge_compiler::OutputLanguage::Go;
        return true;
    }
    if (value == "ts" || value == "typescript") {
        out_language = rulesforge_compiler::OutputLanguage::TypeScript;
        return true;
    }
    if (value == "py" || value == "python") {
        out_language = rulesforge_compiler::OutputLanguage::Python;
        return true;
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    char const* input_path = nullptr;
    char const* output_path = nullptr;
    char const* template_path = nullptr;
    bool has_language = false;
    bool skip_validate = false;
    rulesforge_compiler::ValidationMode validation_mode = rulesforge_compiler::ValidationMode::Both;
    rulesforge_compiler::OutputLanguage output_language = rulesforge_compiler::OutputLanguage::C;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o" || arg == "--output") {
            if (i + 1 >= argc) {
                std::cerr << "error: missing value for " << arg << "\n";
                return 2;
            }
            output_path = argv[++i];
        } else if (arg == "-t" || arg == "--template") {
            if (i + 1 >= argc) {
                std::cerr << "error: missing value for " << arg << "\n";
                return 2;
            }
            template_path = argv[++i];
        } else if (arg == "-l" || arg == "--lang") {
            if (i + 1 >= argc) {
                std::cerr << "error: missing value for " << arg << "\n";
                return 2;
            }
            if (!parse_language(argv[i + 1], output_language)) {
                std::cerr << "error: unsupported language: " << argv[i + 1] << "\n";
                return 2;
            }
            has_language = true;
            ++i;
        } else if (arg == "-S" || arg == "--skip-validate") {
            skip_validate = true;
        } else if (arg == "--json-only") {
            validation_mode = rulesforge_compiler::ValidationMode::JsonOnly;
        } else if (arg == "--binary-only") {
            validation_mode = rulesforge_compiler::ValidationMode::BinaryOnly;
        } else if (!input_path) {
            input_path = argv[i];
        } else {
            std::cerr << "error: unexpected argument: " << arg << "\n";
            return 2;
        }
    }

    if (!input_path || (!template_path && !has_language)) {
        std::cerr << "usage: rulesforge_compiler <input.rfl> (-l c|cpp|go|ts|py | -t template.mustache) "
                     "[-o output] [--skip-validate] [--json-only|--binary-only]\n";
        return 2;
    }

    if (template_path && has_language) {
        std::cerr << "error: use either --lang or --template, not both\n";
        return 2;
    }

    ParsingResult result;
    parser_state state;
    if (!rulesforge_compiler::compile_file(input_path, result, state)) {
        print_errors(result);
        return 1;
    }

    if (!skip_validate) {
        std::string validate_error;
        if (!rulesforge_compiler::validate_databind_support(state, validation_mode, validate_error)) {
            std::cerr << "error: " << validate_error << "\n";
            return 2;
        }
    }

    std::string output;
    if (template_path) {
        std::string render_error;
        if (!rulesforge_compiler::render_with_template_file(state, template_path, output, render_error)) {
            std::cerr << "error: " << render_error << "\n";
            return 3;
        }
    } else {
        std::filesystem::path exe_path = std::filesystem::absolute(argv[0]);
        std::filesystem::path builtin_path = exe_path.parent_path() / "templates"
                                             / rulesforge_compiler::builtin_template_name(output_language);
        std::string render_error;
        if (!rulesforge_compiler::render_with_template_file(state, builtin_path.string(), output, render_error)) {
            std::cerr << "error: " << render_error << "\n";
            return 3;
        }
    }
    bool ok = output_path ? write_text(output_path, output) : (std::cout << output, std::cout.good());

    if (!ok) {
        std::cerr << "error: failed to write output\n";
        return 4;
    }
    return 0;
}
