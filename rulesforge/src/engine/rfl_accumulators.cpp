#include "engine/rfl_accumulators.hpp"
#include "core/logging_control.hpp"

#include <limits>   // for std::numeric_limits
#include <set>      // for std::multiset

void SumAccumulator::accumulate(ConstraintValue const& value) {
    if (std::holds_alternative<double>(value)) {
        is_double = true;
        sum += std::get<double>(value);
    } else if (std::holds_alternative<int64_t>(value)) {
        sum += static_cast<double>(std::get<int64_t>(value));
    }
}

void SumAccumulator::reverse(ConstraintValue const& value) {
    if (std::holds_alternative<double>(value)) {
        sum -= std::get<double>(value);
    } else if (std::holds_alternative<int64_t>(value)) {
        sum -= static_cast<double>(std::get<int64_t>(value));
    }
}

ConstraintValue SumAccumulator::get_result() const {
    if (is_double) { return sum; }
    return static_cast<int64_t>(sum);
}

void SumAccumulator::clear() {
    sum = 0.0;
    is_double = false;
}



// Note: The AccumulateNode's non-reversible path handles the logic.
// These methods can be minimal.

void CollectAccumulator::accumulate(ConstraintValue const& value) {
    // No-op. The AccumulateNode itself will store the contributing facts.
}

void CollectAccumulator::reverse(ConstraintValue const& value) {
    // No-op. supports_reverse() is false.
}

// This method is technically no longer called by the special-cased AccumulateNode,
// but we implement it for completeness.
ConstraintValue CollectAccumulator::get_result() const {
    return FactList{};   // Return an empty list
}

void CollectAccumulator::clear() {
    // No-op.
}



// --- AverageAccumulator ---
void AverageAccumulator::accumulate(ConstraintValue const& value) {
    count++;
    if (std::holds_alternative<double>(value)) {
        sum += std::get<double>(value);
    } else if (std::holds_alternative<int64_t>(value)) {
        sum += static_cast<double>(std::get<int64_t>(value));
    }
}

void AverageAccumulator::reverse(ConstraintValue const& value) {
    count--;
    if (std::holds_alternative<double>(value)) {
        sum -= std::get<double>(value);
    } else if (std::holds_alternative<int64_t>(value)) {
        sum -= static_cast<double>(std::get<int64_t>(value));
    }
}

ConstraintValue AverageAccumulator::get_result() const {
    if (count == 0) {
        return 0.0;   // Avoid division by zero
    }
    return sum / count;
}

void AverageAccumulator::clear() {
    sum = 0.0;
    count = 0;
}



// --- MinAccumulator ---
void MinAccumulator::accumulate(ConstraintValue const& value) {
    if (std::holds_alternative<double>(value)) {
        is_double = true;
        values.insert(std::get<double>(value));
    } else if (std::holds_alternative<int64_t>(value)) {
        values.insert(static_cast<double>(std::get<int64_t>(value)));
    }
}

void MinAccumulator::reverse(ConstraintValue const& value) {
    double val_to_remove = 0.0;
    if (std::holds_alternative<double>(value)) {
        val_to_remove = std::get<double>(value);
    } else if (std::holds_alternative<int64_t>(value)) {
        val_to_remove = static_cast<double>(std::get<int64_t>(value));
    }

    auto it = values.find(val_to_remove);
    if (it != values.end()) { values.erase(it); }
}

ConstraintValue MinAccumulator::get_result() const {
    if (values.empty()) { return is_double ? std::numeric_limits<double>::infinity() : static_cast<int64_t>(0); }
    double result = *values.begin();
    return is_double ? result : static_cast<int64_t>(result);
}

void MinAccumulator::clear() {
    values.clear();
    is_double = false;
}



// --- MaxAccumulator ---
void MaxAccumulator::accumulate(ConstraintValue const& value) {
    if (std::holds_alternative<double>(value)) {
        is_double = true;
        values.insert(std::get<double>(value));
    } else if (std::holds_alternative<int64_t>(value)) {
        values.insert(static_cast<double>(std::get<int64_t>(value)));
    }
}

void MaxAccumulator::reverse(ConstraintValue const& value) {
    double val_to_remove = 0.0;
    if (std::holds_alternative<double>(value)) {
        val_to_remove = std::get<double>(value);
    } else if (std::holds_alternative<int64_t>(value)) {
        val_to_remove = static_cast<double>(std::get<int64_t>(value));
    }

    auto it = values.find(val_to_remove);
    if (it != values.end()) { values.erase(it); }
}

ConstraintValue MaxAccumulator::get_result() const {
    if (values.empty()) { return is_double ? -std::numeric_limits<double>::infinity() : static_cast<int64_t>(0); }
    double result = *values.rbegin();
    return is_double ? result : static_cast<int64_t>(result);
}

void MaxAccumulator::clear() {
    values.clear();
    is_double = false;
}



// --- CollectSetAccumulator ---
void CollectSetAccumulator::accumulate(ConstraintValue const& value) {
    // No-op. The AccumulateNode itself will store the unique contributing facts.
}

void CollectSetAccumulator::reverse(ConstraintValue const& value) {
    // No-op. supports_reverse() is false.
}

// This method is technically no longer called by the special-cased AccumulateNode,
// but we implement it for completeness.
ConstraintValue CollectSetAccumulator::get_result() const {
    return FactList{};   // Return an empty list
}

void CollectSetAccumulator::clear() {
    // No-op.
}




