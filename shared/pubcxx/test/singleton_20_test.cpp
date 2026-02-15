#include "tinytest.h"
#include "pubcxx/singleton.hpp"

#include <atomic>
#include <iostream>

class Foo : public Singleton<Foo> {
public:
    explicit Foo(int n) : n_{n} {}

    void Bar() {}

private:
    int n_;
};

static std::atomic_uint32_t init{0};

class Counter : public Singleton<Counter> {
public:
    Counter() { ++init; }

    ~Counter() { --init; }

    void Add() { ++count_; }

    std::uint32_t GetCount() const { return count_; }

private:
    std::atomic_uint32_t count_{0};
};

suite("Singleton Benchmark") {
    it("benchmarks get instance bar") {
        Foo::Construct(17);
        auto* foo_instance = Foo::GetInstance();
        foo_instance->Bar();

        benchmark("get instance bar", 10000) {
            foo_instance->Bar();
        }

        Foo::Destruct();
    }

    it("benchmarks atomic counter") {
        Counter::Construct();
        auto* counter_instance = Counter::GetInstance();

        counter_instance->Add();
        check_uint_eq(counter_instance->GetCount(), 1);
        check_uint_eq(init, 1);

        benchmark("increment counter", 10000) {
            counter_instance->Add();
        }

        Counter::Destruct();
        check_uint_eq(init, 0);
    }
}
