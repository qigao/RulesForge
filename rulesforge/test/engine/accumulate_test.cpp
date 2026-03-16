#include "rfl_parser.hpp"
#include "engine/knowledge_base.hpp"
#include "engine/query_result.hpp"
#include "engine/stateful_session.hpp"
#include "tinytest.h"

template <typename T>
T get_field(Fact const& f, std::string const& field) {
    auto val_opt = f.get_field(field);
    if (!val_opt.has_value()) { throw std::runtime_error("Field '" + field + "' not found"); }
    if (auto val_ptr = std::get_if<T>(&*val_opt)) { return *val_ptr; }
    if constexpr (std::is_same_v<T, double>) {
        if (auto int_ptr = std::get_if<int64_t>(&*val_opt)) { return static_cast<double>(*int_ptr); }
    }
    throw std::runtime_error("Field '" + field + "' has an unexpected type.");
}

template <typename T>
inline T get_single_query_value(StatefulSession& session, std::string const& query_name,
                                std::string const& binding_name = "$r", std::string const& field_name = "result") {
    QueryResult result = session.execute_query(query_name);
    QueryResultRow row = result.single();
    std::optional<T> value_opt = row.getFieldAs<T>(binding_name, field_name);
    if (!value_opt.has_value()) { throw std::runtime_error("Query '" + query_name + "' returned no value"); }
    return *value_opt;
}

struct AccumulateTestFixture {
    std::shared_ptr<KnowledgeBase> kb;
    std::unique_ptr<StatefulSession> session;
    std::vector<std::shared_ptr<Fact>> facts_;

    void build_session(std::string const& drl) {
        ParsingResult result;
        kb = build_knowledge_base(drl, result);
        if (!result.success) {
            for (auto const& err : result.errors) {
                throw std::runtime_error("RFL parsing failed: " + err.to_string());
            }
            throw std::runtime_error("RFL parsing failed: Unknown error");
        }
        if (!kb) { throw std::runtime_error("KnowledgeBase is null"); }
        session = kb->create_session();
        if (!session) { throw std::runtime_error("Session is null"); }
    }

    std::shared_ptr<Fact> make_purchase(double value) {
        auto fact = std::make_shared<Fact>();
        fact->type = "Purchase";
        fact->fields["value"] = value;
        facts_.push_back(fact);
        return fact;
    }
};

void verify_all_values(StatefulSession& sess, double sum, int64_t count, double avg, double min, double max) {
    auto check_approx = [](double actual, double expected, std::string const& name) {
        if (std::abs(actual - expected) >= 0.001) {
            throw std::runtime_error(name + " mismatch: expected " + std::to_string(expected) + ", got " + std::to_string(actual));
        }
    };
    check_approx(get_single_query_value<double>(sess, "getSum", "$r"), sum, "sum");
    if (get_single_query_value<int64_t>(sess, "getCount", "$r") != count) {
        throw std::runtime_error("count mismatch");
    }
    check_approx(get_single_query_value<double>(sess, "getAverage", "$r"), avg, "avg");
    check_approx(get_single_query_value<double>(sess, "getMin", "$r"), min, "min");
    check_approx(get_single_query_value<double>(sess, "getMax", "$r"), max, "max");
}

suite("Accumulate Functions") {
    group("sum, average, min, max") {
        it("calculates all aggregate functions correctly") {
            AccumulateTestFixture fixture;
            fixture.build_session(R"(
declare Purchase value: double end

declare SumResult      result: double end
declare AverageResult  result: double end
declare MinResult      result: double end
declare MaxResult      result: double end
declare CountResult    result: int end

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

query "getSum"      $r: SumResult() end
query "getAverage"  $r: AverageResult() end
query "getMin"      $r: MinResult() end
query "getMax"      $r: MaxResult() end
query "getCount"    $r: CountResult() end
            )");

            auto p1 = fixture.make_purchase(10.0);
            fixture.session->add_fact(p1);
            check(fixture.session->get_fact_count() >= 1);
        }
    }
}
