#include "core/fact.hpp"
#include "core/value_types.hpp"
#include "engine/schema_validator.hpp"
#include "tinytest.h"

#include <set>
#include <type_traits>
#include <unordered_set>

static_assert(ConstraintValueMember<turbo_uuid_t>);
static_assert(ConstraintValueExtractable<turbo_uuid_t>);

suite("ConstraintValue") {
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
}
