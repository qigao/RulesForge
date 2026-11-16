#include "script/script_values.hpp"
#include <tinytest.hpp>

using namespace rulesforge;
namespace {
rf_script_limits limits() {
  rf_script_limits result{};
  result.value_bytes = 4096;
  result.value_nodes = 64;
  result.value_depth = 8;
  return result;
}
ScriptShape record_shape() {
  ScriptShape number;
  number.kind = ScriptKind::Number;
  ScriptShape inner;
  inner.kind = ScriptKind::Record;
  inner.fields = {{"heatKw", number}};
  ScriptShape outer;
  outer.kind = ScriptKind::Record;
  outer.fields = {{"reading", inner}};
  return outer;
}
} // namespace
suite("Typed script value diagnostics") {
  it("identifies the nested input field without coercing its type") {
    auto inner = std::make_shared<ValueMap>();
    inner->entries.emplace("heatKw", int64_t(42));
    auto outer = std::make_shared<ValueMap>();
    outer->entries.emplace("reading", inner);
    ScriptValues values(limits());
    check_throws_with(values.view(outer, record_shape()),
                      "Field reading: Field heatKw: Script value does not match its schema");
  }
  it("identifies the nested returned field without accepting an integer as a double") {
    toad_script_value_view_t integer{};
    integer.kind = TOAD_SCRIPT_VALUE_INT64;
    integer.as.integer = 42;
    toad_script_record_entry_view_t inner{{"heatKw", 6}, integer};
    toad_script_value_view_t record{};
    record.kind = TOAD_SCRIPT_VALUE_RECORD;
    record.as.record = {&inner, 1};
    toad_script_record_entry_view_t outer{{"reading", 7}, record};
    record.as.record = {&outer, 1};
    ScriptValues values(limits());
    check_throws_with(values.copy(record, record_shape()),
                      "Field reading: Field heatKw: Script value does not match its schema");
  }
}
