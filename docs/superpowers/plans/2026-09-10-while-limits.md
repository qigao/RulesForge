# Exact WHILE Limits Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 删除 WHILE 无效上限 fallback，允许恰好 N 次完成，在 N+1 次循环体执行前失败。

**Architecture:** CompiledAction 保存次数配置；既有递归 action-plan builder 在 admission 拒绝非正值。RhsExecutor 保留运行期不变量检查，先判断条件再检查次数，原 RHS 事务仍独占提交和回滚。

**Tech Stack:** C++20、现有 TinyTest、版本化 CMake user presets。

**Spec:** [RulesForge #9](https://github.com/qigao/RulesForge/issues/9)，用户已确认其中的公开行为契约；父设计 [bounded-execution.md](../../architecture/bounded-execution.md)。本计划只覆盖 #9，不交付父 #6 的完整预算能力。

## Global Constraints

- N 必须为正数；无效值在执行计划 admission 拒绝，运行时边界也明确报错，不采用默认值补救。
- N 表示允许进入循环体的最大次数；恰好 N 次后条件为 false 正常成功。
- 条件仍为 true 时，在第 N+1 次循环体产生任何副作用前失败，并走原 RHS 回滚。
- 允许最后一次只读条件判定；这不等同于步骤预算或 CPU deadline。将来表达式内部仍须独立计费。
- break 与 continue 各保留既有控制语义；continue 不得逃过次数计数。
- 文本规则默认值保持 1000，不新增配置语法、C ABI 或依赖。
- 不修改 CMake/presets，不新增 fallback，不安装 SDK，不声称 #6 或 TurboFlow #73 已完成。

## 影响与风险

HIGH／已批准行为变化：精确上限由失败变成功；无效内部上限由默认补救变拒绝。文件格式和 C ABI 不变。既有正数循环逻辑和事务归属保留；失败后恢复原 fact，后续 RHS action 不执行。增加一次终止条件求值，不声称性能优化；每层循环次数 O(N)，不改变嵌套循环乘积复杂度。部署回滚用完整旧版本，不在新版本保留旧路径。

### Task 1: WHILE admission 与精确边界

**Files:**
- Modify: `rulesforge/src/engine/rhs_executor.cpp`
- Modify: `rulesforge/src/engine/cpp_rhs_action_plan_builder.cpp`
- Modify: `rulesforge/include/core/rhs_actions.hpp`
- Modify: `parser/src/rhs_parser.cpp`
- Modify: `rulesforge/test/engine/rhs_control_flow_test.cpp`
- Modify: `parser/tests/rhs_parser_test.cpp`（仅默认值归属测试确需调整时）
- Modify: `docs/architecture/bounded-execution.md`（记载已交付的局部边界）

**Interfaces:**
- Consumes: `CompiledAction::max_iterations`、`CppRhsActionPlanBuilder::build`、`RhsExecutor::execute`、`StatefulSession::fire_all_rules`。
- Produces: 已有入口的精确次数和拒绝语义，无新增公开执行方法。

- [x] **Step 1: 添加精确上限行为 RED。**

在现有 `group("while")` 复用真实 `build_session`、Counter/Result 规则与 fact setup。把测试输入 `limit` 设为 1000，断言不抛异常、count 与 Result.count 均为 1000；不能读取私有循环计数。规则固定为：

```text
declare Counter
 count: int
 limit: int
end
declare Result
 count: int
end
rule "Exact While Limit"
when
 $c : Counter(count == 0)
then
 while $c.count < $c.limit {
  update $c { count = $c.count + 1 }
 }
 insert Result { count = $c.count }
end
```

测试捕获 `std::runtime_error` 并断言 `threw == false`，避免异常遮蔽真正失败信息。期望旧实现构建成功，但该断言失败，实际错误含 `max iterations`。保存完整 build/test 日志。

- [x] **Step 2: 添加无效上限 admission RED。**

在同一测试文件 include `rhs_parser.hpp` 与 `engine/cpp_rhs_action_plan_builder.hpp`，使用真实 parser 构造 actions，再修改测试自己的 action：

```cpp
auto actions = rulesforge::RhsParser::parse("while 1 > 0 { break }", {});
check_equal(actions.size(), size_t{1});
actions.front().max_iterations = 0;
rulesforge::CppRhsActionScript out;
std::string reason;
check_equal(rulesforge::CppRhsActionPlanBuilder::build(actions, out, &reason), false);
check_equal(reason, std::string("while_limit_invalid"));
check_equal(out.root_actions.empty(), true);
check_equal(actions.front().max_iterations, 0);
```

负数使用独立用例。嵌套用例从 `if 1 > 0 { while 1 > 0 { break } }` 解析，修改 `then_actions.front().max_iterations`。先用合法输入成功填充 out，再构造“有效命令在前、无效 while 在后”的失败输入；断言 script 和所有 action/index 向量均为空，证明无半成品输出。旧实现应返回 true，得到行为 RED。

- [x] **Step 3: 最小实现 GREEN。**

builder 的 WHILE 分支在检查 condition 后、递归 body 前拒绝非正数，复用原有失败时清空 out 的路径：

```cpp
if (action.max_iterations <= 0) {
    if (reason_out) *reason_out = "while_limit_invalid";
    return false;
}
```

executor 的 WHILE 入口在求值前拒绝非正数，抛 `std::runtime_error("RHS while loop requires positive max iterations")`，删除三元补救。循环改为 `while (true)`，保留原条件求值与错误处理；false 直接 break，true 时在执行 body 前做：

```cpp
if (iterations >= action.max_iterations) {
    throw std::runtime_error("RHS while loop hit max iterations");
}
++iterations;
```

然后原样执行 body、处理 break/continue。删除末尾旧增量和循环外的上限报错。这样 continue 不绕过计数，INT_MAX 上限也不会增至溢出。默认 1000 在 CompiledAction 初始化处作为唯一来源命名，删除 parser 重复赋值；说明正数最大循环体次数语义，不更改结构布局。

- [x] **Step 4: 边界及事务回归。**

同一真实规则 fixture 验证实际循环次数 0、1、999、1000 成功，各有正确 Result；1001 与无限循环抛错、count 回滚为 0、Result 不存在、session 保持一致并可 reset/再执行。已有 break 测试保留；新增最后允许一次 break 正常结束和 continue 经过精确上限终止/超限拒绝的用例。不要通过提高上限让失败消失。

运行时 guard 要有直接真实 RhsExecutor 覆盖：使用真实空 KB/session 作为 INetworkCallback，真实 parser/actions/token/bindings，构造内部 command program 绕过 builder 来模拟不变量破坏；0/负值必须报 positive max iterations，不可误认无限循环耗尽为成功的拒绝。测试对象和 program 的借用生命周期必须覆盖 execute，不新增生产测试 API 或 const_cast 修改 KB。

- [x] **Step 5: 双配置验证与文档。**

在 VsDevCmd 环境依次运行：

```text
cmake --preset win-dev-user
cmake --build --preset win-dev-user --target rhs_control_flow_test.RulesForge rhs_parser_test.parser engine_session_test.RulesForge capi_test
ctest --preset win-dev-user -R "^(rhs_control_flow_test\.RulesForge|rhs_parser_test\.parser|engine_session_test\.RulesForge|capi_test)$" --output-on-failure
cmake --preset win-release-user
cmake --build --preset win-release-user
ctest --preset win-release-user --output-on-failure
cmake --build --preset win-dev-user
ctest --preset win-dev-user --output-on-failure
git diff --check
```

只勾选实际完成步骤；记录禁用测试与配置告警，不将其说成全绿无告警。同步架构文档：此限制不是表达式/匹配步骤预算，#6 仍开放。提交前独立审查，issue/PR 更新由主代理处理。
