#include "execution/scalar_function.hpp"
#include "text/owned_text.hpp"
#include <cerrno>
#include <cstdlib>
#include <locale.h>
#ifdef __APPLE__
#include <xlocale.h>
#endif

namespace rulesforge {
namespace {
// The C numeric locale is immutable and shared after C++ static initialization.
// Parsing must never change the host application's process-wide locale.
struct NumericLocale {
#ifdef _WIN32
    _locale_t value = _create_locale(LC_NUMERIC, "C");
    ~NumericLocale() { _free_locale(value); }
#else
    locale_t value = newlocale(LC_NUMERIC_MASK, "C", nullptr);
    ~NumericLocale() { freelocale(value); }
#endif
    NumericLocale() {
        if (!value) throw std::runtime_error("Cannot create the scalar numeric locale");
    }
    NumericLocale(NumericLocale const &) = delete;
    NumericLocale &operator=(NumericLocale const &) = delete;
};
} // namespace

std::from_chars_result parse_scalar_double(std::string_view text, double &value) {
    if (text.empty() || text.front() <= ' ' || text.front() == '+')
        return {text.data(), std::errc::invalid_argument};
    auto unsigned_text = text.front() == '-' ? text.substr(1) : text;
    if (unsigned_text.starts_with("0x") || unsigned_text.starts_with("0X"))
        return {text.data(), std::errc::invalid_argument};
    static NumericLocale const locale;
    OwnedText terminated(text);
    char *end = nullptr;
    errno = 0;
#ifdef _WIN32
    double parsed = _strtod_l(terminated.view().data(), &end, locale.value);
#else
    double parsed = strtod_l(terminated.view().data(), &end, locale.value);
#endif
    int status = errno;
    auto consumed = static_cast<size_t>(end - terminated.view().data());
    if (!consumed) return {text.data(), std::errc::invalid_argument};
    // A representable subnormal may set ERANGE. Only zero underflow or
    // non-finite overflow is outside double range, matching from_chars.
    if (status == ERANGE && (std::fpclassify(parsed) == FP_ZERO || !std::isfinite(parsed)))
        return {text.data() + consumed, std::errc::result_out_of_range};
    value = parsed;
    return {text.data() + consumed, {}};
}
} // namespace rulesforge
