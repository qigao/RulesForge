#pragma once
#include "script/script_invoker.hpp"
#include <memory>
#include <stdexcept>
#include <string>

class StatefulSession;
namespace rulesforge {
class ScriptProgram;
class ScriptProgramSession final : public ScriptInvoker {
public:
  ~ScriptProgramSession();
  std::unique_ptr<StatefulSession> create_engine();
  ConstraintValue invoke(size_t, std::span<ConstraintValue const>) override;
  void begin_rhs() override;
  void end_rhs() noexcept override;
  bool owner_thread() const;
  void close_runtime();

private:
  friend class ScriptProgram;
  struct State;
  explicit ScriptProgramSession(std::unique_ptr<State>);
  std::unique_ptr<State> state_;
};
class ScriptProgram {
public:
  explicit ScriptProgram(std::string const &bundle);
  ~ScriptProgram();
  ScriptProgram(ScriptProgram const &) = delete;
  ScriptProgram &operator=(ScriptProgram const &) = delete;
  void reload(std::string const &bundle);
  std::unique_ptr<ScriptProgramSession> create_session();
  void close();
  void destroy();

private:
  friend struct ScriptProgramSession::State;
  friend class ScriptProgramSession;
  struct State;
  std::unique_ptr<State> state_;
};
class ScriptStatusError : public std::runtime_error {
public:
  int status;
  ScriptStatusError(int code, std::string const &message)
      : std::runtime_error(message), status(code) {}
};
} // namespace rulesforge
