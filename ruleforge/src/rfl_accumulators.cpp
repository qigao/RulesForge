#include "rfl_accumulators.hpp"
#include "fmtlog.h"

#include <iostream>
#include <limits>   // for std::numeric_limits
#include <set>      // for std::multiset

void SumAccumulator::accumulate(ConstraintValue const& value) {
    logd("SumAccumulator::accumulate");
    if (std::holds_alternative<double>(value)) {
        is_double = true;
        sum += std::get<double>(value);
    } else if (std::holds_alternative<int64_t>(value)) {
        sum += static_cast<double>(std::get<int64_t>(value));
    }
}

void SumAccumulator::reverse(ConstraintValue const& value) {
    logd("SumAccumulator::reverse");
    if (std::holds_alternative<double>(value)) {
        sum -= std::get<double>(value);
    } else if (std::holds_alternative<int64_t>(value)) {
        sum -= static_cast<double>(std::get<int64_t>(value));
    }
}

ConstraintValue SumAccumulator::get_result() const {
    logd("SumAccumulator::get_result");
    if (is_double) { return sum; }
    return static_cast<int64_t>(sum);
}

void SumAccumulator::clear() {
    logd("SumAccumulator::clear");
    sum = 0.0;
    is_double = false;
}

std::unique_ptr<IAccumulator> SumAccumulator::clone() const {
    logd("SumAccumulator::clone");
    return std::make_unique<SumAccumulator>(*this);
}

// Note: The AccumulateNode's non-reversible path handles the logic.
// These methods can be minimal.

void CollectAccumulator::accumulate(ConstraintValue const& value) {
    logd("CollectAccumulator::accumulate");
    // No-op. The AccumulateNode itself will store the contributing facts.
}

void CollectAccumulator::reverse(ConstraintValue const& value) {
    logd("CollectAccumulator::reverse");
    // No-op. supports_reverse() is false.
}

// This method is technically no longer called by the special-cased AccumulateNode,
// but we implement it for completeness.
ConstraintValue CollectAccumulator::get_result() const {
    logd("CollectAccumulator::get_result");
    return FactList{};   // Return an empty list
}

void CollectAccumulator::clear() {
    logd("CollectAccumulator::clear");
    // No-op.
}

std::unique_ptr<IAccumulator> CollectAccumulator::clone() const {
    logd("CollectAccumulator::clone");
    return std::make_unique<CollectAccumulator>(*this);
}

// --- AverageAccumulator ---
void AverageAccumulator::accumulate(ConstraintValue const& value) {
    logd("AverageAccumulator::accumulate {}", value.index());
    count++;
    if (std::holds_alternative<double>(value)) {
        sum += std::get<double>(value);
    } else if (std::holds_alternative<int64_t>(value)) {
        sum += static_cast<double>(std::get<int64_t>(value));
    }
}

void AverageAccumulator::reverse(ConstraintValue const& value) {
    logd("AverageAccumulator::reverse {}", value.index());
    count--;
    if (std::holds_alternative<double>(value)) {
        sum -= std::get<double>(value);
    } else if (std::holds_alternative<int64_t>(value)) {
        sum -= static_cast<double>(std::get<int64_t>(value));
    }
}

ConstraintValue AverageAccumulator::get_result() const {
    logd("AverageAccumulator::get_result");
    if (count == 0) {
        return 0.0;   // Avoid division by zero
    }
    return sum / count;
}

void AverageAccumulator::clear() {
    logd("AverageAccumulator::clear");
    sum = 0.0;
    count = 0;
}

std::unique_ptr<IAccumulator> AverageAccumulator::clone() const {
    logd("AverageAccumulator::clone");
    return std::make_unique<AverageAccumulator>(*this);
}

// --- MinAccumulator ---
void MinAccumulator::accumulate(ConstraintValue const& value) {
    logd("MinAccumulator::accumulate, {}", to_string(value));
    if (std::holds_alternative<double>(value)) {
        is_double = true;
        values.insert(std::get<double>(value));
    } else if (std::holds_alternative<int64_t>(value)) {
        values.insert(static_cast<double>(std::get<int64_t>(value)));
    }
}

void MinAccumulator::reverse(ConstraintValue const& value) {
    logd("MinAccumulator::reverse {}", to_string(value));
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
    logd("MinAccumulator::get_result");
    if (values.empty()) { return is_double ? std::numeric_limits<double>::infinity() : static_cast<int64_t>(0); }
    double result = *values.begin();
    return is_double ? result : static_cast<int64_t>(result);
}

void MinAccumulator::clear() {
    logd("MinAccumulator::clear");
    values.clear();
    is_double = false;
}

std::unique_ptr<IAccumulator> MinAccumulator::clone() const {
    logd("MinAccumulator::clone");
    return std::make_unique<MinAccumulator>(*this);
}

// --- MaxAccumulator ---
void MaxAccumulator::accumulate(ConstraintValue const& value) {
    logd("MaxAccumulator::accumulate {}", to_string(value));
    if (std::holds_alternative<double>(value)) {
        is_double = true;
        values.insert(std::get<double>(value));
    } else if (std::holds_alternative<int64_t>(value)) {
        values.insert(static_cast<double>(std::get<int64_t>(value)));
    }
}

void MaxAccumulator::reverse(ConstraintValue const& value) {
    logd("MaxAccumulator::reverse {}", to_string(value));
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
    logd("MaxAccumulator::get_result");
    if (values.empty()) { return is_double ? -std::numeric_limits<double>::infinity() : static_cast<int64_t>(0); }
    double result = *values.rbegin();
    return is_double ? result : static_cast<int64_t>(result);
}

void MaxAccumulator::clear() {
    logd("MaxAccumulator::clear");
    values.clear();
    is_double = false;
}

std::unique_ptr<IAccumulator> MaxAccumulator::clone() const {
    logd("MaxAccumulator::clone");
    return std::make_unique<MaxAccumulator>(*this);
}

// --- CollectSetAccumulator ---
void CollectSetAccumulator::accumulate(ConstraintValue const& value) {
    logd("CollectSetAccumulator::accumulate {}", to_string(value));
    // No-op. The AccumulateNode itself will store the unique contributing facts.
}

void CollectSetAccumulator::reverse(ConstraintValue const& value) {
    logd("CollectSetAccumulator::reverse {}", to_string(value));
    // No-op. supports_reverse() is false.
}

// This method is technically no longer called by the special-cased AccumulateNode,
// but we implement it for completeness.
ConstraintValue CollectSetAccumulator::get_result() const {
    logd("CollectSetAccumulator::get_result");
    return FactList{};   // Return an empty list
}

void CollectSetAccumulator::clear() {
    logd("CollectSetAccumulator::clear");
    // No-op.
}

std::unique_ptr<IAccumulator> CollectSetAccumulator::clone() const {
    logd("CollectSetAccumulator::clone");
    return std::make_unique<CollectSetAccumulator>(*this);
}


