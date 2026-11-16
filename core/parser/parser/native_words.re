#include "model/constraint_types.hpp"
#include "execution/scalar_function.hpp"
#include "rhs_builtins.hpp"
#include "decision_table_converter.hpp"
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>

TableDirective parse_table_directive(std::string_view s) {
    if (s.size() >= 64) { throw std::invalid_argument("Unknown decision-table directive: " + std::string(s)); }
    std::array<char, 64> buffer{};
    if (!s.empty()) std::memcpy(buffer.data(), s.data(), s.size());
    char const* YYCURSOR = buffer.data(), *YYLIMIT = buffer.data() + s.size();
    [[maybe_unused]] char const* YYMARKER = YYCURSOR;
    auto accept = [&](TableDirective value) {
        if (YYCURSOR != YYLIMIT) { throw std::invalid_argument("Unknown decision-table directive: " + std::string(s)); }
        return value;
    };
    /*!re2c
        re2c:define:YYCTYPE = char;
        re2c:yyfill:enable = 0;
        re2c:eof = 0;
        [pP] [aA] [cC] [kK] [aA] [gG] [eE] { return accept(TableDirective::Package); }
        [iI] [mM] [pP] [oO] [rR] [tT] { return accept(TableDirective::Import); }
        [dD] [eE] [cC] [lL] [aA] [rR] [eE] { return accept(TableDirective::Declare); }
        [qQ] [uU] [eE] [rR] [yY] { return accept(TableDirective::Query); }
        * { throw std::invalid_argument("Unknown decision-table directive: " + std::string(s)); }
        $ { throw std::invalid_argument("Unknown decision-table directive: " + std::string(s)); }
    */
}
ColumnType parse_column_type(std::string_view s) {
    if (s.size() >= 64) { return ColumnType::Other; }
    std::array<char, 64> buffer{};
    if (!s.empty()) std::memcpy(buffer.data(), s.data(), s.size());
    char const* YYCURSOR = buffer.data(), *YYLIMIT = buffer.data() + s.size();
    [[maybe_unused]] char const* YYMARKER = YYCURSOR;
    auto accept = [&](ColumnType value) {
        if (YYCURSOR != YYLIMIT) { return ColumnType::Other; }
        return value;
    };
    /*!re2c
        re2c:define:YYCTYPE = char;
        re2c:yyfill:enable = 0;
        re2c:eof = 0;
        [cC] [oO] [nN] [dD] [iI] [tT] [iI] [oO] [nN] { return accept(ColumnType::Condition); }
        [aA] [cC] [tT] [iI] [oO] [nN] { return accept(ColumnType::Action); }
        [rR] [uU] [lL] [eE] " " [nN] [aA] [mM] [eE] { return accept(ColumnType::RuleName); }
        [sS] [aA] [lL] [iI] [eE] [nN] [cC] [eE] { return accept(ColumnType::Salience); }
        [aA] [gG] [eE] [nN] [dD] [aA] "-" [gG] [rR] [oO] [uU] [pP] { return accept(ColumnType::AgendaGroup); }
        * { return ColumnType::Other; }
        $ { return ColumnType::Other; }
    */
}

