#ifndef CSV_FACT_PARSER_HPP
#define CSV_FACT_PARSER_HPP

#include "core/fact.hpp"
#include "core/constraint_types.hpp"
#include "data/fact_arena.hpp"
#include <string>
#include <vector>

namespace rulesforge {

/**
 * @brief CSV parser for batch Fact creation
 *
 * Format: header row + data rows
 * Example:
 *   name,age,city
 *   John,30,London
 *   Amy,25,Paris
 */
class CsvFactParser {
public:
    /**
     * @brief Parse CSV string to Facts
     * @param arena Arena for Fact allocation
     * @param type_name Fact type
     * @param csv_str CSV content (with header)
     * @param declarations Type schemas
     * @return Vector of Facts
     */
    static std::vector<Fact*> parse(
        rulesforge::FactArena& arena,
        std::string const& type_name,
        std::string const& csv_str,
        std::vector<ParsedDeclaration> const& declarations
    );

    static std::string get_last_error() { return last_error_; }

private:
    static thread_local std::string last_error_;

    static std::vector<std::string> parse_csv_line(std::string const& line);
};

} // namespace rulesforge

#endif // CSV_FACT_PARSER_HPP
