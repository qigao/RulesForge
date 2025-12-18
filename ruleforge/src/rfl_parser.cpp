#include "rfl_parser.hpp"
#include "fmtlog.h"

#include "ast_builder.hpp"
#include "ast_transformer.hpp"
#include "decision_table_converter.hpp"
#include "decision_table_parser.hpp"
#include "knowledge_base.hpp"
#include "semantic_analyzer.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <tao/pegtl/contrib/analyze.hpp>
#include <tao/pegtl/contrib/parse_tree.hpp>
#include <tao/pegtl/contrib/trace.hpp>
#include <tao/pegtl/string_input.hpp>

namespace {
    // PROD-004: Helper to log all parsing errors
    void log_parsing_errors(ParsingResult const& result) {
        if (result.success || result.errors.empty()) return;

        loge("Rule parsing failed with {} error(s):", result.errors.size());
        for (auto const& error : result.errors) {
            loge("  {}", error.to_string());
        }
    }

    std::optional<std::string> resolve_import_to_path(std::string const& import_str,
                                                      std::vector<std::string> const& base_dirs) {
        std::string relative_path_str = import_str;
        for (char& c : relative_path_str) {
            if (c == '.') { c = std::filesystem::path::preferred_separator; }
        }
        relative_path_str += ".rfl";

        for (auto const& base : base_dirs) {
            std::filesystem::path full_path = std::filesystem::path(base) / relative_path_str;
            if (std::filesystem::exists(full_path)) { return full_path.string(); }
        }
        return std::nullopt;
    }

    parser_state parse_file_to_local_state(std::string const& file_path, ParsingResult& result) {
        parser_state local_state;

        std::ifstream file_stream(file_path);
        if (!file_stream) {
            result.success = false;
            result.errors.push_back({.file_name = file_path, .message = "Could not open file."});
            return local_state;
        }
        std::stringstream buffer;
        buffer << file_stream.rdbuf();
        std::string content = buffer.str();

        if (content.find_first_not_of(" \t\r\n") == std::string::npos) {
            return local_state;   // Empty file is not an error
        }

        using top_level_grammar = pegtl::must<grammar::main>;
        auto in = pegtl::string_input(content, file_path);
        std::unique_ptr<pegtl::parse_tree::node> root;
        try {
            root = pegtl::parse_tree::parse<top_level_grammar>(in);
        } catch (pegtl::parse_error const& e) {
            auto const p = e.positions().front();
            result.errors.push_back({.file_name = file_path, .line = p.line, .column = p.column, .message = e.what()});
            result.success = false;
            return local_state;
        }

        AstBuilder ast_builder(std::move(root), file_path);
        local_state = ast_builder.build(result.errors);
        if (!result.errors.empty()) { result.success = false; }
        return local_state;
    }

