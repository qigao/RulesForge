#include "script/script_values.hpp"
#include <algorithm>
#include <climits>
#include <cmath>
#include <core/uuid.h>

namespace rulesforge {
namespace {
[[noreturn]] void mismatch() { throw std::runtime_error("Script value does not match its schema"); }
toad_script_value_view_t integer(int64_t n) {
  toad_script_value_view_t v{};
  v.kind = TOAD_SCRIPT_VALUE_INT64;
  v.as.integer = n;
  return v;
}
toad_script_value_view_t text(std::string const &s) {
  toad_script_value_view_t v{};
  v.kind = TOAD_SCRIPT_VALUE_STRING;
  v.as.string = {s.data(), s.size()};
  return v;
}
ScriptShape shape(ScriptKind k) {
  ScriptShape s;
  s.kind = k;
  return s;
}
ScriptShape decimal_shape() {
  ScriptShape s = shape(ScriptKind::Record);
  s.fields = {{"mantissa", shape(ScriptKind::Integer)}, {"scale", shape(ScriptKind::Integer)}};
  return s;
}
ScriptShape money_shape() {
  ScriptShape s = shape(ScriptKind::Record);
  s.fields = {{"amount", decimal_shape()}, {"currency", shape(ScriptKind::String)}};
  return s;
}
ConstraintValue const &field(ValueMap const &map, char const *name) {
  auto it = map.entries.find(std::string(name));
  if (it == map.entries.end())
    mismatch();
  return it->second;
}
} // namespace

void ScriptValues::fail(char const *message) const {
  std::vector<std::string_view> fields;
  for (auto path = field_path_; path; path = path->parent)
    fields.push_back(path->name);
  std::string diagnostic;
  for (auto it = fields.rbegin(); it != fields.rend(); ++it) {
    diagnostic.append("Field ");
    diagnostic.append(*it);
    diagnostic.append(": ");
  }
  diagnostic.append(message);
  throw std::runtime_error(diagnostic);
}
void ScriptValues::mismatch() const { fail("Script value does not match its schema"); }

void ScriptValues::charge(size_t bytes, size_t depth) {
  if (depth > limits_.value_depth || nodes_ >= limits_.value_nodes ||
      bytes > limits_.value_bytes - bytes_)
    throw std::length_error("Script value resource limit exceeded");
  ++nodes_;
  bytes_ += bytes;
}
toad_script_value_view_t ScriptValues::view(ConstraintValue const &v, ScriptShape const &s) {
  return view_at(v, s, 1);
}
ConstraintValue ScriptValues::copy(toad_script_value_view_t const &v, ScriptShape const &s) {
  return copy_at(v, s, 1);
}

toad_script_value_view_t ScriptValues::view_at(ConstraintValue const &v, ScriptShape const &s,
                                               size_t depth) {
  charge(sizeof(toad_script_value_view_t), depth);
  toad_script_value_view_t out{};
  if (std::holds_alternative<NilValue>(v)) {
    if (!s.nullable && s.kind != ScriptKind::Null)
      mismatch();
    return out;
  }
  switch (s.kind) {
  case ScriptKind::Boolean:
    if (auto p = std::get_if<bool>(&v)) {
      out.kind = TOAD_SCRIPT_VALUE_BOOL;
      out.as.boolean = *p;
      return out;
    }
    break;
  case ScriptKind::Integer:
    if (auto p = std::get_if<int64_t>(&v))
      return integer(*p);
    break;
  case ScriptKind::Unsigned:
    if (auto p = std::get_if<uint64_t>(&v)) {
      if (*p > INT64_MAX)
        mismatch();
      return integer(static_cast<int64_t>(*p));
    }
    break;
  case ScriptKind::Number:
    if (auto p = std::get_if<double>(&v)) {
      if (!std::isfinite(*p))
        mismatch();
      out.kind = TOAD_SCRIPT_VALUE_NUMBER;
      out.as.number = *p;
      return out;
    }
    break;
  case ScriptKind::String:
    if (auto p = std::get_if<std::string>(&v)) {
      charge(p->size(), depth);
      return text(*p);
    }
    break;
  case ScriptKind::Uuid:
    if (auto p = std::get_if<UuidValue>(&v)) {
      char buffer[SALTS_UUID_STRING_SIZE];
      if (uuid_format(p->data(), buffer, sizeof(buffer)) != 0)
        mismatch();
      strings_.emplace_back(buffer);
      charge(strings_.back().size(), depth);
      return text(strings_.back());
    }
    break;
  case ScriptKind::Decimal:
    if (auto p = std::get_if<DecimalValue>(&v)) {
      charge(2 * sizeof(toad_script_record_entry_view_t), depth + 1);
      records_.push_back(
          {{{"mantissa", 8}, integer(p->mantissa)}, {{"scale", 5}, integer(p->scale)}});
      out.kind = TOAD_SCRIPT_VALUE_RECORD;
      out.as.record = {records_.back().data(), 2};
      return out;
    }
    break;
  case ScriptKind::Money:
    if (auto p = std::get_if<MoneyValue>(&v)) {
      charge(2 * sizeof(toad_script_record_entry_view_t), depth + 1);
      records_.emplace_back(2);
      auto &entries = records_.back();
      entries[0] = {{"amount", 6}, view_at(p->amount, shape(ScriptKind::Decimal), depth + 1)};
      charge(p->currency.size(), depth + 1);
      entries[1] = {{"currency", 8}, text(p->currency)};
      out.kind = TOAD_SCRIPT_VALUE_RECORD;
      out.as.record = {entries.data(), 2};
      return out;
    }
    break;
  case ScriptKind::Array:
    if (auto p = std::get_if<std::shared_ptr<TypedList>>(&v); p && *p && s.element) {
      if (!visiting_.insert(p->get()).second)
        fail("Cyclic script value");
      auto const &values = (*p)->values;
      if (values.size() > limits_.value_nodes - nodes_)
        throw std::length_error("Script value node limit exceeded");
      arrays_.emplace_back(values.size());
      auto &items = arrays_.back();
      for (size_t i = 0; i < items.size(); ++i)
        items[i] = view_at(values[i], *s.element, depth + 1);
      visiting_.erase(p->get());
      out.kind = TOAD_SCRIPT_VALUE_ARRAY;
      out.as.array = {items.data(), items.size()};
      return out;
    }
    break;
  case ScriptKind::Record:
    if (auto p = std::get_if<std::shared_ptr<ValueMap>>(&v); p && *p) {
      if (!visiting_.insert(p->get()).second)
        fail("Cyclic script value");
      if ((*p)->entries.size() != s.fields.size())
        mismatch();
      records_.emplace_back(s.fields.size());
      auto &entries = records_.back();
      for (size_t i = 0; i < s.fields.size(); ++i) {
        auto const &[name, type] = s.fields[i];
        FieldScope field(*this, name);
        auto it = (*p)->entries.find(name);
        if (it == (*p)->entries.end())
          mismatch();
        charge(name.size(), depth + 1);
        entries[i] = {{name.data(), name.size()}, view_at(it->second, type, depth + 1)};
      }
      visiting_.erase(p->get());
      out.kind = TOAD_SCRIPT_VALUE_RECORD;
      out.as.record = {entries.data(), entries.size()};
      return out;
    }
    break;
  case ScriptKind::Null:
    break;
  }
  mismatch();
}

ConstraintValue ScriptValues::copy_at(toad_script_value_view_t const &v, ScriptShape const &s,
                                      size_t depth) {
  charge(sizeof(ConstraintValue), depth);
  if (v.kind == TOAD_SCRIPT_VALUE_NULL) {
    if (!s.nullable && s.kind != ScriptKind::Null)
      mismatch();
    return NilValue{};
  }
  switch (s.kind) {
  case ScriptKind::Boolean:
    if (v.kind == TOAD_SCRIPT_VALUE_BOOL && v.as.boolean <= 1)
      return bool(v.as.boolean);
    break;
  case ScriptKind::Integer:
    if (v.kind == TOAD_SCRIPT_VALUE_INT64)
      return v.as.integer;
    break;
  case ScriptKind::Unsigned:
    if (v.kind == TOAD_SCRIPT_VALUE_INT64 && v.as.integer >= 0)
      return static_cast<uint64_t>(v.as.integer);
    break;
  case ScriptKind::Number:
    if (v.kind == TOAD_SCRIPT_VALUE_NUMBER && std::isfinite(v.as.number))
      return v.as.number;
    break;
  case ScriptKind::String:
  case ScriptKind::Uuid:
    if (v.kind == TOAD_SCRIPT_VALUE_STRING) {
      if (!v.as.string.data && v.as.string.size)
        mismatch();
      charge(v.as.string.size, depth);
      std::string t(v.as.string.data ? v.as.string.data : "", v.as.string.size);
      if (s.kind == ScriptKind::String)
        return t;
      UuidValue uuid{};
      if (t.size() != 36 || uuid_parse_text(t.c_str(), uuid.data()) != 0)
        mismatch();
      return uuid;
    }
    break;
  case ScriptKind::Array:
    if (v.kind == TOAD_SCRIPT_VALUE_ARRAY && s.element) {
      if ((v.as.array.count && !v.as.array.items) ||
          v.as.array.count > limits_.value_nodes - nodes_)
        throw std::length_error("Script array node limit exceeded");
      auto list = std::make_shared<TypedList>();
      list->values.reserve(v.as.array.count);
      for (size_t i = 0; i < v.as.array.count; ++i)
        list->values.push_back(copy_at(v.as.array.items[i], *s.element, depth + 1));
      return list;
    }
    break;
  case ScriptKind::Decimal: {
    auto record = std::get<std::shared_ptr<ValueMap>>(copy_at(v, decimal_shape(), depth + 1));
    auto scale = std::get<int64_t>(field(*record, "scale"));
    if (scale < INT32_MIN || scale > INT32_MAX)
      mismatch();
    return DecimalValue{std::get<int64_t>(field(*record, "mantissa")), static_cast<int32_t>(scale)};
  }
  case ScriptKind::Money: {
    auto record = std::get<std::shared_ptr<ValueMap>>(copy_at(v, money_shape(), depth + 1));
    auto amount = std::get<std::shared_ptr<ValueMap>>(field(*record, "amount"));
    auto scale = std::get<int64_t>(field(*amount, "scale"));
    if (scale < INT32_MIN || scale > INT32_MAX)
      mismatch();
    return MoneyValue{{std::get<int64_t>(field(*amount, "mantissa")), static_cast<int32_t>(scale)},
                      std::get<std::string>(field(*record, "currency"))};
  }
  case ScriptKind::Record:
    if (v.kind == TOAD_SCRIPT_VALUE_RECORD) {
      if (v.as.record.count != s.fields.size() || (v.as.record.count && !v.as.record.entries))
        mismatch();
      auto result = std::make_shared<ValueMap>();
      for (size_t i = 0; i < v.as.record.count; ++i) {
        auto const &entry = v.as.record.entries[i];
        if (!entry.key.data && entry.key.size)
          mismatch();
        charge(entry.key.size, depth + 1);
        std::string_view name(entry.key.data ? entry.key.data : "", entry.key.size);
        FieldScope field(*this, name);
        auto it = std::find_if(s.fields.begin(), s.fields.end(),
                               [&](auto const &f) { return f.name == name; });
        if (it == s.fields.end() || result->entries.contains(std::string(name)))
          mismatch();
        result->entries.emplace(it->name, copy_at(entry.value, it->shape, depth + 1));
      }
      return result;
    }
    break;
  case ScriptKind::Null:
    break;
  }
  mismatch();
}
} // namespace rulesforge
