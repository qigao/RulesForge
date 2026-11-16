#include "runtime/query_result.hpp"
#include "runtime/stateful_session.hpp"
#include "rfl_parser.hpp"
#include "test_helpers.hpp"
#include "tinytest.hpp"
#include <array>
#include <memory>
#include <vector>

namespace {
std::shared_ptr<KnowledgeBase> compile(std::string const &condition) {
  ParsingResult result;
  auto kb = build_knowledge_base(
      "declare Input value: double timestamp: long end\n"
      "declare Threshold minimum: int end\n"
      "declare Total result: int end\n"
      "declare Match end\n"
      "rule \"Threshold\" when " + condition +
      " then insertLogical Match {} end\n"
      "query \"Matches\" $m: Match() end\n", result);
  if (!result.success) throw_parse_failure(result);
  return kb;
}
std::shared_ptr<Fact> input(StatefulSession &session) {
  auto fact = std::make_shared<Fact>();
  fact->type = "Input";
  fact->fields["value"] = 1.0;
  session.add_fact(fact);
  return fact;
}
size_t matches(StatefulSession &session) {
  session.fire_all_rules(20);
  auto result = session.execute_query("Matches");
  if (!result.success()) throw std::runtime_error(result.error_message());
  return result.size();
}
}

suite("Accumulate result constraints") {
  it("preserves disjunctions in source predicates across MODIFY and RETRACT") {
    auto kb = compile(
        "$total: Total(result >= 1) from accumulate(Input(value < 2.0 || value > 4.0), count())");
    auto session = kb->create_session();
    auto fact = input(*session);
    check_equal(matches(*session), size_t{1});
    session->update_fact(fact.get(), [](Fact &value) { value.fields["value"] = 3.0; });
    check_equal(matches(*session), size_t{0});
    session->update_fact(fact.get(), [](Fact &value) { value.fields["value"] = 5.0; });
    check_equal(matches(*session), size_t{1});
    session->retract_fact(fact);
    check_equal(matches(*session), size_t{0});
  }
  it("preserves mixed correlated and literal source branches when the outer fact changes") {
    auto kb = compile("$threshold: Threshold() $total: Total(result >= 1) from accumulate("
                      "Input(value < $threshold.minimum || timestamp < 10), count())");
    auto session = kb->create_session();
    auto threshold = std::make_shared<Fact>();
    threshold->type = "Threshold";
    threshold->fields["minimum"] = int64_t(40);
    session->add_fact(threshold);
    auto fact = std::make_shared<Fact>();
    fact->type = "Input";
    fact->fields["value"] = 60.0;
    fact->fields["timestamp"] = int64_t(5);
    session->add_fact(fact);
    check_equal(matches(*session), size_t{1});
    session->update_fact(fact.get(), [](Fact &value) { value.fields["timestamp"] = int64_t(20); });
    check_equal(matches(*session), size_t{0});
    session->update_fact(threshold.get(),
                         [](Fact &value) { value.fields["minimum"] = int64_t(70); });
    check_equal(matches(*session), size_t{1});
  }
  it("counts a fact satisfying both OR branches exactly once") {
    auto kb = compile("$total: Total(result >= 2) from accumulate(Input(value < 2.0 || timestamp < "
                      "10), count())");
    auto session = kb->create_session();
    auto first = std::make_shared<Fact>();
    first->type = "Input";
    first->fields["value"] = 1.0;
    first->fields["timestamp"] = int64_t(5);
    session->add_fact(first);
    check_equal(matches(*session), size_t{0});
    auto second = input(*session);
    check_equal(matches(*session), size_t{1});
    session->retract_fact(second);
    check_equal(matches(*session), size_t{0});
  }
  it("asserts and retracts a logical consequence when a count crosses a literal threshold") {
    auto kb = compile("$total: Total(result >= 2) from accumulate(Input(), count())");
    auto session = kb->create_session();
    check_equal(matches(*session), size_t{0});
    auto first = input(*session);
    check_equal(matches(*session), size_t{0});
    auto second = input(*session);
    check_equal(matches(*session), size_t{1});
    session->retract_fact(second);
    check_equal(matches(*session), size_t{0});
    input(*session);
    check_equal(matches(*session), size_t{1});
  }
  it("preserves AND and OR branches when aggregate results change") {
    auto kb = compile("$total: Total(result < 2 || result >= 3 && result < 5) "
                      "from accumulate(Input(), count())");
    auto session = kb->create_session();
    check_equal(matches(*session), size_t{1});
    std::vector<std::shared_ptr<Fact>> facts;
    std::array<size_t, 5> expected{1, 0, 1, 1, 0};
    for (auto count : expected) {
      facts.push_back(input(*session));
      check_equal(matches(*session), count);
    }
    session->retract_fact(facts.back());
    check_equal(matches(*session), size_t{1});
  }
  it("resolves an aggregate threshold from a prior binding and withdraws it with its owner") {
    auto kb = compile("$threshold: Threshold() $total: Total(result >= $threshold.minimum) "
                      "from accumulate(Input(), count())");
    auto session = kb->create_session();
    auto threshold = std::make_shared<Fact>();
    threshold->type = "Threshold";
    threshold->fields["minimum"] = int64_t{2};
    session->add_fact(threshold);
    input(*session);
    check_equal(matches(*session), size_t{0});
    input(*session);
    check_equal(matches(*session), size_t{1});
    session->retract_fact(threshold);
    check_equal(matches(*session), size_t{0});
  }
  it("applies literal membership to an aggregate result") {
    auto kb = compile("$total: Total(result in (1, 3)) from accumulate(Input(), count())");
    auto session = kb->create_session();
    check_equal(matches(*session), size_t{0});
    input(*session);
    check_equal(matches(*session), size_t{1});
    input(*session);
    check_equal(matches(*session), size_t{0});
    input(*session);
    check_equal(matches(*session), size_t{1});
  }
  it("applies IN and NOT IN literals to source facts when they are modified") {
    for (bool negate : {false, true}) {
      auto kb = compile(std::string("Input(value ") + (negate ? "not in" : "in") + " (2, 3))");
      auto session = kb->create_session();
      auto fact = input(*session);
      check_equal(matches(*session), negate ? size_t{1} : size_t{0});
      session->update_fact(fact.get(), [](Fact &value) { value.fields["value"] = 2.0; });
      check_equal(matches(*session), negate ? size_t{0} : size_t{1});
      session->update_fact(fact.get(), [](Fact &value) { value.fields["value"] = 4.0; });
      check_equal(matches(*session), negate ? size_t{1} : size_t{0});
    }
  }
  it("uses inline bound timestamps in correlated accumulate source arithmetic") {
    auto kb = compile("$threshold: Threshold($now: minimum) "
        "$total: Total(result >= 3) from accumulate("
        "Input(timestamp > ($now - 1000), timestamp < $now), count())");
    auto session = kb->create_session();
    auto threshold = std::make_shared<Fact>();
    threshold->type = "Threshold";
    threshold->fields["minimum"] = int64_t{10000};
    session->add_fact(threshold);
    for (int64_t timestamp : {int64_t{9000}, int64_t{9001}, int64_t{9500}}) {
      auto fact = std::make_shared<Fact>();
      fact->type = "Input";
      fact->fields["value"] = 1.0;
      fact->fields["timestamp"] = timestamp;
      session->add_fact(fact);
    }
    check_equal(matches(*session), size_t{0}); // Strict lower boundary excludes 9000.
    auto last = std::make_shared<Fact>();
    last->type = "Input";
    last->fields["value"] = 1.0;
    last->fields["timestamp"] = int64_t{9999};
    session->add_fact(last);
    check_equal(matches(*session), size_t{1});
    session->retract_fact(last);
    check_equal(matches(*session), size_t{0});
  }
  it("resolves inline bound fields in arithmetic result thresholds") {
    auto kb = compile("$threshold: Threshold($minimum: minimum) "
        "$total: Total(result >= ($minimum + 0)) from accumulate(Input(), count())");
    auto session = kb->create_session();
    auto threshold = std::make_shared<Fact>();
    threshold->type = "Threshold";
    threshold->fields["minimum"] = int64_t{2};
    session->add_fact(threshold);
    input(*session);
    check_equal(matches(*session), size_t{0});
    input(*session);
    check_equal(matches(*session), size_t{1});
  }
}