    // The recursive file discovery and state-merging function.
    void discover_and_merge_files(std::string const& file_path, RflParserContext& context) {
        try {
            auto canonical_path = std::filesystem::canonical(file_path);
            if (context.visited_files.count(canonical_path.string())) {
                return;   // Already processed
            }
            context.visited_files.insert(canonical_path.string());
            logd("Parsing imported file: {}", canonical_path.string());
        } catch (std::filesystem::filesystem_error const& e) {
            context.result.errors.push_back({.message = "File system error: " + std::string(e.what())});
            context.result.success = false;
            return;
        }

        parser_state local_state = parse_file_to_local_state(file_path, context.result);
        if (!context.result.success) return;

        // Merge contents into the main state
        context.state.parsed_rules.insert(context.state.parsed_rules.end(),
                                          std::make_move_iterator(local_state.parsed_rules.begin()),
                                          std::make_move_iterator(local_state.parsed_rules.end()));
        context.state.parsed_declarations.insert(context.state.parsed_declarations.end(),
                                                 std::make_move_iterator(local_state.parsed_declarations.begin()),
                                                 std::make_move_iterator(local_state.parsed_declarations.end()));
        context.state.parsed_queries.insert(context.state.parsed_queries.end(),
                                            std::make_move_iterator(local_state.parsed_queries.begin()),
                                            std::make_move_iterator(local_state.parsed_queries.end()));
        context.state.parsed_globals.insert(context.state.parsed_globals.end(),
                                            std::make_move_iterator(local_state.parsed_globals.begin()),
                                            std::make_move_iterator(local_state.parsed_globals.end()));
        context.state.parsed_functions.insert(context.state.parsed_functions.end(),
                                              std::make_move_iterator(local_state.parsed_functions.begin()),
                                              std::make_move_iterator(local_state.parsed_functions.end()));

        // Recurse for imports
        for (auto const& import_str : local_state.parsed_imports) {
            if (!context.result.success) break;

            std::string path_to_find = import_str;
            bool is_wildcard = false;
            if (import_str.ends_with(".*")) {
                is_wildcard = true;
                path_to_find = import_str.substr(0, import_str.length() - 2);
            }

            std::string relative_dir = path_to_find;
            for (char& c : relative_dir) {
                if (c == '.') c = std::filesystem::path::preferred_separator;
            }

            bool found = false;
            for (auto const& base_dir : context.base_dirs) {
                std::filesystem::path full_path = std::filesystem::path(base_dir) / relative_dir;

                if (is_wildcard) {
                    if (std::filesystem::is_directory(full_path)) {
                        found = true;
                        for (auto const& dir_entry : std::filesystem::directory_iterator(full_path)) {
                            if (dir_entry.is_regular_file() && dir_entry.path().extension() == ".rfl") {
                                discover_and_merge_files(dir_entry.path().string(), context);
                            }
                        }
                        break;
                    }
                } else {
                    full_path.replace_extension(".rfl");
                    if (std::filesystem::is_regular_file(full_path)) {
                        found = true;
                        discover_and_merge_files(full_path.string(), context);
                        break;
                    }
                }
            }
            if (!found) {
                context.result.errors.push_back(
                    {.file_name = file_path, .message = "Could not resolve import: " + import_str});
                context.result.success = false;
            }
        }
    }

}   // namespace

std::shared_ptr<KnowledgeBase> build_knowledge_base(std::string const& input, ParsingResult& result,
                                                    std::string const& source_name) {
    if (input.find_first_not_of(" \t\r\n") == std::string::npos) {
        result.success = true;
        parser_state empty_state;
        return KnowledgeBase::create(empty_state);
    }

    using top_level_grammar = pegtl::must<grammar::main>;
    auto in = pegtl::string_input(input, source_name);
    std::unique_ptr<pegtl::parse_tree::node> root;

    try {
        root = pegtl::parse_tree::parse<top_level_grammar>(in);
    } catch (pegtl::parse_error const& e) {
        auto const p = e.positions().front();
        result.success = false;
        result.errors.push_back({.file_name = source_name, .line = p.line, .column = p.column, .message = e.what()});
        log_parsing_errors(result);  // PROD-004: Log errors
        return nullptr;
    }

    if (!root) {
        result.success = false;
        if (result.errors.empty()) {
            result.errors.push_back({.file_name = source_name,
                                     .message = "Parsing failed: input is empty or does not produce a parse tree."});
        }
        log_parsing_errors(result);  // PROD-004: Log errors
        return nullptr;
    }

    parser_state state;
    // The AstBuilder here does not need the package/import context stamping, as it's a single string.
    AstBuilder ast_builder(std::move(root), source_name);
    state = ast_builder.build(result.errors);
    if (!result.errors.empty()) {
        result.success = false;
        log_parsing_errors(result);  // PROD-004: Log errors
        return nullptr;
    }

    AstTransformer transformer(state);
    transformer.transform();

    SemanticAnalyzer analyzer(state, source_name);
    // Call the two new methods in sequence instead of the old 'analyze()'
    if (!analyzer.build_and_analyze_declarations() || !analyzer.analyze_rules_and_queries()) {
        result.success = false;
        result.errors = analyzer.get_errors();
        log_parsing_errors(result);  // PROD-004: Log errors
        return nullptr;
    }

    try {
        result.success = true;
        return KnowledgeBase::create(state);
    } catch (std::exception const& e) {
        result.success = false;
        result.errors.push_back(
            {.file_name = source_name, .message = "KnowledgeBase build failed: " + std::string(e.what())});
        log_parsing_errors(result);  // PROD-004: Log errors
        return nullptr;
    }
}

