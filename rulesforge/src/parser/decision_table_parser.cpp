#include "parser/decision_table_parser.hpp"

#include <fstream>
#include <set>
#include <sstream>
#include <turbo_parser.h>

namespace {
    DecisionTable build_table_from_csv(turbo_csv_doc_t* doc) {
        DecisionTable table;
        size_t row_count = turbo_csv_row_count(doc);
        if (row_count == 0) return table;

        std::set<std::string> const preamble_keywords = {
            "PACKAGE", "IMPORT", "DECLARE", "QUERY"};
        bool header_parsed = false;

        for (size_t r = 0; r < row_count; ++r) {
            std::vector<std::string> record;

            // Determine column count by reading until we get nulls
            for (size_t c = 0;; ++c) {
                char const* val = turbo_csv_get(doc, r, c);
                if (!val) break;
                record.emplace_back(val);
            }

            if (record.empty()) continue;
            if (record.size() == 1 && record[0].empty()) continue;

            std::string const& first_col = record[0];

            if (!header_parsed && preamble_keywords.count(first_col)) {
                table.preamble_records.push_back(std::move(record));
            } else if (!header_parsed) {
                table.headers = std::move(record);
                header_parsed = true;
            } else {
                table.data.push_back(std::move(record));
            }
        }
        return table;
    }
}   // namespace

DecisionTable DecisionTableParser::parse(std::string const& file_path, ParsingResult& result) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file) {
        result.success = false;
        result.errors.push_back({file_path, 0, 0, "Cannot open file: " + file_path});
        return {};
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    return parse_string(content, file_path, result);
}

DecisionTable DecisionTableParser::parse_string(std::string const& csv_content, std::string const& source_name, ParsingResult& result) {
    turbo_csv_doc_t* doc = nullptr;
    int rc = turbo_parse_csv(
        reinterpret_cast<uint8_t const*>(csv_content.data()),
        csv_content.size(), &doc);

    if (rc != 0 || !doc) {
        result.success = false;
        result.errors.push_back({source_name, 0, 0, "CSV parse error"});
        if (doc) turbo_free_csv(doc);
        return {};
    }

    DecisionTable table = build_table_from_csv(doc);
    turbo_free_csv(doc);
    return table;
}
