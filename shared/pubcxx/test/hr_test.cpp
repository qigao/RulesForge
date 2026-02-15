#include "tinytest.h"
#include "pubcxx/readable.hpp"

#include <sstream>

constexpr size_t IntegerRepresentableBoundary() { return size_t{2} << (std::numeric_limits<double>::digits - 1); }

suite("HumanReadable") {
    it("converts 1K correctly") {
        HumanReadable hr;
        hr.size = 1024;

        std::ostringstream oss;
        oss << hr;

        check_str_eq(oss.str().c_str(), "1KB (1024)");
    }

    group("NarrowCast") {
        it("casts values at boundary correctly") {
            check_size_eq(static_cast<size_t>(NarrowCast<double>(size_t{IntegerRepresentableBoundary() - 2})),
                          size_t{IntegerRepresentableBoundary() - 2});
            check_size_eq(static_cast<size_t>(NarrowCast<double>(size_t{IntegerRepresentableBoundary() - 1})),
                          size_t{IntegerRepresentableBoundary() - 1});
            check_size_eq(static_cast<size_t>(NarrowCast<double>(size_t{IntegerRepresentableBoundary()})),
                          size_t{IntegerRepresentableBoundary()});
            check_size_eq(static_cast<size_t>(NarrowCast<double>(size_t{IntegerRepresentableBoundary() + 2})),
                          size_t{IntegerRepresentableBoundary() + 2});
            check_size_eq(static_cast<size_t>(NarrowCast<double>(size_t{IntegerRepresentableBoundary() + 4})),
                          size_t{IntegerRepresentableBoundary() + 4});
        }

        it("throws on non-representable values") {
            REQUIRE_THROWS(NarrowCast<double>(size_t{IntegerRepresentableBoundary() + 1}));
            REQUIRE_THROWS(NarrowCast<double>(size_t{IntegerRepresentableBoundary() + 3}));
            REQUIRE_THROWS(NarrowCast<double>(size_t{IntegerRepresentableBoundary() + 5}));
        }
    }
}
