// RETE profiling benchmark (diagnostic-only).
// Keeps production benchmark output clean while exposing hot-path counters.

#include "engine/knowledge_base.hpp"
#include "engine/rhs_executor.hpp"
#include "engine/stateful_session.hpp"
#include "parser/expression_evaluator.hpp"
#include "parser/rfl_parser.hpp"
#include "rete/rete_node.hpp"
#include "tinytest.h"

#include <chrono>
#include <cstdio>
#include <random>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "data/fact_builder.hpp"

using Clock = std::chrono::high_resolution_clock;
using Microseconds = std::chrono::microseconds;

static std::vector<Fact*> create_facts(int count, std::string const& type = "benchmark.TestFact") {
  std::vector<Fact*> facts;
  facts.reserve(count);

  std::mt19937 gen(42);
  std::uniform_int_distribution<> value_dist(0, 109);
  std::vector<std::string> categories = {"A", "B", "C", "D", "E"};

  static const rulesforge::InternedString KEY_ID(
      rulesforge::StringInterner::instance().intern_persistent("id"));
  static const rulesforge::InternedString KEY_VALUE(
      rulesforge::StringInterner::instance().intern_persistent("value"));
  static const rulesforge::InternedString KEY_CATEGORY(
      rulesforge::StringInterner::instance().intern_persistent("category"));

  for (int i = 0; i < count; ++i) {
    auto* fact = FactBuilder::create(type)
                     .set(KEY_ID, static_cast<int64_t>(i))
                     .set(KEY_VALUE, static_cast<int64_t>(value_dist(gen)))
                     .set(KEY_CATEGORY, categories[i % categories.size()])
                     .build();
    facts.push_back(fact);
  }
  return facts;
}

static std::shared_ptr<KnowledgeBase> build_processing_kb() {
  std::string drl = R"(
package benchmark
declare TestFact id: int, value: int, category: String end
rule "Process Facts"
when
    $f : TestFact(value > 50, category != "processed")
then
    update $f { category = "processed" }
end
)";
  ParsingResult result;
  auto kb = build_knowledge_base(drl, result);
  if (!result.success) throw std::runtime_error("Failed to build processing KB");
  kb->set_phreak_experimental(true);
  return kb;
}

static std::shared_ptr<KnowledgeBase> build_match_only_kb() {
  std::string drl = R"(
package benchmark
declare TestFact id: int, value: int, category: String end
rule "Match Facts Only"
when
    $f : TestFact(value > 50, category != "processed")
then
end
)";
  ParsingResult result;
  auto kb = build_knowledge_base(drl, result);
  if (!result.success) throw std::runtime_error("Failed to build match-only KB");
  kb->set_phreak_experimental(true);
  return kb;
}

