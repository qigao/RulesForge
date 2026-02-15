#include "tinytest.h"
#include "pubcxx/random.hpp"

#include <string>

suite("Random") {
    it("generates string of len(10)") {
        std::string str = random_string(10);
        check_size_eq(str.size(), 10);
    }

    it("generates string of len(0)") {
        std::string str = random_string(0);
        check(str.empty());
    }
}
