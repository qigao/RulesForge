#ifndef DECISION_TABLE_PARSER_HPP
#define DECISION_TABLE_PARSER_HPP

#include "decision_table.hpp"
#include "errors.hpp"   // For ParsingResult

#include <string>

class DecisionTableParser {
public:
    static DecisionTable parse(std::string const& file_path, ParsingResult& result);
    static DecisionTable parse_string(std::string const& csv_content, std::string const& source_name, ParsingResult& result);
};

#endif   // DECISION_TABLE_PARSER_HPP