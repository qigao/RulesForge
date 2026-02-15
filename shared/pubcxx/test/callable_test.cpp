#include "tinytest.h"
#include "pubcxx/callable.hpp"

#include <concepts>
#include <functional>
#include <string>

namespace Test {
    template <typename Fn, typename... Args>
    decltype(auto) TestCallable(Fn&& fun, Args&&... args) {
        return std::invoke(std::forward<Fn>(fun), std::forward<Args>(args)...);
    }

    template <typename Fn, typename... Args>
        requires std::invocable<Fn, Args...>
    decltype(auto) TestCallableWithConstraint(Fn&& fun, Args&&... args) {
        return std::invoke(std::forward<Fn>(fun), std::forward<Args>(args)...);
    }
}

struct Functor {
    int operator()(int x, int y) const { return x + y; }
};

struct TestClass {
    int value = 10;

    int add(int x) const { return value + x; }

    std::string& append(std::string& s) {
        s += " appended";
        return s;
    }
};

int freeFunction(int x, int y) { return x * y; }

suite("TestCallable") {
    group("function template tests") {
        it("calls free function") {
            check_int_eq(Test::TestCallable(freeFunction, 3, 4), 12);
            check_int_eq(Test::TestCallableWithConstraint(freeFunction, 3, 4), 12);
        }

        it("calls lambda without capture") {
            auto lambda = [](int x, int y) { return x + y; };
            check_int_eq(Test::TestCallable(lambda, 5, 6), 11);
            check_int_eq(Test::TestCallableWithConstraint(lambda, 5, 6), 11);
        }

        it("calls lambda with capture") {
            int offset = 10;
            auto lambda = [offset](int x) { return x + offset; };
            check_int_eq(Test::TestCallable(lambda, 5), 15);
            check_int_eq(Test::TestCallableWithConstraint(lambda, 5), 15);
        }

        it("calls functor") {
            Functor functor;
            check_int_eq(Test::TestCallable(functor, 7, 8), 15);
            check_int_eq(Test::TestCallableWithConstraint(functor, 7, 8), 15);
        }

        it("calls const member function") {
            TestClass obj;
            check_int_eq(Test::TestCallable(&TestClass::add, obj, 5), 15);
            check_int_eq(Test::TestCallableWithConstraint(&TestClass::add, obj, 5), 15);
        }

        it("calls member function with reference return") {
            TestClass obj;
            std::string str = "test";
            check_ptr_eq(&Test::TestCallable(&TestClass::append, obj, str), &str);
            check_str_eq(str.c_str(), "test appended");
            str = "test";
            check_ptr_eq(&Test::TestCallableWithConstraint(&TestClass::append, obj, str), &str);
            check_str_eq(str.c_str(), "test appended");
        }

        it("handles reference return from lambda") {
            int x = 42;
            auto lambda = [&x]() -> int& { return x; };
            Test::TestCallable(lambda) = 100;
            check_int_eq(x, 100);
            x = 42;
            Test::TestCallableWithConstraint(lambda) = 200;
            check_int_eq(x, 200);
        }

        it("handles no arguments") {
            auto lambda = []() { return 42; };
            check_int_eq(Test::TestCallable(lambda), 42);
            check_int_eq(Test::TestCallableWithConstraint(lambda), 42);
        }
    }

    group("concepts") {
        it("ensures invocable constraints") {
            static_assert(std::invocable<decltype(freeFunction), int, int>, "Free function should be invocable");
            static_assert(std::invocable<Functor, int, int>, "Functor should be invocable");
            static_assert(std::invocable<decltype(&TestClass::add), TestClass, int>, "Member function should be invocable");
            static_assert(!std::invocable<decltype(freeFunction), int>, "Free function with wrong args should not be invocable");
            static_assert(!std::invocable<int, int>, "Non-callable type should not be invocable");
            check(true);
        }
    }
}
