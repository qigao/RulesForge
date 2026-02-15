#include "tinytest.h"
#include "pubcxx/str_algo.hpp"

#include <string>
#include <vector>

suite("join_range") {
    it("joins two strings with comma") {
        std::vector<std::string> strings = {"hello", "world"};
        std::string result = join_range(strings, ",");
        check_str_eq(result.c_str(), "hello,world");
    }

    it("joins three strings with hyphen") {
        std::vector<std::string> strings = {"a", "b", "c"};
        std::string result = join_range(strings, "-");
        check_str_eq(result.c_str(), "a-b-c");
    }

    it("joins one string with comma") {
        std::vector<std::string> strings = {"one"};
        std::string result = join_range(strings, ",");
        check_str_eq(result.c_str(), "one");
    }

    it("joins empty vector with comma") {
        std::vector<std::string> strings = {};
        std::string result = join_range(strings, ",");
        check_str_eq(result.c_str(), "");
    }

    it("joins three strings with space") {
        std::vector<std::string> strings = {"first", "second", "third"};
        std::string result = join_range(strings, " ");
        check_str_eq(result.c_str(), "first second third");
    }

    it("joins strings with empty delimiter") {
        std::vector<std::string> strings = {"a", "b", "c"};
        std::string result = join_range(strings, "");
        check_str_eq(result.c_str(), "abc");
    }

    it("joins strings with multi-character delimiter") {
        std::vector<std::string> strings = {"one", "two", "three"};
        std::string result = join_range(strings, " and ");
        check_str_eq(result.c_str(), "one and two and three");
    }

    it("joins strings with special characters in delimiter") {
        std::vector<std::string> strings = {"apple", "banana", "cherry"};
        std::string result = join_range(strings, " | ");
        check_str_eq(result.c_str(), "apple | banana | cherry");
    }
}
