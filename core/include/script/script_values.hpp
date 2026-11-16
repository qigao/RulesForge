#pragma once
#include "script/script_catalog.hpp"
#include <deque>
#include <set>
#include <string_view>

namespace rulesforge {
class ScriptValues {
public:
  explicit ScriptValues(rf_script_limits const &limits) : limits_(limits) {}
  toad_script_value_view_t view(ConstraintValue const &, ScriptShape const &);
  ConstraintValue copy(toad_script_value_view_t const &, ScriptShape const &);
  size_t bytes_used() const { return bytes_; }

private:
  struct FieldPath {
    std::string_view name;
    FieldPath const *parent;
  };
  struct FieldScope {
    ScriptValues &owner;
    FieldPath path;
    FieldScope(ScriptValues &owner, std::string_view name)
        : owner(owner), path{name, owner.field_path_} {
      owner.field_path_ = &path;
    }
    ~FieldScope() { owner.field_path_ = path.parent; }
    FieldScope(FieldScope const &) = delete;
  };
  [[noreturn]] void fail(char const *message) const;
  [[noreturn]] void mismatch() const;
  void charge(size_t bytes, size_t depth);
  toad_script_value_view_t view_at(ConstraintValue const &, ScriptShape const &, size_t);
  ConstraintValue copy_at(toad_script_value_view_t const &, ScriptShape const &, size_t);
  rf_script_limits limits_;
  size_t bytes_ = 0, nodes_ = 0;
  FieldPath const *field_path_ =
      nullptr; // Borrowed stack scopes, never retained beyond conversion.
  std::deque<std::string> strings_;
  std::deque<std::vector<toad_script_value_view_t>> arrays_;
  std::deque<std::vector<toad_script_record_entry_view_t>> records_;
  std::set<void const *> visiting_;
};
} // namespace rulesforge
