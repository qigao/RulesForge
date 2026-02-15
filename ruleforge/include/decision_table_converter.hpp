#ifndef DECISION_TABLE_CONVERTER_HPP
#define DECISION_TABLE_CONVERTER_HPP

#include "decision_table.hpp"

#include <string>

/**
 * @brief Column types for decision table (enum for O(1) comparison)
 */
enum class ColumnType : uint8_t {
    Condition,
    Action,
    RuleName,
    Salience,
    AgendaGroup,
    Other
};

inline ColumnType parse_column_type(std::string_view s) {
    if (s == "CONDITION") return ColumnType::Condition;
    if (s == "ACTION") return ColumnType::Action;
    if (s == "Rule Name") return ColumnType::RuleName;
    if (s == "Salience") return ColumnType::Salience;
    if (s == "agenda-group") return ColumnType::AgendaGroup;
    return ColumnType::Other;
}

class DecisionTableConverter {
public:
    explicit DecisionTableConverter(DecisionTable table);
    std::string generate_drl();

private:
    struct ColumnDefinition {
        ColumnType type;
        std::string template_text;
    };

    std::string substitute(std::string_view template_str, std::string_view value) const;

    DecisionTable table_;
    std::vector<ColumnDefinition> column_defs_;
    std::string drl_preamble_;
};

#endif   // DECISION_TABLE_CONVERTER_HPP

