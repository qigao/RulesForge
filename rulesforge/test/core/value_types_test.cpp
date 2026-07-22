#include "core/fact.hpp"
#include "core/value_types.hpp"
#include "engine/schema_validator.hpp"
#include "tinytest.h"

#include <set>
#include <type_traits>
#include <unordered_set>

static_assert(ConstraintValueMember<turbo_uuid_t>);
static_assert(ConstraintValueExtractable<turbo_uuid_t>);
static_assert(ConstraintValueMember<bool>);
static_assert(ConstraintValueExtractable<bool>);
static_assert(ConstraintValueMember<uint64_t>);
static_assert(ConstraintValueMember<BytesValue>);
static_assert(ConstraintValueMember<EnumValue>);
static_assert(ConstraintValueMember<DateTimeValue>);
static_assert(ConstraintValueMember<DateValue>);
static_assert(ConstraintValueMember<TimeValue>);
static_assert(ConstraintValueMember<DurationValue>);
static_assert(ConstraintValueMember<DecimalValue>);
static_assert(ConstraintValueMember<BigIntValue>);
static_assert(ConstraintValueMember<MoneyValue>);

suite("ConstraintValue") {
    group("bool") {
        it("stores booleans without collapsing them into integers") {
            ConstraintValue enabled{true};
            ConstraintValue disabled{false};
            ConstraintValue integer{int64_t{1}};

            check(std::holds_alternative<bool>(enabled));
            check(std::get<bool>(enabled));
            check(!std::get<bool>(disabled));
            check(!ConstraintValueEquals{}(enabled, integer));
            std::string enabled_text = to_string(enabled);
            std::string disabled_text = to_string(disabled);
            check_str_eq(enabled_text.c_str(), "true");
            check_str_eq(disabled_text.c_str(), "false");
        }

        it("compares and hashes boolean values by their boolean type") {
            ConstraintValue disabled{false};
            ConstraintValue enabled{true};
            std::set<ConstraintValue, ConstraintValueCompare> ordered;
            ordered.insert(enabled);
            ordered.insert(disabled);
            ordered.insert(enabled);

            check_size_eq(ordered.size(), 2);
            check(ConstraintValueEquals{}(*ordered.begin(), disabled));

            std::unordered_set<ConstraintValue, ConstraintValueHasher, ConstraintValueEquals> hashed;
            hashed.insert(disabled);
            hashed.insert(enabled);
            hashed.insert(enabled);
            check_size_eq(hashed.size(), 2);
        }

        it("validates only boolean values against boolean schema fields") {
            ParsedDeclaration declaration;
            declaration.type_name = "Switch";
            declaration.fields.push_back(ParsedField{"enabled", FT_Boolean, {}});
            std::vector<ParsedDeclaration> declarations{declaration};
            SchemaValidator validator(declarations);

            Fact valid;
            valid.type = "Switch";
            valid.fields["enabled"] = true;
            check(validator.validate(valid).empty());

            Fact integer;
            integer.type = "Switch";
            integer.fields["enabled"] = int64_t{1};
            auto errors = validator.validate(integer);
            check_size_eq(errors.size(), 1);
        }
    }

    group("turbo_uuid_t") {
        it("stores and copies UUID values without losing their type") {
            turbo_uuid_t uuid{};
            check_int_eq(
                turbo_uuid_parse("01890f3e-5c5a-7cc2-9f2b-8b7f47f0c001", &uuid),
                TURBO_OK);

            ConstraintValue value{uuid};
            ConstraintValue copy = value;
            auto const* stored = std::get_if<turbo_uuid_t>(&copy);

            check_not_null(stored);
            check(turbo_uuid_equal(stored, &uuid));
            check(value == copy);
        }

        it("compares and hashes UUID values by their 16 bytes") {
            turbo_uuid_t lower{};
            turbo_uuid_t upper{};
            check_int_eq(
                turbo_uuid_parse("01890f3e-5c5a-7cc2-9f2b-8b7f47f0c001", &lower),
                TURBO_OK);
            check_int_eq(
                turbo_uuid_parse("11890f3e-5c5a-7cc2-9f2b-8b7f47f0c001", &upper),
                TURBO_OK);

            ConstraintValue lower_value{lower};
            ConstraintValue upper_value{upper};
            std::set<ConstraintValue, ConstraintValueCompare> ordered;
            ordered.insert(upper_value);
            ordered.insert(lower_value);
            ordered.insert(lower_value);

            check_size_eq(ordered.size(), 2);
            check(ConstraintValueEquals{}(*ordered.begin(), lower_value));

            std::unordered_set<ConstraintValue, ConstraintValueHasher, ConstraintValueEquals> hashed;
            hashed.insert(lower_value);
            hashed.insert(upper_value);
            hashed.insert(lower_value);
            check_size_eq(hashed.size(), 2);
        }

        it("formats UUID values as canonical text") {
            turbo_uuid_t uuid{};
            char const* canonical = "01890f3e-5c5a-7cc2-9f2b-8b7f47f0c001";
            check_int_eq(turbo_uuid_parse(canonical, &uuid), TURBO_OK);

            std::string text = to_string(ConstraintValue{uuid});
            check_str_eq(text.c_str(), canonical);
        }

        it("validates UUID values against UUID schema fields") {
            ParsedDeclaration declaration;
            declaration.type_name = "Message";
            declaration.fields.push_back(ParsedField{"id", FT_Uuid, {}});
            std::vector<ParsedDeclaration> declarations{declaration};
            SchemaValidator validator(declarations);

            turbo_uuid_t uuid{};
            check_int_eq(
                turbo_uuid_parse("01890f3e-5c5a-7cc2-9f2b-8b7f47f0c001", &uuid),
                TURBO_OK);
            Fact fact;
            fact.type = "Message";
            fact.fields["id"] = uuid;

            check(validator.validate(fact).empty());
        }

        it("rejects string values for UUID schema fields") {
            ParsedDeclaration declaration;
            declaration.type_name = "Message";
            declaration.fields.push_back(ParsedField{"id", FT_Uuid, {}});
            std::vector<ParsedDeclaration> declarations{declaration};
            SchemaValidator validator(declarations);

            Fact fact;
            fact.type = "Message";
            fact.fields["id"] = std::string("01890f3e-5c5a-7cc2-9f2b-8b7f47f0c001");

            auto errors = validator.validate(fact);
            check_size_eq(errors.size(), 1);
        }
    }

    group("DataBind scalar identities") {
        it("keeps each extended scalar as a distinct runtime type") {
            std::vector<ConstraintValue> values{
                uint64_t{UINT64_MAX},
                BytesValue{{0x41, 0x7a}},
                EnumValue{"Side", int64_t{2}, "Sell"},
                DateTimeValue{2026, 6, 28, 9, 30, 5, 123, 0, true},
                DateValue{2026, 6, 28},
                TimeValue{9, 30, 5, 123},
                DurationValue{5405250},
                DecimalValue{12345, 2},
                BigIntValue{"123456789012345678901234567890"},
                MoneyValue{DecimalValue{12345, 2}, "USD"},
            };

            std::unordered_set<ConstraintValue, ConstraintValueHasher, ConstraintValueEquals> unique;
            for (auto const& value : values) unique.insert(value);
            check_size_eq(unique.size(), values.size());
            auto const enum_text = to_string(values[2]);
            auto const datetime_text = to_string(values[3]);
            auto const date_text = to_string(values[4]);
            auto const time_text = to_string(values[5]);
            auto const decimal_text = to_string(values[7]);
            auto const money_text = to_string(values[9]);
            check_str_eq(enum_text.c_str(), "Sell");
            check_str_eq(datetime_text.c_str(), "2026-06-28T09:30:05.123Z");
            check_str_eq(date_text.c_str(), "2026-06-28");
            check_str_eq(time_text.c_str(), "09:30:05.123");
            check_str_eq(decimal_text.c_str(), "123.45");
            check_str_eq(money_text.c_str(), "USD 123.45");
        }

        it("validates extended scalars against exact schema field types") {
            ParsedDeclaration declaration;
            declaration.type_name = "ScalarFact";
            declaration.fields = {
                ParsedField{"counter", FT_UInt64, {}},
                ParsedField{"raw", FT_Bytes, {}},
                ParsedField{"side", FT_Enum, {}},
                ParsedField{"observed", FT_DateTime, {}},
                ParsedField{"day", FT_Date, {}},
                ParsedField{"clock", FT_Time, {}},
                ParsedField{"latency", FT_Duration, {}},
                ParsedField{"price", FT_Decimal, {}},
                ParsedField{"sequence", FT_BigInt, {}},
                ParsedField{"total", FT_Money, {}},
            };
            std::vector<ParsedDeclaration> declarations{declaration};
            SchemaValidator validator(declarations);

            Fact fact;
            fact.type = "ScalarFact";
            fact.fields["counter"] = uint64_t{UINT64_MAX};
            fact.fields["raw"] = BytesValue{{0x41, 0x7a}};
            fact.fields["side"] = EnumValue{"Side", int64_t{2}, "Sell"};
            fact.fields["observed"] = DateTimeValue{2026, 6, 28, 9, 30, 5, 123, 0, true};
            fact.fields["day"] = DateValue{2026, 6, 28};
            fact.fields["clock"] = TimeValue{9, 30, 5, 123};
            fact.fields["latency"] = DurationValue{5405250};
            fact.fields["price"] = DecimalValue{12345, 2};
            fact.fields["sequence"] = BigIntValue{"123456789012345678901234567890"};
            fact.fields["total"] = MoneyValue{DecimalValue{12345, 2}, "USD"};
            check(validator.validate(fact).empty());

            fact.fields["counter"] = int64_t{1};
            check_size_eq(validator.validate(fact).size(), 1);
        }
    }
}
