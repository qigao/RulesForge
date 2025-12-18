#include "catch2/catch_all.hpp"
#include "rfl_parser.hpp"
#include "knowledge_base.hpp"
#include "query_result.hpp"
#include "stateful_session.hpp"

// Helper to get a typed value from a fact, failing the test if the field is missing or the type is wrong.
template <typename T>
T get_field(Fact const& f, std::string const& field) {
    auto val_opt = f.get_field(field);
    REQUIRE(val_opt.has_value());
    if (auto val_ptr = std::get_if<T>(&*val_opt)) { return *val_ptr; }
    // Allow implicit conversion from int64_t for double checks
    if constexpr (std::is_same_v<T, double>) {
        if (auto int_ptr = std::get_if<int64_t>(&*val_opt)) { return static_cast<double>(*int_ptr); }
    }
    FAIL("Field '" + field + "' has an unexpected type.");
    return T{};
}

/**
 * @brief A generic test helper to execute a query that is expected to return
 *        a single row with a single result value.
 *
 * This function encapsulates the logic of executing a query, validating that
 * exactly one result is found, and extracting a specific field value of a
 * known type. It will fail the test (via REQUIRE or an exception) if
 * these assumptions are not met.
 *
 * @tparam T The expected type of the result field (e.g., double, int64_t).
 * @param session A reference to the active StatefulSession.
 * @param query_name The name of the query to execute.
 * @param binding_name The name of the binding in the query (e.g., "$r" for "$r").
 * @param field_name The name of the field on the bound fact.
 * @return The extracted value of type T.
 */
template <typename T>
inline T get_single_query_value(StatefulSession& session, std::string const& query_name,
                                std::string const& binding_name = "$r", std::string const& field_name = "result") {
    // 1. Execute the query using the improved API
    QueryResult result = session.execute_query(query_name);

    // 2. Use the expressive .single() method to get the unique row.
    //    This will throw an exception if the size is not 1, failing the test.
    QueryResultRow row = result.single();

    // 3. Use the safe .getFieldAs() method to get the value.
    //    This returns an optional, protecting against missing bindings or fields.
    std::optional<T> value_opt = row.getFieldAs<T>(binding_name, field_name);

    // 4. Assert that the value was actually found and had the correct type.
    REQUIRE(value_opt.has_value());

    return *value_opt;
}

// Test fixture to set up a session with a specific accumulate rule
struct AccumulateTestFixture {
    std::unique_ptr<StatefulSession> session;

    void build_session(std::string const& drl) {
        ParsingResult result;
        auto kb = build_knowledge_base(drl, result);
        if (!result.success) {
            for (auto const& err : result.errors) { FAIL(err.to_string()); }
        }
        REQUIRE(result.success);
        REQUIRE(kb != nullptr);
        session = kb->create_session();
        REQUIRE(session != nullptr);
    }

    std::shared_ptr<Fact> make_purchase(double value) {
        auto fact = std::make_shared<Fact>();
        fact->type = "Purchase";
        fact->fields["value"] = value;
        return fact;
    }
};

void check_all_values(StatefulSession& sess, double sum, int64_t count, double avg, double min, double max) {
    INFO("Checking values: sum=" << sum << ", count=" << count << ", avg=" << avg << ", min=" << min
                                 << ", max=" << max);
    REQUIRE(get_single_query_value<double>(sess, "getSum", "$r") == Catch::Approx(sum));
    REQUIRE(get_single_query_value<int64_t>(sess, "getCount", "$r") == count);
    REQUIRE(get_single_query_value<double>(sess, "getAverage", "$r") == Catch::Approx(avg));
    REQUIRE(get_single_query_value<double>(sess, "getMin", "$r") == Catch::Approx(min));
    REQUIRE(get_single_query_value<double>(sess, "getMax", "$r") == Catch::Approx(max));
}

TEST_CASE_METHOD(AccumulateTestFixture, "AccumulateNode: `sum`, `average`, `min`, `max`", "[accumulate][functions]") {
    build_session(R"(
declare Purchase value: double end

// Result fact types
declare SumResult      result: double end
declare AverageResult  result: double end
declare MinResult      result: double end
declare MaxResult      result: double end
declare CountResult    result: int end

// --- Rules using the accumulators ---
rule "Calculate Sum"
when
    $s: SumResult() from accumulate(
        $p: Purchase(),
        sum($p.value)
    )
then end

rule "Calculate Average"
when
    $a: AverageResult() from accumulate(
        $p: Purchase(),
        average($p.value)
    )
then end

rule "Calculate Min"
when
    $m: MinResult() from accumulate(
        $p: Purchase(),
        min($p.value)
    )
then end

rule "Calculate Max"
when
    $m: MaxResult() from accumulate(
        $p: Purchase(),
        max($p.value)
    )
then end

rule "Calculate Count"
when
    $c: CountResult() from accumulate(
        $p: Purchase(),
        count($p.value)
    )
then end

// --- Queries to inspect the results ---
query "getSum"      $r: SumResult() end
query "getAverage"  $r: AverageResult() end
query "getMin"      $r: MinResult() end
query "getMax"      $r: MaxResult() end
query "getCount"    $r: CountResult() end
    )");



    // 1. Initial state
    check_all_values(*session, /*sum*/ 0.0, /*count*/ 0, /*avg*/ 0.0, /*min*/ 0.0, /*max*/ 0.0);

    // 2. Add first two facts
    session->add_fact(make_purchase(10.0));
    session->add_fact(make_purchase(20.0));
    check_all_values(*session, /*sum*/ 30.0, /*count*/ 2, /*avg*/ 15.0, /*min*/ 10.0, /*max*/ 20.0);

    // 3. Add a third fact
    auto p3 = make_purchase(60.0);
    session->add_fact(p3);
    check_all_values(*session, /*sum*/ 90.0, /*count*/ 3, /*avg*/ 30.0, /*min*/ 10.0, /*max*/ 60.0);

    // 4. Retract a fact
    session->retract_fact(p3);
    check_all_values(*session, /*sum*/ 30.0, /*count*/ 2, /*avg*/ 15.0, /*min*/ 10.0, /*max*/ 20.0);
}


