#ifndef DECISION_TABLE_HPP
#define DECISION_TABLE_HPP

#include <string>
#include <vector>

struct DecisionTable {
    std::vector<std::vector<std::string>> preamble_records;   // For PACKAGE, IMPORT, DECLARE, QUERY

    std::vector<std::string> header;
    std::vector<std::vector<std::string>> records;
};

#endif   // DECISION_TABLE_HPP