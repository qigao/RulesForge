#pragma once
#include "model/value_types.hpp"
#include <span>

namespace rulesforge {
class ScriptInvoker {
public:
  virtual ~ScriptInvoker() = default;
  virtual void begin_rhs() = 0;
  virtual void end_rhs() noexcept = 0;
  virtual ConstraintValue invoke(size_t binding, std::span<ConstraintValue const> arguments) = 0;
};
} // namespace rulesforge
