#ifndef RUNTIME_PREDICATE_HPP
#define RUNTIME_PREDICATE_HPP

#include "core/constraint_types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rulesforge {

enum class RuntimePredicateKind : uint8_t {
    Compare,
    Temporal,
    StringContains,
    StringMatches,
    StringAffix,
    StringLengthIs,
    MapContainsKey,
    CollectionContains,
    NumericLiteral,
    NumericExpression,
    ValueList,
    EvalExpression,
    External,
};

enum class RuntimePredicateBackend : uint8_t {
    Native,
};

struct RuntimePredicateRef {
    RuntimePredicateBackend backend = RuntimePredicateBackend::Native;
    RuntimePredicateKind kind = RuntimePredicateKind::Compare;
    std::size_t predicate_id = 0;
    CompareOp compare_op = CompareOp::None;
    TemporalOp temporal_op = TemporalOp::None;
    std::int64_t window_ms = 0;
    std::string external_name;

    RuntimePredicateRef() = default;
    RuntimePredicateRef(RuntimePredicateKind kind_,
                        std::size_t predicate_id_ = 0,
                        CompareOp compare_op_ = CompareOp::None,
                        TemporalOp temporal_op_ = TemporalOp::None,
                        std::int64_t window_ms_ = 0,
                        std::string external_name_ = {})
        : backend(RuntimePredicateBackend::Native),
          kind(kind_),
          predicate_id(predicate_id_),
          compare_op(compare_op_),
          temporal_op(temporal_op_),
          window_ms(window_ms_),
          external_name(std::move(external_name_)) {}

    RuntimePredicateRef(RuntimePredicateBackend backend_,
                        RuntimePredicateKind kind_,
                        std::size_t predicate_id_ = 0,
                        CompareOp compare_op_ = CompareOp::None,
                        TemporalOp temporal_op_ = TemporalOp::None,
                        std::int64_t window_ms_ = 0,
                        std::string external_name_ = {})
        : backend(backend_),
          kind(kind_),
          predicate_id(predicate_id_),
          compare_op(compare_op_),
          temporal_op(temporal_op_),
          window_ms(window_ms_),
          external_name(std::move(external_name_)) {}
};

} // namespace rulesforge

#endif // RUNTIME_PREDICATE_HPP
