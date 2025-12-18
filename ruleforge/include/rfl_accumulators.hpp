#ifndef RFL_ACCUMULATORS_HPP
#define RFL_ACCUMULATORS_HPP

#include "rfl_rete_defs.hpp"

#include <map>
#include <memory>
#include <set>
#include "phmap.h"

// The interface for all custom accumulator functions
struct IAccumulator {
    virtual ~IAccumulator() = default;

    // Called for each matching fact when it is asserted
    virtual void accumulate(ConstraintValue const& value) = 0;

    // Called for each matching fact when it is retracted
    virtual void reverse(ConstraintValue const& value) = 0;

    // Get the final result of the accumulation
    virtual ConstraintValue get_result() const = 0;

    // Reset the accumulator to its initial state
    virtual void clear() = 0;

    // Create a new instance of this accumulator type (for prototyping)
    virtual std::unique_ptr<IAccumulator> clone() const = 0;

    // Check if the accumulator supports efficient reversal
    virtual bool supports_reverse() const { return false; }
};

// --- Accumulator Registry for Extensibility ---
class AccumulatorRegistry {
public:
    void register_accumulator(std::string const& name, std::unique_ptr<IAccumulator> prototype) {
        prototypes_[name] = std::move(prototype);
    }

    // This method can be const because it returns a pointer to a prototype, not modifying the registry itself.
    IAccumulator* get_prototype(std::string const& name) const {
        auto it = prototypes_.find(name);
        return (it != prototypes_.end()) ? it->second.get() : nullptr;
    }

private:
     map<std::string, std::unique_ptr<IAccumulator>> prototypes_;
};

// --- Example Implementations ---

class SumAccumulator : public IAccumulator {
public:
    void accumulate(ConstraintValue const& value) override;
    void reverse(ConstraintValue const& value) override;
    ConstraintValue get_result() const override;

    bool supports_reverse() const override { return true; }

    void clear() override;
    std::unique_ptr<IAccumulator> clone() const override;

private:
    double sum = 0.0;
    bool is_double = false;
};

class CountAccumulator : public IAccumulator {
public:
    void accumulate(ConstraintValue const& value) override { count++; }

    void reverse(ConstraintValue const& value) override { count--; }

    ConstraintValue get_result() const override { return count; }

    void clear() override { count = 0; }

    std::unique_ptr<IAccumulator> clone() const override { return std::make_unique<CountAccumulator>(*this); }

    bool supports_reverse() const override { return true; }

private:
    int64_t count = 0;
};

class AverageAccumulator : public IAccumulator {
public:
    void accumulate(ConstraintValue const& value) override;
    void reverse(ConstraintValue const& value) override;
    ConstraintValue get_result() const override;

    bool supports_reverse() const override { return true; }

    void clear() override;
    std::unique_ptr<IAccumulator> clone() const override;

private:
    double sum = 0.0;
    int64_t count = 0;
};

class MinAccumulator : public IAccumulator {
public:
    void accumulate(ConstraintValue const& value) override;
    void reverse(ConstraintValue const& value) override;
    ConstraintValue get_result() const override;

    bool supports_reverse() const override { return true; }

    void clear() override;
    std::unique_ptr<IAccumulator> clone() const override;

private:
    // We need to keep track of all values to correctly handle reverse
    std::multiset<double> values;
    bool is_double = false;
};

class MaxAccumulator : public IAccumulator {
public:
    void accumulate(ConstraintValue const& value) override;
    void reverse(ConstraintValue const& value) override;
    ConstraintValue get_result() const override;

    bool supports_reverse() const override { return true; }

    void clear() override;
    std::unique_ptr<IAccumulator> clone() const override;

private:
    std::multiset<double> values;
    bool is_double = false;
};

// Implements `collectList` (and is the target for `collect`)
class CollectAccumulator : public IAccumulator {
public:
    void accumulate(ConstraintValue const& value) override;
    void reverse(ConstraintValue const& value) override;
    ConstraintValue get_result() const override;
    void clear() override;
    std::unique_ptr<IAccumulator> clone() const override;
    // supports_reverse is false by default, which is correct for collect.
};

// Implements `collectSet`
class CollectSetAccumulator : public IAccumulator {
public:
    void accumulate(ConstraintValue const& value) override;
    void reverse(ConstraintValue const& value) override;
    ConstraintValue get_result() const override;
    void clear() override;
    std::unique_ptr<IAccumulator> clone() const override;
    // supports_reverse is false by default
};

#endif   // RFL_ACCUMULATORS_HPP


