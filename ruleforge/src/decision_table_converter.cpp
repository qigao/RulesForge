#include "decision_table_converter.hpp"

#include <iostream>
#include <sstream>

// Helper to replace "$1" with the actual value from the CSV cell.
std::string DecisionTableConverter::substitute(std::string_view template_str, std::string_view value) const {
    std::string result;
    size_t last_pos = 0;
    size_t pos = template_str.find("$1");
    if (pos == std::string::npos) { return std::string(template_str); }
    result.reserve(template_str.length() - 2 + value.length());
    result.append(template_str.substr(0, pos));
    result.append(value);
    result.append(template_str.substr(pos + 2));
    return result;
}

DecisionTableConverter::DecisionTableConverter(DecisionTable table) : table_(std::move(table)) {
    // 1. Process the cleanly separated preamble records.
    std::stringstream preamble_ss;
    for (auto const& record : table_.preamble_records) {
        if (record.empty()) continue;
        std::string const& directive = record[0];
        if (directive == "PACKAGE" && record.size() > 1) {
            preamble_ss << "package " << record[1] << ";\n";
        } else if (directive == "IMPORT" && record.size() > 1) {
            preamble_ss << "import " << record[1] << ";\n";
        } else if (directive == "DECLARE" && record.size() > 2) {
            preamble_ss << "declare " << record[1] << "\n    " << record[2] << "\nend\n";
        } else if (directive == "QUERY" && record.size() > 2) {
            preamble_ss << "query \"" << record[1] << "\" " << record[2] << " end\n";
        }
    }
    drl_preamble_ = preamble_ss.str();

    // 2. Process the header to define columns.
    for (std::string const& header_text : table_.headers) {
        if (header_text.rfind("CONDITION: ", 0) == 0) {
            column_defs_.push_back({"CONDITION", header_text.substr(11)});
        } else if (header_text.rfind("ACTION: ", 0) == 0) {
            column_defs_.push_back({"ACTION", header_text.substr(8)});
        } else {
            column_defs_.push_back({header_text, ""});   // It's a metadata column like "Rule Name".
        }
    }
}

std::string DecisionTableConverter::generate_drl() {
    if (table_.data.empty()) return "";

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
            if (column_defs_[i].type == "Rule Name") rule_name_col = i;
            if (column_defs_[i].type == "Salience") salience_col = i;
            if (column_defs_[i].type == "agenda-group") agenda_group_col = i;
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
            auto const& value = record[col_idx];

            if (def.type == "CONDITION" && !value.empty() && value != "*") {
                drl_ss << "    " << substitute(def.template_text, value) << "\n";
            }
        }

        // --- Generate THEN block ---
        drl_ss << "then\n";
        for (size_t col_idx = 0; col_idx < record.size(); ++col_idx) {
            if (col_idx >= column_defs_.size()) break;
            auto const& def = column_defs_[col_idx];
            auto const& value = record[col_idx];

            if (def.type == "ACTION" && !value.empty() && value != "*") {
                // Add the necessary semicolon to make it a valid statement.
                drl_ss << "    " << substitute(def.template_text, value) << ";\n";
            }
        }
        drl_ss << "end\n\n";
    }

    return drl_ss.str();
}