static void run_profile(char const* label,
                        std::shared_ptr<KnowledgeBase> const& kb,
                        std::vector<Fact*> const& facts) {
  auto session = kb->create_session();

  session->reset_runtime_counters();
  rulesforge::rete_prof::reset_stats();
  rulesforge::rhs_prof::reset_stats();
  auto t_add0 = Clock::now();
  session->add_facts(facts);
  auto t_add1 = Clock::now();
  auto add_st = rulesforge::rete_prof::get_stats();
  auto add_rt = session->runtime_counters();
  auto add_rhs = rulesforge::rhs_prof::get_stats();
  auto add_us = std::chrono::duration_cast<Microseconds>(t_add1 - t_add0).count();
  std::printf(
      "[RETE_PROF_ADD] %s: add_us=%lld alpha=%llu join=%llu compare=%llu expr_eval=%llu lookups=%llu "
      "agenda_add=%llu agenda_pop=%llu fire_calls=%llu modify_calls=%llu pop_us=%llu pop_sel_us=%llu pop_det_us=%llu fire_us=%llu modify_us=%llu "
      "rhs_us=%llu book_us=%llu assign_us=%llu cond_us=%llu upd_us=%llu ins_us=%llu ret_us=%llu phreak_seg=%llu phreak_path=%llu phreak_flush=%llu\n",
      label,
      static_cast<long long>(add_us),
      static_cast<unsigned long long>(add_st.alpha_checks),
      static_cast<unsigned long long>(add_st.join_checks),
      static_cast<unsigned long long>(add_st.compare_calls),
      static_cast<unsigned long long>(add_st.compiled_expr_evals),
      static_cast<unsigned long long>(add_st.field_lookups),
      static_cast<unsigned long long>(add_rt.agenda_add_calls),
      static_cast<unsigned long long>(add_rt.agenda_pop_calls),
      static_cast<unsigned long long>(add_rt.fire_activation_calls),
      static_cast<unsigned long long>(add_rt.propagate_modify_calls),
      static_cast<unsigned long long>(add_rt.agenda_pop_time_us),
      static_cast<unsigned long long>(add_rt.agenda_pop_select_time_us),
      static_cast<unsigned long long>(add_rt.agenda_pop_detach_time_us),
      static_cast<unsigned long long>(add_rt.fire_activation_time_us),
      static_cast<unsigned long long>(add_rt.propagate_modify_time_us),
      static_cast<unsigned long long>(add_rt.rhs_execute_time_us),
      static_cast<unsigned long long>(add_rt.fire_bookkeeping_time_us),
      static_cast<unsigned long long>(add_rhs.evaluate_assignment_us),
      static_cast<unsigned long long>(add_rhs.condition_eval_us),
      static_cast<unsigned long long>(add_rhs.update_action_us),
      static_cast<unsigned long long>(add_rhs.insert_action_us),
      static_cast<unsigned long long>(add_rhs.retract_action_us),
      static_cast<unsigned long long>(add_rt.phreak_dirty_segment_marks),
      static_cast<unsigned long long>(add_rt.phreak_dirty_path_marks),
      static_cast<unsigned long long>(add_rt.phreak_flush_iterations));

  session->reset_runtime_counters();
  rulesforge::rete_prof::reset_stats();
  rulesforge::rhs_prof::reset_stats();
  auto t0 = Clock::now();
  session->fire_all_rules();
  auto t1 = Clock::now();
  auto st = rulesforge::rete_prof::get_stats();
  auto rt = session->runtime_counters();
  auto rhs = rulesforge::rhs_prof::get_stats();
  auto us = std::chrono::duration_cast<Microseconds>(t1 - t0).count();
  std::printf(
      "[RETE_PROF_FIRE] %s: fire_us=%lld alpha=%llu join=%llu compare=%llu expr_eval=%llu lookups=%llu "
      "agenda_add=%llu agenda_pop=%llu fire_calls=%llu modify_calls=%llu pop_us=%llu pop_sel_us=%llu pop_det_us=%llu fire_body_us=%llu modify_us=%llu "
      "rhs_us=%llu book_us=%llu assign_us=%llu cond_us=%llu upd_us=%llu ins_us=%llu ret_us=%llu phreak_seg=%llu phreak_path=%llu phreak_flush=%llu\n",
      label,
      static_cast<long long>(us),
      static_cast<unsigned long long>(st.alpha_checks),
      static_cast<unsigned long long>(st.join_checks),
      static_cast<unsigned long long>(st.compare_calls),
      static_cast<unsigned long long>(st.compiled_expr_evals),
      static_cast<unsigned long long>(st.field_lookups),
      static_cast<unsigned long long>(rt.agenda_add_calls),
      static_cast<unsigned long long>(rt.agenda_pop_calls),
      static_cast<unsigned long long>(rt.fire_activation_calls),
      static_cast<unsigned long long>(rt.propagate_modify_calls),
      static_cast<unsigned long long>(rt.agenda_pop_time_us),
      static_cast<unsigned long long>(rt.agenda_pop_select_time_us),
      static_cast<unsigned long long>(rt.agenda_pop_detach_time_us),
      static_cast<unsigned long long>(rt.fire_activation_time_us),
      static_cast<unsigned long long>(rt.propagate_modify_time_us),
      static_cast<unsigned long long>(rt.rhs_execute_time_us),
      static_cast<unsigned long long>(rt.fire_bookkeeping_time_us),
      static_cast<unsigned long long>(rhs.evaluate_assignment_us),
      static_cast<unsigned long long>(rhs.condition_eval_us),
      static_cast<unsigned long long>(rhs.update_action_us),
      static_cast<unsigned long long>(rhs.insert_action_us),
      static_cast<unsigned long long>(rhs.retract_action_us),
      static_cast<unsigned long long>(rt.phreak_dirty_segment_marks),
      static_cast<unsigned long long>(rt.phreak_dirty_path_marks),
      static_cast<unsigned long long>(rt.phreak_flush_iterations));
}

suite("RETE Profile Benchmarks") {
  bench("profiles 1K fire path counters") {
    auto kb_match = build_match_only_kb();
    auto kb_update = build_processing_kb();

    run_profile("match_only_1k", kb_match, create_facts(1000));
    run_profile("update_1k", kb_update, create_facts(1000));
    check(true);
  }
}
