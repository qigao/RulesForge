#include "model/value_types.hpp"
#include "model/fact.hpp"
#include <cstl.h>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace {

template <typename T>
void hash_combine(std::size_t& seed, T const& value) {
    seed ^= std::hash<T>{}(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

std::string format_decimal(DecimalValue const& value) {
    bool const negative = value.mantissa < 0;
    uint64_t magnitude = negative
        ? static_cast<uint64_t>(-(value.mantissa + 1)) + 1
        : static_cast<uint64_t>(value.mantissa);
    std::string digits = std::to_string(magnitude);
    if (value.scale <= 0) {
        digits.append(static_cast<std::size_t>(-value.scale), '0');
    } else {
        auto const scale = static_cast<std::size_t>(value.scale);
        if (digits.size() <= scale) {
            digits.insert(0, scale + 1 - digits.size(), '0');
        }
        digits.insert(digits.size() - scale, 1, '.');
        while (digits.back() == '0') digits.pop_back();
        if (digits.back() == '.') digits.pop_back();
    }
    if (negative && digits != "0") digits.insert(digits.begin(), '-');
    return digits;
}

std::string format_date(DateValue const& value) {
    std::ostringstream out;
    out << std::setfill('0') << std::setw(4) << value.year << '-'
        << std::setw(2) << value.month << '-' << std::setw(2) << value.day;
    return out.str();
}

std::string format_time(TimeValue const& value) {
    std::ostringstream out;
    out << std::setfill('0') << std::setw(2) << value.hour << ':'
        << std::setw(2) << value.minute << ':' << std::setw(2) << value.second;
    if (value.millisecond != 0) out << '.' << std::setw(3) << value.millisecond;
    return out.str();
}

std::string format_datetime(DateTimeValue const& value) {
    std::string result = format_date(DateValue{value.year, value.month, value.day});
    result += 'T';
    result += format_time(TimeValue{value.hour, value.minute, value.second, value.millisecond});
    if (value.has_timezone) {
        if (value.tz_offset_minutes == 0) {
            result += 'Z';
        } else {
            int const offset = std::abs(value.tz_offset_minutes);
            std::ostringstream zone;
            zone << (value.tz_offset_minutes < 0 ? '-' : '+') << std::setfill('0')
                 << std::setw(2) << offset / 60 << ':' << std::setw(2) << offset % 60;
            result += zone.str();
        }
    }
    return result;
}

} // namespace

// --- ConstraintValueCompare implementation ---
bool ConstraintValueCompare::operator()(ConstraintValue const& a, ConstraintValue const& b) const {
    // First compare by variant index
    if (a.index() != b.index()) {
        return a.index() < b.index();
    }
    // Same type, compare values
    return std::visit([&b, this](auto const& val_a) -> bool {
        using T = std::decay_t<decltype(val_a)>;
        auto const& val_b = std::get<T>(b);
        if constexpr (std::is_same_v<T, std::string>) {
            return val_a < val_b;
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return val_a < val_b;
        } else if constexpr (std::is_same_v<T, double>) {
            return val_a < val_b;
        } else if constexpr (std::is_same_v<T, bool>) {
            return val_a < val_b;
        } else if constexpr (std::is_same_v<T, uint64_t>
                             || std::is_same_v<T, BytesValue>
                             || std::is_same_v<T, EnumValue>
                             || std::is_same_v<T, DateTimeValue>
                             || std::is_same_v<T, DateValue>
                             || std::is_same_v<T, TimeValue>
                             || std::is_same_v<T, DurationValue>
                             || std::is_same_v<T, DecimalValue>
                             || std::is_same_v<T, BigIntValue>
                             || std::is_same_v<T, MoneyValue>) {
            return val_a < val_b;
        } else if constexpr (std::is_same_v<T, UuidValue>) {
            return std::lexicographical_compare(
                val_a.begin(), val_a.end(),
                val_b.begin(), val_b.end());
        } else if constexpr (std::is_same_v<T, FactList>) {
            auto fact_key = [](Fact const* fact) {
                if (!fact) return std::pair<int64_t, uintptr_t>{0, 0};
                if (fact->id != 0) {
                    return std::pair<int64_t, uintptr_t>{fact->id, 0};
                }
                return std::pair<int64_t, uintptr_t>{0, reinterpret_cast<uintptr_t>(fact)};
            };
            return std::lexicographical_compare(
                val_a.facts.begin(), val_a.facts.end(),
                val_b.facts.begin(), val_b.facts.end(),
                [&fact_key](Fact const* lhs, Fact const* rhs) {
                    return fact_key(lhs) < fact_key(rhs);
                });
        } else if constexpr (std::is_same_v<T, NilValue>) {
            return false;  // All nils are equal
        } else if constexpr (std::is_same_v<T, std::shared_ptr<TypedList>>) {
            if (!val_a && !val_b) return false;
            if (!val_a) return true;
            if (!val_b) return false;
            return std::lexicographical_compare(
                val_a->values.begin(), val_a->values.end(),
                val_b->values.begin(), val_b->values.end(),
                *this);
        } else if constexpr (std::is_same_v<T, std::shared_ptr<ValueSet>>) {
            if (!val_a && !val_b) return false;
            if (!val_a) return true;
            if (!val_b) return false;
            return std::lexicographical_compare(
                val_a->values.begin(), val_a->values.end(),
                val_b->values.begin(), val_b->values.end(),
                *this);
        } else if constexpr (std::is_same_v<T, std::shared_ptr<ValueMap>>) {
            if (!val_a && !val_b) return false;
            if (!val_a) return true;
            if (!val_b) return false;
            // Compare maps by iterating through entries
            auto it_a = val_a->entries.begin();
            auto it_b = val_b->entries.begin();
            while (it_a != val_a->entries.end() && it_b != val_b->entries.end()) {
                if ((*this)(it_a->first, it_b->first)) return true;
                if ((*this)(it_b->first, it_a->first)) return false;
                if ((*this)(it_a->second, it_b->second)) return true;
                if ((*this)(it_b->second, it_a->second)) return false;
                ++it_a;
                ++it_b;
            }
            return it_a == val_a->entries.end() && it_b != val_b->entries.end();
        } else {
            return false;
        }
    }, a);
}

bool ConstraintValueEquals::operator()(ConstraintValue const& a, ConstraintValue const& b) const {
    ConstraintValueCompare compare;
    return !compare(a, b) && !compare(b, a);
}

bool operator==(FactList const& a, FactList const& b) {
    if (a.facts.size() != b.facts.size()) {
        return false;
    }

    auto same_fact = [](Fact const* lhs, Fact const* rhs) {
        if (lhs == rhs) {
            return true;
        }
        if (!lhs || !rhs) {
            return false;
        }
        if (lhs->id != 0 && rhs->id != 0) {
            return lhs->id == rhs->id;
        }
        return lhs == rhs;
    };

    for (size_t i = 0; i < a.facts.size(); ++i) {
        if (!same_fact(a.facts[i], b.facts[i])) {
            return false;
        }
    }
    return true;
}

bool operator!=(FactList const& a, FactList const& b) {
    return !(a == b);
}

// --- Implementation for to_string free function ---
std::string to_string(ConstraintValue const& val) {
    return std::visit(
        [](auto&& arg) -> std::string {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::string>) {
                return "\"" + arg + "\"";
            } else if constexpr (std::is_same_v<T, int64_t>) {
                return std::to_string(arg);
            } else if constexpr (std::is_same_v<T, double>) {
                return std::to_string(arg);
            } else if constexpr (std::is_same_v<T, bool>) {
                return arg ? "true" : "false";
            } else if constexpr (std::is_same_v<T, uint64_t>) {
                return std::to_string(arg);
            } else if constexpr (std::is_same_v<T, BytesValue>) {
                std::ostringstream out;
                out << "0x" << std::hex << std::setfill('0');
                for (uint8_t byte : arg.bytes) out << std::setw(2) << static_cast<unsigned>(byte);
                return out.str();
            } else if constexpr (std::is_same_v<T, EnumValue>) {
                return arg.item_name.empty() ? std::visit([](auto numeric) {
                    return std::to_string(numeric);
                }, arg.value) : arg.item_name;
            } else if constexpr (std::is_same_v<T, DateTimeValue>) {
                return format_datetime(arg);
            } else if constexpr (std::is_same_v<T, DateValue>) {
                return format_date(arg);
            } else if constexpr (std::is_same_v<T, TimeValue>) {
                return format_time(arg);
            } else if constexpr (std::is_same_v<T, DurationValue>) {
                return std::to_string(arg.milliseconds);
            } else if constexpr (std::is_same_v<T, DecimalValue>) {
                return format_decimal(arg);
            } else if constexpr (std::is_same_v<T, BigIntValue>) {
                return arg.digits;
            } else if constexpr (std::is_same_v<T, MoneyValue>) {
                return arg.currency + " " + format_decimal(arg.amount);
            } else if constexpr (std::is_same_v<T, UuidValue>) {
                char text[SALTS_UUID_STRING_SIZE];
                if (uuid_format(arg.data(), text, sizeof(text)) != SALTS_OK) {
                    throw std::runtime_error("Failed to format UuidValue");
                }
                return text;
            } else if constexpr (std::is_same_v<T, FactList>) {
                return "[FactList]";
            } else if constexpr (std::is_same_v<T, NilValue>) {
                return "nil";
            } else if constexpr (std::is_same_v<T, std::shared_ptr<TypedList>>) {
                if (!arg) return "[]";
                std::string result = "[";
                for (size_t i = 0; i < arg->values.size(); ++i) {
                    if (i > 0) result += ", ";
                    result += to_string(arg->values[i]);
                }
                return result + "]";
            } else if constexpr (std::is_same_v<T, std::shared_ptr<ValueSet>>) {
                if (!arg) return "{}";
                std::string result = "{";
                bool first = true;
                for (auto const& v : arg->values) {
                    if (!first) result += ", ";
                    result += to_string(v);
                    first = false;
                }
                return result + "}";
            } else if constexpr (std::is_same_v<T, std::shared_ptr<ValueMap>>) {
                if (!arg) return "{}";
                std::string result = "{";
                bool first = true;
                for (auto const& [k, v] : arg->entries) {
                    if (!first) result += ", ";
                    result += to_string(k) + ": " + to_string(v);
                    first = false;
                }
                return result + "}";
            } else {
                return "UNKNOWN";
            }
        },
        val);
}