CompareOp parse_compare_op(std::string_view s) {
    if (s.size() >= 64) { throw std::invalid_argument("Unknown parse_compare_op: " + std::string(s)); }
    // Views may end at any byte: provide the sentinel required by the DFA.
    std::array<char, 64> buffer{};
    if (!s.empty()) std::memcpy(buffer.data(), s.data(), s.size());
    char const* YYCURSOR = buffer.data();
    char const* YYLIMIT = buffer.data() + s.size();
    [[maybe_unused]] char const* YYMARKER = YYCURSOR;
    auto accept = [&](CompareOp value) {
        if (YYCURSOR != YYLIMIT) { throw std::invalid_argument("Unknown parse_compare_op: " + std::string(s)); }
        return value;
    };
    /*!re2c
        re2c:define:YYCTYPE = char;
        re2c:yyfill:enable = 0;
        re2c:eof = 0;
        "==" { return accept(CompareOp::EQ); }
        "!=" { return accept(CompareOp::NE); }
        ">" { return accept(CompareOp::GT); }
        "<" { return accept(CompareOp::LT); }
        ">=" { return accept(CompareOp::GE); }
        "<=" { return accept(CompareOp::LE); }
        "has_flag" { return accept(CompareOp::HasFlag); }
        "contains" { return accept(CompareOp::Contains); }
        "not contains" { return accept(CompareOp::NotContains); }
        "matches" { return accept(CompareOp::Matches); }
        "not matches" { return accept(CompareOp::NotMatches); }
        "startsWith" { return accept(CompareOp::StartsWith); }
        "startswith" { return accept(CompareOp::StartsWith); }
        "endsWith" { return accept(CompareOp::EndsWith); }
        "endswith" { return accept(CompareOp::EndsWith); }
        "lengthIs" { return accept(CompareOp::LengthIs); }
        "lengthis" { return accept(CompareOp::LengthIs); }
        "memberOf" { return accept(CompareOp::MemberOf); }
        "memberof" { return accept(CompareOp::MemberOf); }
        "not memberOf" { return accept(CompareOp::NotMemberOf); }
        "not memberof" { return accept(CompareOp::NotMemberOf); }
        "in" { return accept(CompareOp::In); }
        "not in" { return accept(CompareOp::NotIn); }
        "containsKey" { return accept(CompareOp::ContainsKey); }
        "containskey" { return accept(CompareOp::ContainsKey); }
        "not containsKey" { return accept(CompareOp::NotContainsKey); }
        "not containskey" { return accept(CompareOp::NotContainsKey); }
        * { throw std::invalid_argument("Unknown parse_compare_op: " + std::string(s)); }
        $ { return CompareOp::None; }
    */
}
TemporalOp parse_temporal_op(std::string_view s) {
    if (s.size() >= 64) { throw std::invalid_argument("Unknown parse_temporal_op: " + std::string(s)); }
    // Views may end at any byte: provide the sentinel required by the DFA.
    std::array<char, 64> buffer{};
    if (!s.empty()) std::memcpy(buffer.data(), s.data(), s.size());
    char const* YYCURSOR = buffer.data();
    char const* YYLIMIT = buffer.data() + s.size();
    [[maybe_unused]] char const* YYMARKER = YYCURSOR;
    auto accept = [&](TemporalOp value) {
        if (YYCURSOR != YYLIMIT) { throw std::invalid_argument("Unknown parse_temporal_op: " + std::string(s)); }
        return value;
    };
    /*!re2c
        re2c:define:YYCTYPE = char;
        re2c:yyfill:enable = 0;
        re2c:eof = 0;
        "after" { return accept(TemporalOp::After); }
        "before" { return accept(TemporalOp::Before); }
        "within" { return accept(TemporalOp::Within); }
        "coincides" { return accept(TemporalOp::Coincides); }
        "during" { return accept(TemporalOp::During); }
        * { throw std::invalid_argument("Unknown parse_temporal_op: " + std::string(s)); }
        $ { return TemporalOp::None; }
    */
}
namespace rulesforge {
ScalarFunction parse_scalar_function(std::string_view s) {
    if (s.size() >= 64) { throw std::invalid_argument("Unknown parse_scalar_function: " + std::string(s)); }
    // Views may end at any byte: provide the sentinel required by the DFA.
    std::array<char, 64> buffer{};
    if (!s.empty()) std::memcpy(buffer.data(), s.data(), s.size());
    char const* YYCURSOR = buffer.data();
    char const* YYLIMIT = buffer.data() + s.size();
    [[maybe_unused]] char const* YYMARKER = YYCURSOR;
    auto accept = [&](ScalarFunction value) {
        if (YYCURSOR != YYLIMIT) { throw std::invalid_argument("Unknown parse_scalar_function: " + std::string(s)); }
        return value;
    };
    /*!re2c
        re2c:define:YYCTYPE = char;
        re2c:yyfill:enable = 0;
        re2c:eof = 0;
        "floor" { return accept(ScalarFunction::Floor); }
        "ceil" { return accept(ScalarFunction::Ceil); }
        "abs" { return accept(ScalarFunction::Abs); }
        "round" { return accept(ScalarFunction::Round); }
        "sqrt" { return accept(ScalarFunction::Sqrt); }
        "sin" { return accept(ScalarFunction::Sin); }
        "cos" { return accept(ScalarFunction::Cos); }
        "tan" { return accept(ScalarFunction::Tan); }
        "acos" { return accept(ScalarFunction::Acos); }
        "asin" { return accept(ScalarFunction::Asin); }
        "atan" { return accept(ScalarFunction::Atan); }
        "log" { return accept(ScalarFunction::Log); }
        "exp" { return accept(ScalarFunction::Exp); }
        "min" { return accept(ScalarFunction::Min); }
        "max" { return accept(ScalarFunction::Max); }
        "pow" { return accept(ScalarFunction::Pow); }
        "fmod" { return accept(ScalarFunction::Fmod); }
        * { throw std::invalid_argument("Unknown parse_scalar_function: " + std::string(s)); }
        $ { throw std::invalid_argument("Unknown parse_scalar_function: " + std::string(s)); }
    */
}
} // namespace rulesforge
namespace rulesforge {
bool is_rhs_builtin(std::string_view s) {
    if (s.size() >= 64) { return false; }
    // Views may end at any byte: provide the sentinel required by the DFA.
    std::array<char, 64> buffer{};
    if (!s.empty()) std::memcpy(buffer.data(), s.data(), s.size());
    char const* YYCURSOR = buffer.data();
    char const* YYLIMIT = buffer.data() + s.size();
    [[maybe_unused]] char const* YYMARKER = YYCURSOR;
    auto accept = [&](bool value) {
        if (YYCURSOR != YYLIMIT) { return false; }
        return value;
    };
    /*!re2c
        re2c:define:YYCTYPE = char;
        re2c:yyfill:enable = 0;
        re2c:eof = 0;
        "floor" { return accept(true); }
        "ceil" { return accept(true); }
        "abs" { return accept(true); }
        "min" { return accept(true); }
        "max" { return accept(true); }
        "round" { return accept(true); }
        "pow" { return accept(true); }
        "sqrt" { return accept(true); }
        "log" { return accept(true); }
        "exp" { return accept(true); }
        "sin" { return accept(true); }
        "cos" { return accept(true); }
        "tan" { return accept(true); }
        "fmod" { return accept(true); }
        "trunc" { return accept(true); }
        "asin" { return accept(true); }
        "acos" { return accept(true); }
        "atan" { return accept(true); }
        "atan2" { return accept(true); }
        "sinh" { return accept(true); }
        "cosh" { return accept(true); }
        "tanh" { return accept(true); }
        "sgn" { return accept(true); }
        "root" { return accept(true); }
        "erf" { return accept(true); }
        "log2" { return accept(true); }
        "log10" { return accept(true); }
        "log1p" { return accept(true); }
        "expm1" { return accept(true); }
        "hypot" { return accept(true); }
        "concat" { return accept(true); }
        "strlen" { return accept(true); }
        "substr" { return accept(true); }
        "trim" { return accept(true); }
        "replace" { return accept(true); }
        "indexOf" { return accept(true); }
        "contains" { return accept(true); }
        "now_ms" { return accept(true); }
        "upper" { return accept(true); }
        "lower" { return accept(true); }
        "toUpper" { return accept(true); }
        "toLower" { return accept(true); }
        "to_upper" { return accept(true); }
        "to_lower" { return accept(true); }
        "year" { return accept(true); }
        "month" { return accept(true); }
        "day" { return accept(true); }
        "to_string" { return accept(true); }
        "to_number" { return accept(true); }
        "is_null" { return accept(true); }
        "is_not_null" { return accept(true); }
        "uuid" { return accept(true); }
        "random" { return accept(true); }
        "plugin_load" { return accept(true); }
        * { return false; }
        $ { return false; }
    */
}
} // namespace rulesforge
