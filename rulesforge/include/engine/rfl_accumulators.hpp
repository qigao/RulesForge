#ifndef RFL_ACCUMULATORS_HPP
#define RFL_ACCUMULATORS_HPP

#include "core/value_types.hpp"

#include <map>
#include <memory>
#include <set>


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

/**
 * @brief CRTP Mixin ：自動为派生类实现 clone()。
 *
 * 使用方式：派生类继承 AccumulatorBase<自身>而非直接继承 IAccumulator。
 * CRTP 展开后等价于手写 `return std::make_unique<Derived>(*this);`，
 * 但消除了每个子类的样板重复。
 *
 * @tparam Derived 具体的 Accumulator 子类（必须是可拷贝构造的）
 *
 * 注意：static_assert 放在 clone() 内部而非类定义处，因为 CRTP 声明时
 * Derived 是不完全类型，只有方法实例化时才能检测 CopyConstructible。
 */
template <typename Derived>
struct AccumulatorBase : IAccumulator {
    std::unique_ptr<IAccumulator> clone() const final {
        // static_assert 必须在此处（方法体内），Derived 在实例化 clone() 时已完整定义
        static_assert(std::is_copy_constructible_v<Derived>,
            "AccumulatorBase<Derived>: Derived must be copy constructible to support clone()");
        return std::make_unique<Derived>(static_cast<Derived const&>(*this));
    }
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
     std::map<std::string, std::unique_ptr<IAccumulator>> prototypes_;
};

// --- Example Implementations ---

class SumAccumulator : public AccumulatorBase<SumAccumulator> {
public:
    void accumulate(ConstraintValue const& value) override;
    void reverse(ConstraintValue const& value) override;
    ConstraintValue get_result() const override;

    bool supports_reverse() const override { return true; }

    void clear() override;

private:
    double sum = 0.0;
    bool is_double = false;
};

class CountAccumulator : public AccumulatorBase<CountAccumulator> {
public:
    void accumulate(ConstraintValue const& value) override { count++; }

    void reverse(ConstraintValue const& value) override { count--; }

    ConstraintValue get_result() const override { return count; }

    void clear() override { count = 0; }

    bool supports_reverse() const override { return true; }

private:
    int64_t count = 0;
};

class AverageAccumulator : public AccumulatorBase<AverageAccumulator> {
public:
    void accumulate(ConstraintValue const& value) override;
    void reverse(ConstraintValue const& value) override;
    ConstraintValue get_result() const override;

    bool supports_reverse() const override { return true; }

    void clear() override;

private:
    double sum = 0.0;
    int64_t count = 0;
};

class MinAccumulator : public AccumulatorBase<MinAccumulator> {
public:
    void accumulate(ConstraintValue const& value) override;
    void reverse(ConstraintValue const& value) override;
    ConstraintValue get_result() const override;

    bool supports_reverse() const override { return true; }

    void clear() override;

private:
    // We need to keep track of all values to correctly handle reverse
    std::multiset<double> values;
    bool is_double = false;
};

class MaxAccumulator : public AccumulatorBase<MaxAccumulator> {
public:
    void accumulate(ConstraintValue const& value) override;
    void reverse(ConstraintValue const& value) override;
    ConstraintValue get_result() const override;

    bool supports_reverse() const override { return true; }

    void clear() override;

private:
    std::multiset<double> values;
    bool is_double = false;
};

// Implements `collectList` (and is the target for `collect`)
class CollectAccumulator : public AccumulatorBase<CollectAccumulator> {
public:
    void accumulate(ConstraintValue const& value) override;
    void reverse(ConstraintValue const& value) override;
    ConstraintValue get_result() const override;
    void clear() override;
    bool supports_reverse() const override { return true; }
};

// Implements `collectSet`
class CollectSetAccumulator : public AccumulatorBase<CollectSetAccumulator> {
public:
    void accumulate(ConstraintValue const& value) override;
    void reverse(ConstraintValue const& value) override;
    ConstraintValue get_result() const override;
    void clear() override;
    bool supports_reverse() const override { return true; }
};

#endif   // RFL_ACCUMULATORS_HPP

