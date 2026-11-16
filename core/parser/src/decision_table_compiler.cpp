#include "decision_table_compiler.hpp"
#include "decision_table_converter.hpp"
#include "rfl_parser_impl.hpp"
#include <stdexcept>

namespace {
// Per-call deltas are observed by the compiling thread; concurrent knowledge
// base construction must not race on diagnostic counters.
thread_local DecisionTableCompileStats compile_stats;
}

parser_state DirectTableCompiler::compile(DecisionTable const& table,
                                          std::string const& source_name,
                                          std::vector<StructuredError>& errors) {
    std::string generated;
    try {
        DecisionTableConverter converter(table);
        generated = converter.generate_drl();
    } catch (std::invalid_argument const& error) {
        errors.push_back({.file_name = source_name, .message = error.what()});
        return {};
    }
    // One grammar owns admission. A parse failure is returned directly; no
    // second compiler, alternate interpretation or retry follows it.
    auto state = rfl_parse_lemon(generated, source_name, errors);
    if (errors.empty()) ++compile_stats.parser_path_success;
    return state;
}
DecisionTableCompileStats DirectTableCompiler::get_stats() { return compile_stats; }
void DirectTableCompiler::reset_stats() { compile_stats = {}; }
