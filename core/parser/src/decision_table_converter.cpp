#include "decision_table_converter.hpp"

#include <sstream>
#include <stdexcept>

namespace {
std::string_view trim(std::string_view text) {
    auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}
}

// Helper to replace "$1" with the actual value from the CSV cell.
std::string DecisionTableConverter::substitute(std::string_view template_str, std::string_view value) const {
    std::string result;
    size_t pos = template_str.find("$1");
    if (pos == std::string::npos) { return std::string(template_str); }
    result.reserve(template_str.length() - 2 + value.length());
    result.append(template_str.substr(0, pos));
    result.append(value);
    result.append(template_str.substr(pos + 2));
    return result;
}

DecisionTableConverter::DecisionTableConverter(DecisionTable const& table) : table_(table) {
    // 1. Process the cleanly separated preamble records.
    std::stringstream preamble_ss;
    for (auto const& record : table_.preamble_records) {
        if (record.empty()) continue;
        auto directive = parse_table_directive(trim(record[0]));
        size_t required = directive == TableDirective::Package || directive == TableDirective::Import ? 2 : 3;
        if (record.size() != required || trim(record[1]).empty() ||
            (required == 3 && trim(record[2]).empty()))
            throw std::invalid_argument("Incomplete decision-table preamble: " + record[0]);
        switch (directive) {
            case TableDirective::Package: preamble_ss << "package " << trim(record[1]) << ";\n"; break;
            case TableDirective::Import: preamble_ss << "import " << trim(record[1]) << ";\n"; break;
            case TableDirective::Declare:
                preamble_ss << "declare " << trim(record[1]) << "\n    " << trim(record[2]) << "\nend\n"; break;
            case TableDirective::Query:
                preamble_ss << "query \"" << trim(record[1]) << "\" " << trim(record[2]) << " end\n"; break;
        }
    }
    drl_preamble_ = preamble_ss.str();

    // 2. Process the header to define columns.
    for (size_t i = 0; i < table_.headers.size(); ++i) {
        auto header = trim(table_.headers[i]);
        auto colon = header.find(':');
        auto type = i == 0 && header.empty() ? ColumnType::RuleName :
            parse_column_type(trim(header.substr(0, colon)));
        if (type == ColumnType::Condition || type == ColumnType::Action) {
            if (colon == std::string_view::npos || trim(header.substr(colon + 1)).empty())
                throw std::invalid_argument("Decision-table condition/action requires a template");
            column_defs_.push_back({type, trim(header.substr(colon + 1))});
        } else column_defs_.push_back({type, {}});
    }
}

std::string DecisionTableConverter::generate_drl() {
    std::stringstream drl_ss;
    drl_ss << drl_preamble_ << "\n";

    for (size_t row_idx = 0; row_idx < table_.data.size(); ++row_idx) {
        auto const& record = table_.data[row_idx];
        if (record.empty()) continue;

        // --- Generate Rule Header ---
        std::string rule_name = "DecisionTable_Row_" + std::to_string(row_idx + 1);
        std::string attributes;

        // Find the column indices for rule name and attributes first.
        size_t rule_name_col = -1, salience_col = -1, agenda_group_col = -1;
        for (size_t i = 0; i < column_defs_.size(); ++i) {
            if (column_defs_[i].type == ColumnType::RuleName) rule_name_col = i;
            if (column_defs_[i].type == ColumnType::Salience) salience_col = i;
            if (column_defs_[i].type == ColumnType::AgendaGroup) agenda_group_col = i;
        }

        if (rule_name_col != -1 && rule_name_col < record.size() && !record[rule_name_col].empty()) {
            rule_name = record[rule_name_col];
        }
        if (salience_col != -1 && salience_col < record.size() && !record[salience_col].empty() &&
            record[salience_col] != "*") {
            attributes += "salience " + record[salience_col] + "\n";
        }
        if (agenda_group_col != -1 && agenda_group_col < record.size() && !record[agenda_group_col].empty() &&
            record[agenda_group_col] != "*") {
            attributes += "agenda-group \"" + record[agenda_group_col] + "\"\n";
        }

        drl_ss << "rule \"" << rule_name << "\"\n";
        if (!attributes.empty()) { drl_ss << attributes; }

        // --- Generate WHEN block ---
        drl_ss << "when\n";
        for (size_t col_idx = 0; col_idx < record.size(); ++col_idx) {
            if (col_idx >= column_defs_.size()) break;
            auto const& def = column_defs_[col_idx];
            auto value = trim(record[col_idx]);

            if (def.type == ColumnType::Condition && !value.empty() && value != "*") {
                drl_ss << "    " << substitute(def.template_text, value) << "\n";
            }
        }

        // --- Generate THEN block ---
        drl_ss << "then\n";
        for (size_t col_idx = 0; col_idx < record.size(); ++col_idx) {
            if (col_idx >= column_defs_.size()) break;
            auto const& def = column_defs_[col_idx];
            auto value = trim(record[col_idx]);

            if (def.type == ColumnType::Action && !value.empty() && value != "*") {
                // Native RHS syntax doesn't need semicolons
                drl_ss << "    " << substitute(def.template_text, value) << "\n";
            }
        }
        drl_ss << "end\n\n";
    }

    return drl_ss.str();
}
