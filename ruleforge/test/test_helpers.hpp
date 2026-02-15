#ifndef TEST_HELPERS_HPP
#define TEST_HELPERS_HPP

#include "rfl_parser.hpp"
#include "knowledge_base.hpp"
#include "stateful_session.hpp"

// Helper that returns session without assertions - caller must check result
inline std::unique_ptr<StatefulSession> try_build_session(std::string const& drl, ParsingResult& out_result) {
    auto kb = build_knowledge_base(drl, out_result);
    if (!out_result.success || !kb) return nullptr;
    return kb->create_session();
}

// Helper that returns parser_state without assertions - caller must check result
inline std::optional<parser_state> try_parse_drl(std::string const& drl, ParsingResult& out_result) {
    auto kb = build_knowledge_base(drl, out_result);
    if (!out_result.success || !kb) return std::nullopt;
    return kb->get_parser_state();
}

// Macro to check parsing result and session creation inside test blocks
#define BUILD_SESSION(session_var, drl_code) \
    ParsingResult _parse_result_##session_var; \
    auto session_var = try_build_session(drl_code, _parse_result_##session_var); \
    check(_parse_result_##session_var.success); \
    for (auto const& _err : _parse_result_##session_var.errors) check(false, _err.to_string().c_str()); \
    check(session_var != nullptr)

// Macro to parse DRL and get parser_state inside test blocks
#define PARSE_DRL_SUCCESS(state_var, drl_code) \
    ParsingResult _parse_result_##state_var; \
    auto _state_opt_##state_var = try_parse_drl(drl_code, _parse_result_##state_var); \
    check(_parse_result_##state_var.success); \
    for (auto const& _err : _parse_result_##state_var.errors) check(false, _err.to_string().c_str()); \
    check(_state_opt_##state_var.has_value()); \
    auto state_var = *_state_opt_##state_var

#endif // TEST_HELPERS_HPP
