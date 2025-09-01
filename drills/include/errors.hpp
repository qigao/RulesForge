// FILE: errors.hpp
#ifndef ERRORS_HPP
#define ERRORS_HPP

#include <sstream>
#include <string>
#include <vector>

/**
 * @brief Represents a single, structured error from parsing or analysis.
 *
 * Contains detailed information about the error's location and message,
 * allowing for user-friendly display in logs or a UI.
 */
struct StructuredError {
    std::string file_name;
    size_t line = 0;
    size_t column = 0;
    std::string message;

    /**
     * @brief Formats the error into a standard compiler-style string.
     * @return A user-friendly, formatted error message.
     */
    std::string to_string() const {
        std::stringstream ss;
        if (!file_name.empty() && line > 0) { ss << file_name << ":" << line << ":" << column << ": "; }
        ss << "error: " << message;
        return ss.str();
    }
};

/**
 * @brief Represents the complete result of a parsing and analysis operation.
 *
 * This structure is returned by the main parsing function, indicating
 * overall success and containing a list of all errors found.
 */
struct ParsingResult {
    bool success = true;
    std::vector<StructuredError> errors;
};

#endif   // ERRORS_HPP