std::optional<std::string> scalar_text(ConstraintValue const& val) {
    return std::visit([](auto const& arg) -> std::optional<std::string> {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::string>) return arg;
        else if constexpr (std::is_same_v<T, DateTimeValue>) return format_datetime(arg);
        else if constexpr (std::is_same_v<T, DateValue>) return format_date(arg);
        else if constexpr (std::is_same_v<T, TimeValue>) return format_time(arg);
        else if constexpr (std::is_same_v<T, DecimalValue>) return format_decimal(arg);
        else if constexpr (std::is_same_v<T, BigIntValue>) return arg.digits;
        else if constexpr (std::is_same_v<T, MoneyValue>) {
            return arg.currency + " " + format_decimal(arg.amount);
        } else if constexpr (std::is_same_v<T, EnumValue>) {
            return arg.item_name.empty() ? std::nullopt : std::optional<std::string>{arg.item_name};
        } else {
            return std::nullopt;
        }
    }, val);
}

// --- Implementation for Hasher ---
std::size_t ConstraintValueHasher::operator()(ConstraintValue const& v) const {
    return std::visit(
        [](auto const& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, FactList>) {
                size_t h = 0;
                for (auto const* fact : arg.facts) {
                    size_t fact_hash = 0;
                    if (fact) {
                        fact_hash = fact->id != 0 ? std::hash<int64_t>{}(fact->id)
                                                  : std::hash<uintptr_t>{}(reinterpret_cast<uintptr_t>(fact));
                    }
                    h ^= fact_hash + 0x9e3779b9 + (h << 6) + (h >> 2);
                }
                return h;
            } else if constexpr (std::is_same_v<T, UuidValue>) {
                return hash_bytes(arg.data(), SALTS_UUID_SIZE, nullptr);
            } else if constexpr (std::is_same_v<T, BytesValue>) {
                return hash_bytes(arg.bytes.data(), arg.bytes.size(), nullptr);
            } else if constexpr (std::is_same_v<T, EnumValue>) {
                std::size_t h = std::hash<std::string>{}(arg.type_name);
                std::visit([&h](auto numeric) { hash_combine(h, numeric); }, arg.value);
                return h;
            } else if constexpr (std::is_same_v<T, DateTimeValue>
                                 || std::is_same_v<T, DateValue>
                                 || std::is_same_v<T, TimeValue>
                                 || std::is_same_v<T, DecimalValue>
                                 || std::is_same_v<T, BigIntValue>
                                 || std::is_same_v<T, MoneyValue>) {
                return std::hash<std::string>{}(*scalar_text(ConstraintValue{arg}));
            } else if constexpr (std::is_same_v<T, DurationValue>) {
                return std::hash<int64_t>{}(arg.milliseconds);
            } else if constexpr (std::is_same_v<T, NilValue>) {
                return (size_t)0;
            } else if constexpr (std::is_same_v<T, std::shared_ptr<TypedList>>) {
                if (!arg) return (size_t)0;
                size_t h = 0;
                for (auto const& val : arg->values) {
                    h ^= ConstraintValueHasher{}(val) + 0x9e3779b9 + (h << 6) + (h >> 2);
                }
                return h;
            } else if constexpr (std::is_same_v<T, std::shared_ptr<ValueSet>>) {
                if (!arg) return (size_t)0;
                size_t h = 0;
                for (auto const& val : arg->values) {
                    h ^= ConstraintValueHasher{}(val);
                }
                return h;
            } else if constexpr (std::is_same_v<T, std::shared_ptr<ValueMap>>) {
                if (!arg) return (size_t)0;
                size_t h = 0;
                for (auto const& [k, val] : arg->entries) {
                    h ^= ConstraintValueHasher{}(k) ^ ConstraintValueHasher{}(val);
                }
                return h;
            } else {
                return std::hash<T>{}(arg);
            }
        },
        v);
}
