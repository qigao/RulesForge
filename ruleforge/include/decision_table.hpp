#ifndef DECISION_TABLE_HPP
#define DECISION_TABLE_HPP

#include <string>
#include <vector>

struct DecisionTable {
    std::vector<std::vector<std::string>> preamble_records;   // For PACKAGE, IMPORT, DECLARE, QUERY

    std::vector<std::string> headers;
    std::vector<std::string> column_types;
    std::vector<std::string> column_templates;
    std::vector<std::vector<std::string>> data;
};

#endif   // DECISION_TABLE_HPP