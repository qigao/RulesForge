#include "drools_parser_state.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

// --- JSON serialization for parser_state ---
void to_json(json& j, parser_state const& p) {
    j = json{{"package_name", p.package_name},         {"parsed_imports", p.parsed_imports},
             {"parsed_rules", p.parsed_rules},         {"parsed_queries", p.parsed_queries},
             {"parsed_functions", p.parsed_functions}, {"parsed_declarations", p.parsed_declarations},
             {"parsed_globals", p.parsed_globals}};
}

void from_json(json const& j, parser_state& p) {
    if (j.contains("package_name")) { j.at("package_name").get_to(p.package_name); }
    if (j.contains("parsed_imports")) { j.at("parsed_imports").get_to(p.parsed_imports); }
    if (j.contains("parsed_rules")) { j.at("parsed_rules").get_to(p.parsed_rules); }
    if (j.contains("parsed_queries")) { j.at("parsed_queries").get_to(p.parsed_queries); }
    if (j.contains("parsed_functions")) { j.at("parsed_functions").get_to(p.parsed_functions); }
    if (j.contains("parsed_declarations")) { j.at("parsed_declarations").get_to(p.parsed_declarations); }
    if (j.contains("parsed_globals")) { j.at("parsed_globals").get_to(p.parsed_globals); }
}
