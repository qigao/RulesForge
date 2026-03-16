#include "codec/csv_fact_parser.hpp"
#include <sstream>

namespace rulesforge {

thread_local std::string CsvFactParser::last_error_;

std::vector<std::string> CsvFactParser::parse_csv_line(std::string const& line) {
    std::vector<std::string> fields;
    std::string field;
    bool in_quotes = false;

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '"') {
            in_quotes = !in_quotes;
        } else if (c == ',' && !in_quotes) {
            fields.push_back(field);
            field.clear();
        } else {
            field += c;
        }
    }
    fields.push_back(field);
    return fields;
}

std::vector<Fact*> CsvFactParser::parse(
    rulesforge::FactArena& arena,
    std::string const& type_name,
    std::string const& csv_str,
    std::vector<ParsedDeclaration> const& declarations
) {
    last_error_.clear();
    std::vector<Fact*> facts;

    // Find declaration
    ParsedDeclaration const* decl = nullptr;
    for (auto const& d : declarations) {
        if (d.type_name == type_name) {
            decl = &d;
            break;
        }
    }
    if (!decl) {
        last_error_ = "Type not found: " + type_name;
        return facts;
    }

    std::istringstream iss(csv_str);
    std::string line;

    // Parse header
    if (!std::getline(iss, line)) {
        last_error_ = "Empty CSV";
        return facts;
    }
    auto headers = parse_csv_line(line);

    // Parse data rows
    while (std::getline(iss, line)) {
        if (line.empty()) continue;

        auto values = parse_csv_line(line);
        if (values.size() != headers.size()) {
            last_error_ = "Column count mismatch";
            return facts;
        }

        Fact* fact = arena.create_fact();
        fact->type = type_name;

        for (size_t i = 0; i < headers.size(); ++i) {
            std::string const& field_name = headers[i];
            std::string const& value_str = values[i];

            // Find field type
            ParsedField const* field = nullptr;
            for (auto const& f : decl->fields) {
                if (f.name == field_name) {
                    field = &f;
                    break;
                }
            }

            if (!field) continue; // Skip unknown fields

            // Convert value
            if (field->type & FT_String) {
                fact->fields[field_name] = value_str;
            } else if (field->type & (FT_Int | FT_Long)) {
                fact->fields[field_name] = static_cast<int64_t>(std::stoll(value_str));
            } else if (field->type & (FT_Double | FT_Float)) {
                fact->fields[field_name] = std::stod(value_str);
            } else if (field->type & FT_Boolean) {
                fact->fields[field_name] = static_cast<int64_t>(value_str == "true" || value_str == "1");
            }
        }

        facts.push_back(fact);
    }

    return facts;
}

} // namespace rulesforge