std::shared_ptr<KnowledgeBase> build_knowledge_base(ParsingResult& result, std::string const& file_path) {
    std::filesystem::path path_obj(file_path);
    return build_knowledge_base(file_path, {path_obj.parent_path().string()}, result);
}

std::shared_ptr<KnowledgeBase> build_knowledge_base(std::string const& file_path,
                                                    std::vector<std::string> const& base_dirs, ParsingResult& result) {
    return build_knowledge_base(std::vector<std::string>{file_path}, base_dirs, result);
}

std::shared_ptr<KnowledgeBase> build_knowledge_base(std::vector<std::string> const& file_paths,
                                                    std::vector<std::string> const& base_dirs,
                                                    ParsingResult& result) {
    if (file_paths.empty()) {
        result.success = true;
        parser_state empty_state;
        return KnowledgeBase::create(empty_state);
    }

    RflParserContext context{.base_dirs = base_dirs, .result = result};

    // PASS 1: PARSE & MERGE
    // Iterate over all provided file paths and merge them into the single context.state.
    for (auto const& file_path : file_paths) {
        discover_and_merge_files(file_path, context);
        if (!result.success) {
            log_parsing_errors(result);  // PROD-004: Log errors
            return nullptr;
        }
    }

    // Run AST transformations on the unified AST
    AstTransformer transformer(context.state);
    transformer.transform();

    // Use the first file path as the primary source name for error reporting in SemanticAnalyzer
    SemanticAnalyzer analyzer(context.state, file_paths[0]);

    // PASS 2: BUILD SCHEMA
    if (!analyzer.build_and_analyze_declarations()) {
        result.success = false;
        result.errors.insert(result.errors.end(), analyzer.get_errors().begin(), analyzer.get_errors().end());
        log_parsing_errors(result);  // PROD-004: Log errors
        return nullptr;
    }

    // PASS 3: ANALYZE RULES
    if (!analyzer.analyze_rules_and_queries()) {
        result.success = false;
        result.errors.insert(result.errors.end(), analyzer.get_errors().begin(), analyzer.get_errors().end());
        log_parsing_errors(result);  // PROD-004: Log errors
        return nullptr;
    }

    // FINAL: Create the KnowledgeBase
    try {
        result.success = true;
        return KnowledgeBase::create(context.state);
    } catch (std::exception const& e) {
        result.success = false;
        result.errors.push_back({.message = "KnowledgeBase build failed: " + std::string(e.what())});
        log_parsing_errors(result);  // PROD-004: Log errors
        return nullptr;
    }
}

std::shared_ptr<KnowledgeBase> build_knowledge_base_from_csv(std::string const& csv_file_path, ParsingResult& result) {
    // Step 1: Parse the CSV file into a structured DecisionTable object.
    DecisionTable table = DecisionTableParser::parse(csv_file_path, result);
    if (!result.success) { return nullptr; }

    // Step 2: Convert the structured data into a RFL string.
    DecisionTableConverter converter(std::move(table));
    std::string generated_drl = converter.generate_drl();

    logd("Generated RFL from {}:\n---\n{}\n---", csv_file_path, generated_drl);

    if (generated_drl.empty()) {
        result.success = true;
        parser_state empty_state;
        return KnowledgeBase::create(empty_state);
    }

    // Step 3: Use the existing RFL parser to build the knowledge base from the generated string.
    return build_knowledge_base(generated_drl, result, csv_file_path);
}

std::shared_ptr<KnowledgeBase> build_knowledge_base_from_csv_string(std::string const& csv_content, ParsingResult& result,
                                                                     std::string const& source_name) {
    // Step 1: Parse the CSV string into a structured DecisionTable object.
    DecisionTable table = DecisionTableParser::parse_string(csv_content, source_name, result);
    if (!result.success) { return nullptr; }

    // Step 2: Convert the structured data into a RFL string.
    DecisionTableConverter converter(std::move(table));
    std::string generated_drl = converter.generate_drl();

    logd("Generated RFL from {}:\n---\n{}\n---", source_name, generated_drl);

    if (generated_drl.empty()) {
        result.success = true;
        parser_state empty_state;
        return KnowledgeBase::create(empty_state);
    }

    // Step 3: Use the existing RFL parser to build the knowledge base from the generated string.
    return build_knowledge_base(generated_drl, result, source_name);
}
