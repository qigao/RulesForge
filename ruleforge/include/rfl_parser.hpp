#ifndef RFL_PARSER_HPP
#define RFL_PARSER_HPP

#include "rfl_parser_state.hpp"
#include "errors.hpp"
#include "knowledge_base.hpp"

#include <memory>
#include <set>
#include <string>
#include <vector>

class ReteNetwork;


/**
 * @brief Parses a RFL string, analyzes it, and builds a KnowledgeBase.
 *
 * This is the main entry point for the rule engine's compiler. It returns
 * an immutable, thread-safe KnowledgeBase ready for creating sessions.
 *
 * @param input The RFL source code as a string.
 * @param result Output parameter for the success status and a list of any errors.
 * @param source_name The name of the source file (e.g., "my_rules.rfl") for error reporting.
 * @return A shared_ptr to the compiled KnowledgeBase, or nullptr on failure.
 */
std::shared_ptr<KnowledgeBase> build_knowledge_base(std::string const& input, ParsingResult& result,
                                                    std::string const& source_name = "string.rfl");

/**
 * @brief Overload that parses a RFL file directly from its path.
 *
 * @param result Output parameter for the success status and a list of any errors.
 * @param file_path The path to the RFL file to be parsed.
 * @return A shared_ptr to the compiled KnowledgeBase, or nullptr on failure.
 */
std::shared_ptr<KnowledgeBase> build_knowledge_base(ParsingResult& result, std::string const& file_path);

/**
 * @brief The main entry point for parsing RFL files with import resolution.
 *
 * This function parses a root RFL file and recursively parses any files
 * specified via `import` statements, searching for them in the provided unqualified
 * base directories.
 *
 * @param file_path The path to the root RFL file.
 * @param base_dirs A list of directories to search for imported files.
 * @param result Output parameter for success and errors.
 * @return A shared_ptr to the compiled KnowledgeBase, or nullptr on failure.
 */
std::shared_ptr<KnowledgeBase> build_knowledge_base(std::string const& file_path,
                                                    std::vector<std::string> const& base_dirs, ParsingResult& result);

/**
 * @brief Parses multiple RFL files and merges them into a single KnowledgeBase.
 *
 * @param file_paths A list of RFL file paths to be parsed and merged.
 * @param base_dirs A list of directories to search for imported files.
 * @param result Output parameter for success and errors.
 * @return A shared_ptr to the compiled KnowledgeBase, or nullptr on failure.
 */
std::shared_ptr<KnowledgeBase> build_knowledge_base(std::vector<std::string> const& file_paths,
                                                    std::vector<std::string> const& base_dirs,
                                                    ParsingResult& result);

/**
 * @brief Overload that parses a RFL file directly from its path.
 *
 * @param result Output parameter for the success status and a list of any errors.
 * @param file_path The path to the RFL file to be parsed.
 * @return A shared_ptr to the compiled KnowledgeBase, or nullptr on failure.
 */
std::shared_ptr<KnowledgeBase> build_knowledge_base(ParsingResult& result, std::string const& file_path);

/**
 * @brief Parses a RFL CSV decision table, converts it to RFL, and builds a KnowledgeBase.
 *
 * @param csv_file_path The path to the CSV decision table file.
 * @param result Output parameter for success and errors.
 * @return A shared_ptr to the compiled KnowledgeBase, or nullptr on failure.
 */
std::shared_ptr<KnowledgeBase> build_knowledge_base_from_csv(std::string const& csv_file_path, ParsingResult& result);

/**
 * @brief Parses a CSV decision table from a string, converts it to RFL, and builds a KnowledgeBase.
 *
 * @param csv_content The CSV content as a string.
 * @param result Output parameter for success and errors.
 * @param source_name The name of the source for error reporting.
 * @return A shared_ptr to the compiled KnowledgeBase, or nullptr on failure.
 */
std::shared_ptr<KnowledgeBase> build_knowledge_base_from_csv_string(std::string const& csv_content, ParsingResult& result,
                                                                     std::string const& source_name = "csv_string");

#endif   // RFL_PARSER_HPP


