# Session Execution Admission Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在真实 session 执行期间拒绝同 session 的嵌套 fire/reset，保护将来预算观察点调用宿主时的生命周期。

**Architecture:** 在 StatefulSession 中维护一个 caller-serialized 执行标志，两种 fire 入口共享原有实现入口。局部 RAII 在所有返回/异常路径释放标志，reset 在任何清理前检查它；不新增线程同步、预算 API 或第二套状态机。

**Tech Stack:** C++20、Salts TinyTest、既有 CMake user presets。

**Spec:** [有界执行与 session 生命周期](../../architecture/bounded-execution.md)，第一阶段。
跟踪：[RulesForge #7](https://github.com/qigao/RulesForge/issues/7)，父任务 #6。

## Global Constraints

- 本阶段只实现执行准入，不声明已支持步骤配额、取消、deadline、owner-thread 或在途 destroy。
- 同 session 的嵌套 fire/reset 在任何状态修改前抛出 `std::logic_error`；不同 session 不互斥。
- 不新增 C ABI 导出、C/CMake fallback、静态引擎替代或 DLL 复制规则。
- `fire_all_rules` 与 `fire_all_rules_fail_fast` 共享同一执行标志；正常返回、提前返回和异常退出均释放。
- session 仍要求 caller-serialized 使用；普通 bool 不是跨线程安全承诺。
- 测试使用真实 parser/KB/session/listener。保留现有规则、事务、halt 和 C API 行为，除了明确拒绝原先不安全的重入/reset。
- Windows configure/build/test 使用 VsDevCmd 和 `win-dev-user`、`win-release-user`，不得改弱 sanitizer 或依赖检查。

### Task 1: 执行准入与真实 listener 回归

**Files:**
- Modify: `rulesforge/include/engine/stateful_session.hpp`
- Modify: `rulesforge/src/engine/stateful_session.cpp`
- Modify: `rulesforge/test/engine/engine_session_test.cpp`
- Modify: 本计划，仅在完成验证后勾选进度。

**Interfaces:**
- Consumes: `StatefulSession::fire_all_rules(int)`、`fire_all_rules_fail_fast(int)`、`reset()`、`addListener(shared_ptr<IEngineListener>)`。
- Produces: 上述已有入口的执行期拒绝语义；无新增公开方法。

- [x] **Step 1: 添加同 session 重入的行为测试。**

在测试文件中添加明确的 listener helper，避免嵌套导致无限递归；不读取生产私有状态：

```cpp
struct ReentrantFireListener final : IEngineListener {
    StatefulSession& session;
    bool fail_fast;
    bool attempted = false;
    bool rejected = false;
    explicit ReentrantFireListener(StatefulSession& s, bool strict)
        : session(s), fail_fast(strict) {}
    void before_rule_fired(std::string const&) override {
        if (attempted) return;
        attempted = true;
        try {
            if (fail_fast) session.fire_all_rules_fail_fast();
            else session.fire_all_rules();
        } catch (std::logic_error const&) {
            rejected = true;
        }
    }
};
```

分别为普通/strict 嵌套调用写独立 `it`。复用 `create_test_kb()` 和
`SessionTestFixture::make_person("John", 30)`，登记 listener，外层 fire 后断言：

```cpp
check_equal(session->fire_all_rules(), 1);
check_equal(listener->attempted, true);
check_equal(listener->rejected, true);
check_equal(session->get_fact_count(), size_t{2});
```

- [x] **Step 2: 验证真实 RED。**

```text
cmake --build --preset win-dev-user --target engine_session_test.RulesForge
ctest --preset win-dev-user -R ^engine_session_test\.RulesForge$ --output-on-failure
```

预期构建成功、`rejected` 期望 true 实际 false；若不是此原因，先修正测试条件。
保存完整日志，不以缺头/编译错误冒充行为 RED。

- [x] **Step 3: 实现最小 guard，并复测 GREEN。**

在 session 私有区添加 `bool execution_active_ = false;`。给两个 fire 声明和 reset
补充同 session 不可重入、caller-serialized 与 `std::logic_error` 注释。
`stateful_session.cpp` 显式包含 `<stdexcept>`；在共享 `fire_all_rules_impl` 的第一行
（在 consistency 检查、flush、halt 重置前）使用局部 scope：

```cpp
if (execution_active_) {
    throw std::logic_error("Cannot reenter rule execution on the same session");
}
struct ExecutionScope final {
    bool& active;
    explicit ExecutionScope(bool& flag) : active(flag) { active = true; }
    ~ExecutionScope() noexcept { active = false; }
    ExecutionScope(ExecutionScope const&) = delete;
    ExecutionScope& operator=(ExecutionScope const&) = delete;
} execution_scope(execution_active_);
```

同 Step 2 命令验证通过。标志只属于 session，不使用 global/thread_local。

- [x] **Step 4: 补齐独立边界测试，每项描述一个真实行为。**

1. 先添加 reset listener 在 before 回调里调用 reset，捕获 `logic_error`；拒绝后
   外层仍 fire 1，原 Person 与新 Adult 共 2 个 fact。如果 reset 没有被拒绝，listener
   必须立即抛测试专用 `ResetWasAccepted` 异常，外层测试捕获后断言拒绝标志为 true，
   不让旧实现继续执行 RHS 使用失效数据。先运行并保存该断言失败的真实 RED；
   之后才在 `reset()` 第一行添加下面的检查，并重跑该用例与 Step 2 测试。
2. after listener 也尝试嵌套 fire，必须拒绝，覆盖执行范围直到通知完成。
3. before listener 调用**另一**独立 session，内外各 fire 1，各有 2 个 fact。
4. 正常返回后 reset，插入新 Person 再 fire 1，验证成功路径释放准入。
5. before listener 首次抛 `std::runtime_error`，外层向调用方传播；移除 listener，
   reset 并插入新 Person 后 fire 1。此测试只要求 guard 清理，不暗示 agenda 自动重放。
6. strict 外层在同样异常后也可 reset/重新 fire，不能只测试普通入口。
7. 在既有 `blocks mutating apis after rollback failure until reset` 用例的
   `is_consistent()==false` 之后、reset 之前，分别调用两个 fire 入口并断言返回 0；
   保留原 reset 与后续恢复断言，验证 consistency early-return 也释放执行准入。

reset 和 after helper 必须在测试文件内；不向生产类添加仅供测试的查询方法。
生产 guard 删除、作用域提前结束、异常不释放、使用全局标志，均应至少破坏一项测试。

```cpp
if (execution_active_) {
    throw std::logic_error("Cannot reset a session during rule execution");
}
```

reset 测试专用 helper 的关键路径如下，类型只定义在测试文件：

```cpp
struct ResetWasAccepted {};
// before_rule_fired 内：
try {
    session.reset();
} catch (std::logic_error const&) {
    rejected = true;
    return;
}
throw ResetWasAccepted{};
```

- [x] **Step 5: 执行相邻回归与双配置验证。**

在 VS 环境依次运行：

```text
cmake --preset win-dev-user
cmake --build --preset win-dev-user --target engine_session_test.RulesForge rhs_control_flow_test.RulesForge capi_test
ctest --preset win-dev-user -R "^(engine_session_test\.RulesForge|rhs_control_flow_test\.RulesForge|capi_test)$" --output-on-failure
cmake --preset win-release-user
cmake --build --preset win-release-user --target engine_session_test.RulesForge rhs_control_flow_test.RulesForge capi_test
ctest --preset win-release-user -R "^(engine_session_test\.RulesForge|rhs_control_flow_test\.RulesForge|capi_test)$" --output-on-failure
```

共享 session 实现影响所有引擎消费者；合并前完整 build/CTest 两个 user preset，
如发现无关基线失败，报告证据并区分，不跳过或放松测试。此阶段不运行 install。

- [ ] **Step 6: 审查、提交并更新 #6，不关闭父任务。**

```text
git diff --check
git add rulesforge/include/engine/stateful_session.hpp rulesforge/src/engine/stateful_session.cpp rulesforge/test/engine/engine_session_test.cpp docs/superpowers/plans/2026-09-10-session-execution-admission.md
git commit -m "fix(engine): reject reentrant session execution and reset"
```

审查重点：guard 是否在所有可回调工作之前生效、异常与 early return 是否释放、
原 RHS 事务是否仍唯一负责回滚、是否误声称线程安全或完整预算能力。
最终交付单独列出 #6 未完成的检查点、C ABI 和资源配额，不以此修复替代 DLL 验收。
