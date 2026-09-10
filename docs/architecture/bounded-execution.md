# 有界执行与 session 生命周期

状态：设计阶段，未开放新的有界执行能力。
跟踪：[RulesForge #6](https://github.com/qigao/RulesForge/issues/6)、
[TurboFlow #73](https://github.com/qigao/turbo-flow/issues/73)。
执行准入前置单独跟踪于 [RulesForge #7](https://github.com/qigao/RulesForge/issues/7)。

## 背景与证据

事实基线：`809048ae11e54eb7a10952af19f3639d3d135d20`。

- `include/rules_forge.h:550` 的 `ruleforge_session_fire_all_rules` 只有
  `max_rules`，没有原生步骤计费、取消或 deadline 参数。
- `rulesforge/src/engine/stateful_session.cpp:426` 在执行前 flush 匹配；
  `:456`、`:482` 在 activation 外检查规则数，`:516` 调用 RHS。
  限制触发数不能证明匹配或单个 RHS 的工作量有界。
- `stateful_session.cpp:544`、`:609` 调用 listener；`:1210` 的 reset
  清空 agenda、facts、网络内存。当前入口没有执行期重入保护。
- `rulesforge/src/engine/rhs_executor.cpp:494-533` 已有事务异常回滚路径；
  `stateful_session.cpp:949-1015` 判断回滚是否成功，失败标记 session 不一致。
- `capi/src/rules_forge.cpp:803` 的 wrapper 只有 session 和 active stream
  计数；destroy 的 stream 检查不等于在途执行或 owner-thread 检查。

HIGH / 推论：listener 中嵌套 fire/reset 可能改变外层调用正在使用的状态；
在引入可由宿主调用的预算观察点前，必须先防止这种重入。复现须使用真实
listener/session 测试，不用 mock 引擎或仅检查成员变量。

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

## 分阶段交付

1. **执行准入基础**：同 session 的嵌套 fire 和执行期 reset 明确失败；RAII 保证
   退出释放准入。该步骤不宣称提供取消、owner-thread、在途 destroy 或预算保证。
2. **内部检查点与准入证明**：同一控制对象贯穿 deferred matching、RETE 节点、
   expression 和 RHS 的循环/命令；先覆盖支持的明确后端集合，再开放 profile。
   特别检查输入插入阶段的匹配，不能只覆盖 fire 后的工作。对第三方解析/正则等
   原生调用说明最大不可中断范围；不能证明的保障明确 unsupported。
3. **公开 C 契约与插件消费**：精确 size/version、owner/reentrancy admission、
   budget callback 生命周期、在途 reset/destroy、终态与错误诊断；安装 C 消费者
   通过之后，再让 TurboFlow DLL 声明相应能力。
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
