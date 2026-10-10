# 有界执行与 session 生命周期

状态：执行准入保护已接入；完整有界执行仍处于设计阶段，未开放新的预算 API。
同步日期：2026-10-10。以下区分 RulesForge 的待实现设计、上游已公开契约和本地分支；
上游具备预算或 lease 不表示 RulesForge 已获得同样保障。
完整模块、类型、输入、连续处理与 SDK 迁移边界见[架构总览](overview.md)。
跟踪：[RulesForge #6](https://github.com/qigao/RulesForge/issues/6)、
[TurboFlow #73](https://github.com/qigao/turbo-flow/issues/73)。
执行准入前置单独跟踪于 [RulesForge #7](https://github.com/qigao/RulesForge/issues/7)。

## 背景与证据

事实基线：RulesForge `8b9127f148a3b54ad30c731bbc314ea74b15d976` 加当前工作区。
本文件同步待实现的执行设计；SDK 升级与构建验证单独记录在
[部署指南](../DEPLOYMENT.md#sdk-alignment)，没有开放新的有界执行接口。

上游引用已刷新，并获取所选发布 tag，用 `git show <commit>:<path>`
核对公开头文件、实现及测试；没有切换相邻 checkout 或合并其改动。

| 来源 | 核对版本 | 对本设计的含义 |
| --- | --- | --- |
| [Salts 2.3.0-rc.4](https://github.com/qigao/salts/releases/tag/v2.3.0-rc.4) | `233a0d1c2086808d85160ad70010e13b4c63d5c8`；CMake 项目版本 2.3.0 | CMeta Reflection ABI 4、Plugin ABI 5；SDK 回归与有界执行能力分别验证 |
| [TurboScript 上游](https://github.com/qigao/TurboScript/tree/a89f8a718639794644ad2a129add471080db9c03) | `a89f8a718639794644ad2a129add471080db9c03`；3.0.9 | 已公开 Host ABI v1；native ABI 3 与 Host ABI、Plugin ABI 分开 |
| 相邻 Salts checkout | `a650262031e9b3e5f11a227a7911a75db9ae1fb8`，另有未提交改动 | 不是上述上游版本，不可用其旧头文件替代新契约 |
| 相邻 TurboScript checkout | `f7014ca49a9db2fcaa8c4c5598286dfadb4c56ee`，另有指引文件改动 | CMeta/plugin 迁移准备分支；其私有 Host 声明和 module v2 open/close 方案不能代表上游 3.0.9 |

当前 RulesForge 证据：

- `include/rules_forge.h:550` 的 `ruleforge_session_fire_all_rules` 只有
  `max_rules`，没有原生步骤计费、取消或 deadline 参数。
- `StatefulSession::fire_all_rules_impl` 在执行前 flush 匹配，
  在 activation 外检查规则数，然后调用 RHS。
  限制触发数不能证明匹配或单个 RHS 的工作量有界。
- `StatefulSession::fire_activation` 调用 before/after listener；`reset`
  清空 agenda、facts、网络内存。共享 fire 入口现以 session-owned bool 和 RAII
  保护从首次检查、flush 到末次通知及退出的范围；嵌套 fire 或执行期 reset 在
  修改状态前抛出 `std::logic_error`。
- `rulesforge/src/engine/rhs_executor.cpp:494-533` 已有事务异常回滚路径；
  `StatefulSession::end_rhs_transaction` 判断回滚是否成功，失败标记 session 不一致。
- `capi/src/rules_forge.cpp:803` 的 wrapper 只有 session 和 active stream
  计数；destroy 的 stream 检查不等于在途执行或 owner-thread 检查。

事实：本次复用独立分支提交 `8576a5af4d71ed347c513933ceae7f67695eedeb` 的执行准入
实现与真实 listener/session 测试；原分支和用户已有改动保持原样。测试覆盖两种 fire
入口、执行期 reset、after 通知、独立 session、正常/异常退出及不一致状态提前返回。
实现与集成记录见[第一阶段计划](../superpowers/plans/2026-09-10-session-execution-admission.md)。
HIGH / 剩余边界：该 bool 仅适用于 caller-serialized session，不保护跨线程访问、
执行期销毁或其他尚未约束的生命周期操作，也不提供工作量预算。

## 与最新 Salts / TurboScript 的边界

### 已有能力与依赖方向

事实：RulesForge 当前 C ABI 只使用 `include/rules_forge.h`，内部 C++ 头文件不安装。
`rulesforge/CMakeLists.txt` 与 `capi/CMakeLists.txt` 消费 Core、CSTL、DataBind 等既有
target，没有接入 TurboScript 或 Salts Plugin。保留这一依赖方向：RulesForge 拥有
facts、agenda、RETE 和 RHS 事务；宿主适配器拥有脚本 context、调用映射及 DLL lease。
同步设计不新增脚本 RHS、原生 callback、插件 loader 或新的调度线程。

Salts 的 [CMeta ABI](https://github.com/qigao/salts/blob/233a0d1c2086808d85160ad70010e13b4c63d5c8/cmeta/include/cmeta/abi.h)
要求跨 DSO 先协商 Reflection epoch，再解释 descriptor。
[Plugin](https://github.com/qigao/salts/blob/233a0d1c2086808d85160ad70010e13b4c63d5c8/plugin/include/salts/plugin.h)
使用 `cmeta_plugin_*`、`CMETA_PLUGIN_*` 和 `cmeta_plugin_query`，头路径仍为
`<salts/plugin.h>`。发布层用 `Salts::PluginABI`，宿主 registry 用 `Salts::Plugin`；
Function/Interface 的签名、ownership、effects 归 canonical CMeta 描述符。
这些元数据不证明某个 native 函数具有执行检查点或可回滚副作用。

静态能力声明现为
[`<cmeta/component.h>`](https://github.com/qigao/salts/blob/233a0d1c2086808d85160ad70010e13b4c63d5c8/cmeta/include/cmeta/component.h)
的 `cmeta_component`，不能再把旧静态 `cmeta_plugin` 当作最新接口。
静态 Component 元数据不持有实例或 lease，不替代 Plugin 的动态模块生命周期。

### TurboScript Host ABI 与执行预算

事实：[上游安装头 `turbo_script.h`](https://github.com/qigao/TurboScript/blob/a89f8a718639794644ad2a129add471080db9c03/turbo_script/include/turbo_script.h)
已公开 `TURBO_SCRIPT_HOST_ABI_VERSION == 1`、opaque module/instance/result、
export resolve/call 和 `turbo_script_call_options_t`。内部
`turbo_script_host_api_internal.h` 只是包含该公开头的兼容入口；不应继续描述为
“Host ABI 尚未公开”。instance 创建时固定 interpreter/JIT 模式，不允许自动切换。

Host 操作同步运行在 context owner 线程；参数和 callback view 只借用于接收调用。
result getter 的 view 在 result reset、重写或 destroy 后失效；跨调用保留时必须复制
到接收方拥有的存储。instance、module、result 在 context 释放前销毁，export handle
仅属于解析它的 instance，不能当作 native 地址或另一 instance 的句柄。

[预算实现](https://github.com/qigao/TurboScript/blob/a89f8a718639794644ad2a129add471080db9c03/turbo_script/src/host/turbo_script_host_limits.c)
已有每次调用独立的 steps、loop iterations、host callbacks、recursion、stack 和
result/retained bytes 计量，以及首个失败原因保留。interrupt 在 owner 线程的函数入口、
循环、callback 前后和步骤检查点观察；步骤轮询间隔为 256。上游还将调用上限限制在
自身硬边界内，零调用限额在准入时拒绝，不能解释为无限或直接照搬成 RulesForge 的数值约定。

HIGH / 设计约束：这些检查点仅约束 TurboScript 覆盖的执行路径。一次 host callback
计费不能约束内部执行的 RETE 匹配或 RHS；callback 返回后检测取消也无法撤销已经发生
的副作用。未来适配器可把同一个取消请求和单调 deadline 接到各 runtime 的观察入口，
但不得复制未计费的剩余额度给多个独立调用。Host ABI v1 没有公开逐检查点的外部步骤
扣款接口，也不返回完整步骤用量；跨引擎统一步骤预算仍需单独定义预留/结算契约，
未实现时只可声明分别计量的限额，不能承诺端到端统一 fuel。

现有公开 call options 没有独立 deadline 字段；宿主可在 interrupt probe 中检查单调
deadline 并保留首个停止原因。`INTERRUPTED` 本身不能区分取消和超时，
`LIMIT_EXCEEDED` 也不直接区分每种配额。适配器必须保留原 status/phase/error 信息，
不能猜测错误类别后改报成功。probe 不调用脚本、不执行 I/O，也不重入同一 session。

### 插件 publication 与关闭

事实：上游 [TurboScript `ts_plugin.h`](https://github.com/qigao/TurboScript/blob/a89f8a718639794644ad2a129add471080db9c03/exprtk/include/ts_plugin.h)
使用 `turboscript.module` contract v1 Interface，包含 `load` 和 void `unload`；
[loader](https://github.com/qigao/TurboScript/blob/a89f8a718639794644ad2a129add471080db9c03/turbo_script/include/ts_plugin_loader.h)
还支持已绑定 canonical Function，并以 registry lease 保活 descriptor、代码和 context。
这是本设计的消费基线。相邻准备分支中的 `qigao.turboscript.module` v2、
`TS_PLUGIN_STATEFUL_CHECKED` 和 `ts_plugin_unload_ex()` 不能作为上游可调用接口。

未来 RulesForge 插件适配器复用宿主 registry，不在 engine 内建立第二套 loader 或
lease count。关闭时先拒绝新业务调用、请求协作停止并等待在途工作完成；依赖 DLL
代码的 callback、绑定、session 对象和数据清理必须处于有效 lease 下。随后释放 lease，
完成 stop/quiescence/unload；复制 Function binding 或 descriptor 不会延长模块寿命。
Salts 的 request-stop、lease 和 quiescence 各自保留原契约，不能把 stop 当作 drain。

HIGH / 设计门槛：需要可重试关闭的适配器不得把失败压入上游 void `unload` 并继续
释放 owner。必须先在宿主拥有的、可返回状态的边界完成 drain，或另行交付有明确失败
义务的适配契约；未完成前不发布该能力。本设计不声称上游 TurboScript loader 已具备
本地准备分支的失败保留/重试语义。

## 选择与代价

不采用插件外计时、整次执行计为一步、强杀线程或调用后检查预算。这些方案不能
在副作用前拒绝工作。也不创建新的规则解释器或第二套 agenda。

采用现有分层：C ABI 校验与 owner admission → session-owned 执行控制 →
RETE/表达式/RHS 的显式检查点 → 既有事务收尾。C++ RAII 只负责撤销调用期借用与
执行标志，业务回滚继续归原事务 owner。失败不能被现有异常捕获转换为执行成功。

影响面是 C ABI、session、RETE、RHS/表达式和安装消费者；新增检查点有运行成本，
需实测，不能将安全改动宣称为性能优化。预算控制不改变 RETE 选取算法。

## 状态与错误归属

一个 session 在一个 owner 上串行执行；本设计不把 session 变成 thread-safe。
控制面状态为 idle/executing；预算状态仅属于一次调用，不跨调用自动复位复用。
嵌套执行和执行期 reset 在任何状态修改前失败。正常返回与异常退出都必须恢复
idle。不同 session 的嵌套调用不应被进程全局标志错误拒绝。

这里的“不同 session”指独立 RulesForge session。TurboScript 的
[Host admission](https://github.com/qigao/TurboScript/blob/a89f8a718639794644ad2a129add471080db9c03/turbo_script/src/host/turbo_script_host_instance.c)
还检查 context 的 active instance 和 callback depth，因此同一 context 下另一个
instance 的重入也可能被拒绝；适配器不能因 RulesForge 允许独立 session 嵌套就绕过它。

后续 C ABI 使用独立的终态结果区分完成、规则预算耗尽、步骤预算耗尽、取消、
deadline、执行错误。终态只提交一次；fired count 计已完成的规则，不计开始后失败
的规则。前置校验失败不修改 session，也不读取未验证的短结构尾部。

预算使用 checked subtraction：仅在 `requested <= limit - used` 时预留步骤，
失败不增加 used，并粘住第一个停止原因。不能把 `used + requested` 的溢出当成功。
所有执行层共享同一控制对象，不各自累计可独立推进的剩余额度。

取消来源可跨线程发出请求，但不能跨线程访问 session。观察点在 owner 线程读取
取消状态；请求不是完成通知。deadline 是单调时钟上的协作边界，不承诺硬实时。
未验证观察点的后端、原生回调或表达式在 admission 前明确拒绝相应保障要求。

中断当前 RHS 后先完成既有回滚，再返回终态。回滚失败时保留不一致状态，只允许
既有恢复/销毁协议；不得继续 fire。此前已提交的规则副作用不自动回滚；结果必须
明确这一范围。未支持可回滚语义的外部副作用不能进入有界 profile。

TurboScript 的执行失败可能使 instance 进入 Faulted，后续 call 被拒绝；前置校验失败
与执行后失败不能合并处理。RulesForge reset 或 RHS 回滚不会复原脚本 globals、解除
Faulted 或撤销 host callback 的外部副作用。若未来引入脚本参与事务，必须另行定义
快照/命令提交边界与 instance 重建策略，通过验证后才可宣称跨 runtime 原子性。

continuous session 的 `DRAIN_REQUIRED` 是规则触发预算后的继续执行状态，acknowledge
释放 pending-output accounting；两者都不是本设计的取消完成或执行终态。其 retained
events/results/replay 数量上限也不证明 CPU 步骤或 allocator 硬上限。保留
[现有 continuous 契约](../CONTINUOUS_RULE_ENGINE_DESIGN.md)，未来中断与 replay 恢复
分别预留有界清理资源，不因主执行预算耗尽而跳过必要的回滚或保活。

## 分阶段交付

1. **执行准入基础（已接入）**：同 session 的嵌套 fire 和执行期 reset 明确失败；RAII 保证
   退出释放准入。该步骤不宣称提供取消、owner-thread、在途 destroy 或预算保证。
2. **内部检查点与准入证明**：同一控制对象贯穿 deferred matching、RETE 节点、
   expression 和 RHS 的循环/命令；先覆盖支持的明确后端集合，再开放 profile。
   特别检查输入插入阶段的匹配，不能只覆盖 fire 后的工作。对第三方解析/正则等
   原生调用说明最大不可中断范围；不能证明的保障明确 unsupported。
3. **公开 C 契约与插件消费**：精确 size/version、owner/reentrancy admission、
   budget callback 生命周期、在途 reset/destroy、终态与错误诊断；安装 C 消费者
   通过之后，再让 TurboFlow DLL 或其他宿主适配器声明相应能力。若消费 TurboScript，
   使用已公开的 Host ABI 和固定 execution mode，先验证其重入、Faulted、view 生命周期
   及配额映射；统一步骤结算和可重试关闭仍是独立准入门槛。
4. **资源配额**：fact/activation/result/内存分别说明计量口径、上限和失败原子性；
   逻辑计数与 allocator 硬上限不能混称。不完整的硬配额要求必须拒绝。

上述是交付顺序，不是允许公开部分实现的借口。每阶段只公开已实现的行为；#6
保持开放直到全项验收。旧 API 不作为新有界 API 的兼容转发或失败 fallback；
旧消费者的迁移与最终移除另随调用点一并验证，不能在本前置修复中批量删除能力。

## 验证与迁移

第一阶段测试使用真实 parser、KB、session、listener：同 session 两种 fire 入口
均拒绝嵌套；reset 不清空外层输入；不同 session 仍能嵌套；正常与异常退出后能再次
执行，且输出 fact 数量正确。异常恢复测试不应假设原先已弹出的 activation 自动
重入队列，应 reset 并重新插入独立输入验证 guard 已释放。

后续阶段增加 0/1/N/N+1、溢出、matching/RHS 中断、取消竞争、回滚失败、预算停止
后无更多步骤，以及真实安装 C 消费者测试。Debug/ASan、Release 各自使用版本化
user preset；不覆盖外部 SDK，直到上游验收完成且进入明确安装步骤。

迁移成本是对检查点传播与拒绝范围逐层审查，不是只新增一个 C 函数。新有界请求
失败必须保留原 owner 的可诊断状态；回滚部署使用完整的已验证版本，不能在新
版本内自动选择旧执行路径。第一阶段不改文件格式、网络协议或安装布局。

### 上游同步后的验收补充

- RulesForge 正式回归沿用 `engine_session_test.RulesForge`、
  `rhs_control_flow_test.RulesForge`、`capi_test`；加入真实 matching/RHS 中断以及
  continuous drain/ack/replay 的兼容性验证，不只测适配层返回码。
- TurboScript 可复用的上游证据入口为
  [`test_turbo_script_host_limits.c`](https://github.com/qigao/TurboScript/blob/a89f8a718639794644ad2a129add471080db9c03/turbo_script/test/test_turbo_script_host_limits.c)
  和 [`test_turbo_script_host_state.c`](https://github.com/qigao/TurboScript/blob/a89f8a718639794644ad2a129add471080db9c03/turbo_script/test/test_turbo_script_host_state.c)：
  interpreter/JIT 边界、零限额拒绝、硬限额、callback 前后 interrupt、Faulted 与同 context
  多 instance 重入。它们不替代 RulesForge adapter 的真实跨引擎测试。
- 安装边界参考上游
  [`cmake/ci/host-abi`](https://github.com/qigao/TurboScript/tree/a89f8a718639794644ad2a129add471080db9c03/cmake/ci/host-abi)
  的 C/C++ 消费者；只使用安装头和导出 target。分别验证错误 ABI/contract 拒绝、在途
  停止、借用输出复制、lease 覆盖清理及失败后的 owner 保留。
- SDK 更新单独记录在[部署指南](../DEPLOYMENT.md#sdk-alignment)：当前 Windows Release
  使用 Salts rc.4 / SaltsUtils rc.2。Salts 2.3 改变 native ABI/SONAME，TurboScript 3.0.9
  native ABI 为 3；不能把包版本相近当成布局兼容。按相同平台、配置和 ABI 重建
  host/plugin/依赖完整集合；回滚也恢复该集合，不混用新头文件与旧 DLL。

本次集成重新构建了正式 session/RHS/C API targets，相邻回归 3/3 通过。
完整回归结果记录于[架构总览](overview.md#版本与事实源)。当前 Debug SDK 仍为 Salts
2.2.0 / SaltsUtils 4.1.21，不满足新基线，因此本次没有运行 Debug/ASan；也未运行
安装消费者或可选 runtime 的测试。不沿用原分支或其他提交的通过记录作为本次证明。
