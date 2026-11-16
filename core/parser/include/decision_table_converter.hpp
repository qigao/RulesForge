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

ColumnType parse_column_type(std::string_view s);
enum class TableDirective { Package, Import, Declare, Query };
TableDirective parse_table_directive(std::string_view s);

class DecisionTableConverter {
public:
    // Borrows an immutable table until conversion finishes.
    explicit DecisionTableConverter(DecisionTable const& table);
    DecisionTableConverter(DecisionTable&&) = delete;
    std::string generate_drl();

private:
    struct ColumnDefinition {
        ColumnType type;
        std::string_view template_text;
    };

    std::string substitute(std::string_view template_str, std::string_view value) const;

    DecisionTable const& table_;
    std::vector<ColumnDefinition> column_defs_;
    std::string drl_preamble_;
};

#endif   // DECISION_TABLE_CONVERTER_HPP

