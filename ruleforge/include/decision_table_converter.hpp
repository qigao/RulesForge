#ifndef DECISION_TABLE_CONVERTER_HPP
#define DECISION_TABLE_CONVERTER_HPP

#include "decision_table.hpp"

#include <string>

class DecisionTableConverter {
public:
    explicit DecisionTableConverter(DecisionTable table);
    std::string generate_drl();

private:
    struct ColumnDefinition {
        std::string type;   // "CONDITION", "ACTION", "Rule Name", "Salience"
        std::string template_text;
    };

    std::string substitute(std::string_view template_str, std::string_view value) const;

    DecisionTable table_;
    std::vector<ColumnDefinition> column_defs_;
    std::string drl_preamble_;
};

#endif   // DECISION_TABLE_CONVERTER_HPP

