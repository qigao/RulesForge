#include "tinytest.h"
#include "pubcxx/singleton.hpp"

#include <atomic>
#include <thread>
#include <vector>

static std::atomic_uint32_t init{0};

class Counter : public Singleton<Counter> {
public:
    Counter() { ++init; }

    void Add() { ++count_; }

    std::uint32_t GetCount() const { return count_; }

private:
    std::atomic_uint32_t count_{0};
};

suite("Singleton MultiThread") {
    it("handles concurrent access correctly") {
        auto const count = std::thread::hardware_concurrency();
        Counter::Construct();
        Counter::GetInstance();
        auto threads = std::vector<std::thread>{};

        threads.reserve(count);
        for (auto i = 0u; i < count; ++i) {
            threads.emplace_back([&] { Counter::GetInstance()->Add(); });
        }

        for (auto& thread : threads) { thread.join(); }

        check_uint_eq(init, 1);
        check_uint_eq(Counter::GetInstance()->GetCount(), count);
    }
}
