#include "runtime/stateful_session.hpp"
#include "test_helpers.hpp"
#include <tinytest.hpp>

namespace {
std::shared_ptr<ValueMap> metrics(bool ready, int64_t tick, double load) {
  auto result = std::make_shared<ValueMap>();
  result->entries["ready"] = ready;
  result->entries["tick"] = tick;
  result->entries["load"] = load;
  return result;
}
std::unique_ptr<StatefulSession> nested_session(std::string const &constraint) {
  ParsingResult parsed;
  auto kb = build_knowledge_base("declare Metrics ready: bool tick: long load: double end\n"
                                 "declare Snapshot metrics: Metrics end\n"
                                 "declare Request tick: long metrics: Metrics end\n"
                                 "query \"Matches\" $request: Request() $row: Snapshot(" +
                                     constraint +
                                     ") end\n"
                                     "query \"Missing\" $request: Request() not Snapshot(" +
                                     constraint + ") end\n",
                                 parsed);
  if (!parsed.success)
    throw_parse_failure(parsed);
  auto session = kb->create_session();
  auto request = std::make_shared<Fact>();
  request->type = "Request";
  request->fields["tick"] = int64_t(123);
  request->fields["metrics"] = metrics(true, 123, 40);
  session->add_fact(request);
  return session;
}
std::shared_ptr<Fact> snapshot(StatefulSession &session, bool ready = true) {
  auto fact = std::make_shared<Fact>();
  fact->type = "Snapshot";
  fact->fields["metrics"] = metrics(ready, 123, 60);
  session.add_fact(fact);
  return fact;
}
} // namespace

suite("Declared nested scalar constraint admission") {
  it("reads owned DataBind record fields through a dotted path") {
    Fact fact;
    fact.fields["metrics"] = metrics(true, 123, 60);
    auto ready = fact.get_field("metrics.ready");
    check_true(ready.has_value());
    check_true(std::get<bool>(*ready));
    auto time = fact.get_field("metrics.tick");
    check_true(time.has_value());
    check_equal(std::get<int64_t>(*time), 123);
    check_false(fact.get_field("metrics.absent").has_value());
  }
  it("reads nested map children and preserves explicit null-safe access") {
    Fact fact;
    auto outer = std::make_shared<ValueMap>();
    outer->entries["child"] = metrics(true, 123, 60);
    fact.fields["metrics"] = outer;
    auto nested = fact.get_field("metrics.child.ready");
    check_true(nested.has_value());
    check_true(std::get<bool>(*nested));
    auto indexed = fact.get_field("metrics[\"child\"].tick");
    check_true(indexed.has_value());
    check_equal(std::get<int64_t>(*indexed), 123);
    fact.fields["metrics"] = std::shared_ptr<ValueMap>{};
    check_false(fact.get_field("metrics.ready").has_value());
    auto safe = fact.get_field("metrics!.ready");
    check_true(safe.has_value());
    check_true(std::holds_alternative<NilValue>(*safe));
  }
  it("resolves nested scalar types and values on both sides of a join") {
    auto session = nested_session("metrics.load > $request.metrics.load");
    snapshot(*session);
    check_equal(session->execute_query("Matches").size(), 1u);
  }
  it("preserves false boolean values instead of treating record existence as truth") {
    auto session = nested_session("metrics.ready == false");
    snapshot(*session, false);
    check_equal(session->execute_query("Matches").size(), 1u);
    check_equal(session->execute_query("Missing").size(), 0u);
  }
  it("uses the boolean leaf type for a nested literal predicate and its NOT blocker") {
    auto session = nested_session("metrics.ready == true");
    snapshot(*session);
    check_equal(session->execute_query("Matches").size(), 1u);
    check_equal(session->execute_query("Missing").size(), 0u);
  }
  it("uses the numeric leaf type for a nested threshold") {
    auto session = nested_session("metrics.load >= 40.0");
    snapshot(*session);
    check_equal(session->execute_query("Matches").size(), 1u);
  }
  it("joins a nested timestamp to a bound scalar without comparing the containing object") {
    auto session = nested_session("metrics.tick == $request.tick, metrics.ready == true");
    snapshot(*session);
    check_equal(session->execute_query("Matches").size(), 1u);
  }
  it("retracts and recreates nested matches after UPDATE replaces the record") {
    auto session = nested_session("metrics.ready == true, metrics.load >= 40.0");
    auto fact = snapshot(*session);
    check_equal(session->execute_query("Matches").size(), 1u);
    session->update_fact(fact.get(),
                         [](Fact &value) { value.fields["metrics"] = metrics(false, 123, 60); });
    check_equal(session->execute_query("Matches").size(), 0u);
    check_equal(session->execute_query("Missing").size(), 1u);
    session->update_fact(fact.get(),
                         [](Fact &value) { value.fields["metrics"] = metrics(true, 123, 60); });
    check_equal(session->execute_query("Matches").size(), 1u);
  }
}
