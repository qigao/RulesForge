# RulesForge 拆分 SDK 迁移记录

## 2026-11-14：最新正式 SDK 与共享 vcpkg-cache

- **授权与工作区**：用户要求在 `C:/projects/cpp/turbonet/rulesforge` 继续使用
  最新已发布依赖和共享 cache。原工作区已有 core/parser/C API 重组及大量暂存改动，
  全部保留；本轮未执行 git add/commit/push，不发布、不删除 RulesForge 的
  历史 releases/packages。前一项目 Toad 的发布/清理授权不外推到本仓库。
- **发布版本事实**：逐项查询最新正式 GitHub Release，选定 vcpkg-cache 0.1.3、
  SaltsBase 0.1.8、CMeta 0.1.7、SaltsUtils 0.1.4、CNet 0.1.12。共享基础设施
  checkout 为干净 v0.1.3。`.env.example` 和 RulesForge 五个同名 CI variables 已同步。
  忽略的 `.env` 仅更新对应构建字段，保留凭据及其他机器配置。
- **共享根与所有权**：本机直接复用前一项目已资格化的
  `TurboScript/build/dependencies/vcpkg-cache-0.1.3-qigao-x64-windows-llvm-mingw/payload/vcpkg_installed`
  及匹配的 Base/CMeta/Utils/CNet、宿主 Lemon/re2c 安装树。Roots 是用户配置输入，
  接受前核对正式 release identity、SHA-256 和共享恢复回执；本轮没有改写这些安装树。
  没有设置共享根时，恢复入口选择本仓库 build/dependencies 下的版本独立目录。
- **恢复入口**：`restore-native-sdks.ps1` 改为调用消费者的精确发布编排脚本。
  下载使用 vcpkg-cache 原有 `verified_download`；cache 采用共享 `resolve-release`
  产生 URL/hash 锁再 `locked` 恢复；宿主工具按精确 nupkg/hash 走同一个 locked helper。
  Base/CMeta 清单校验发布版本、平台、triplet、成功 producer CI、大小、摘要及依赖约束，
  SDK 按原始 TAR 安全解包。旧 floating Salts.Native/SaltsUtils.Native 路径已移除，
  不构建上游源码、不覆盖旧 SDK、不设失败后的依赖来源切换。
- **共享回执排障（MED，事实）**：共享 helper 的 receipt 包含 required_files。
  首次为 cache 改传 consumer-specific cxxopts 检查，与已有 zstd canonical receipt
  不同，因此按契约拒绝复用。恢复编排现沿用同一 release payload 的 canonical
  zstd admission；实际 cxxopts 必需查找仍由本项目 CMake 执行。没有改旧 receipt、
  重装 cache 或用空路径绕过校验。再次实际恢复通过。
- **构建归属**：根使用 SaltsBase/CMeta/SaltsCNet/SaltsUtils 的公开包；CSTL target
  改为 CMeta::CSTL，preinstalled triplet 加入 prefix，不运行 vcpkg manifest toolchain。
  原生/交叉生成器都必需来自 RE2C_ROOT、LEMON_ROOT，template 来自宿主 SDK；
  grammar、工具、template 都显式进入生成依赖。未改用户已有 presets 或 CmakeUtils。
- **目录迁移修复**：core/include、core/parser/include、rulesforge/include 为对应源码
  owner。修正旧 capi/include/source 路径、parser 的非法 target 名和 data 目录、
  根未装配的 CMake helper、失效的 binary_splitters 源条目、损坏的 package include。
  原有 parser/test/C API 测试仍由各自模块组装，BUILD_TESTS 统一控制；benchmark
  由 BUILD_BENCHMARK 控制并通过 CTest 注册，不增加 benchmark 子集/诊断开关。
  模块和现有示例无条件组装，未恢复用户删除的独立 package_consumer 工程。
- **代码修复（HIGH，事实）**：批量路径替换曾损坏 grammar 的 `%include`、`#include`
  和 `parser.error()` 表达式，均按语法及实际调用修正。Base 的 object_pool、tlog、fmt、
  platform 头文件改用其安装后的公开路径，保留原算法、TokenPool 容量及关闭协议。
- **UUID 边界**：已移除的 CMeta UUID 改为内部 UuidValue（16 字节 std::array）；
  variant 拥有值，调用 Base UUID API 时只借用 data()，没有堆所有权或跨线程状态变化。
  复制、哈希、字典序及标准字符串语义由现有 value_types/C ABI/DataBind 回归验证。
  没有增加旧 CMeta UUID shim，公开 C API 形状和 DataBind ABI 10 保持一致。
  C++ TinyTest 的三参内存断言改用 memcmp 的二参结果断言，保留实际字节验证。
- **版本与 CI**：按工作区已有根 CMake 0.1.0，统一 public version macros、vcpkg、
  NuGet 元数据；这是源配置一致性修复，本轮未发布 RulesForge 0.1.0。NuGet 不再声明
  旧聚合 SDK 的浮动 PackageReference。主 workflow 使用完整图、正式 CTest、install
  和同一精确恢复入口；原 release-sdk workflow 作为手动 reusable 调用，移除失效
  matrix/preset/独立 consumer 工程引用。Windows compiler root 在 CI 输入边界统一 `/`。
- **可重复验证**：同一个 PowerShell 内先令父环境 GITHUB_TOKEN 来自已登录 gh，执行
  `./cmake/ci/restore-native-sdks.ps1 -Rid windows-x64 -Profile windows-llvm-mingw-x64 -Local`，
  然后 `cmake --fresh --preset native-release-user`、完整 `cmake --build --preset native-release-user`、
  `ctest --preset native-release-user --output-on-failure --no-tests=error --timeout 60`、
  `cmake --build --preset install-native-release-user`。实际全部通过，CTest 为 **38/38**。
  安装包的公开头文件、DLL/import lib、RulesForgeTargets 和版本配置已生成；配置依赖
  固定 Base 0.1.8、CMeta 0.1.7、Utils 0.1.4。actionlint、版本一致性和脚本语法检查通过，
  CodeGraph sync 通过。日志在忽略目录 build/sdk-migration/。
- **验证范围**：仅实际运行本机 Clang 23.1.4 / LLVM-MinGW 20261020 UCRT / Release。
  没有运行其他平台、远端 RulesForge CI、sanitizer、benchmark 或 NuGet 打包；workflow
  actionlint 通过不等于这些运行时验证通过。后续需在相应 runner 资格化。

## 2026-11-15：Toad RHS Program 与 ACE generation

- **范围与授权**：用户授权实现 RulesForge 的脚本计算方案，并明确要求“修复并发布
  Toad，再完成 RulesForge”。Toad 上游已提交、推送并正式发布；RulesForge 本轮未执行
  add/commit/push，保留用户已有暂存与工作区改动。未修改当前 presets、CmakeUtils 或
  ProjectOptions，未增加功能开关、独立验证工程或新的依赖恢复框架。
- **正式 SDK 事实**：消费 Toad **0.1.2** 原始发布 SDK，源码提交
  `a1c99501b92627ec5671ea30416d42145a9edce7`，成功 producer **43620027243**。
  四平台 CI、打包与发布通过；六个公开资产的大小、GitHub SHA-256、SHA256SUMS、
  四平台 manifest 与 NuGet 内全部 SDK 文件逐字节核对通过。审计记录位于
  `TurboScript/build/release-0.1.2/`。恢复入口校验其 Base/CMeta/Utils/CNet/cache
  组合，没有替换为本地 HEAD SDK。本机依旧共享前述 Toad 工程的 cache/Base/CMeta/
  Utils/CNet/宿主工具安装树；vcpkg-cache checkout 仍为干净 v0.1.3。
- **上游竞态（HIGH，事实）**：0.1.0 的两个独立 owner 线程初始化 Script 时，
  LLDB 栈定位 `toad_registry_init` 中 qsort/strcmp 与另一个线程的 realloc 竞争。
  修复为 builtin 注册 sync_once、registry 变更与查找同步，并在解锁前复制条目；
  调用用户函数时不持 registry 锁。新增正式并发测试覆盖独立解释器/JIT context。
- **计算权限边界（HIGH，事实）**：bare 初始化和 native plugin authorizer 不能阻止
  ambient builtin。Toad 新增保持旧默认行为与结构布局的 EXPLICIT_CALLS 编译标志，
  在 lowering 前仅接纳已声明命名函数、list 和预注册 host capability；RulesForge
  provider 使用该标志、拒绝 native 插件且不注册应用回调。文件/进程/I/O/import/eval
  等不进入允许调用集合。这不是进程沙箱，编译与 native 操作也没有硬超时保证。
- **发布失败处理（MED，事实）**：0.1.1 producer 因缺少版本发行说明在 publish 阶段
  失败，未当作可消费发布使用；补齐说明后新发布 0.1.2，没有移动旧 tag。仅撤回本次
  失败先行上传的 NuGet 0.1.1，保留 0.1.0 和源码历史；最终 0.1.2 资产与包核对通过。
- **模块与所有权**：Program 将 RFL/schema/script 快照和 binding catalog 交给 ACE
  ServiceHost generation；root ObjectRef 真实拥有 KB，先销毁 dependent 对象再释放
  provider lease。每个 session 固定 owner 线程，独立 Toad context，每次 CALL 新实例。
  Typed view 仅借用到返回，结果在释放实例前验证并深拷贝。保留现有 Fact/
  ConstraintValue 与事务；没有把 Facts 暴露为可变脚本指针，也没有 JSON 数据往返。
- **用户行为**：RHS 支持 `call "binding"(...) into $result`；binding/arity/字面量
  类型在编译时检查，字段与运行结果在调用边界检查。局部结果只读且遵循块作用域。
  类型错误、脚本错误和配额触发原 RHS 回滚。配置、文本、展开后的 schema、值和
  整个 RHS 累计复制结果均有界；循环、步骤、递归与会话配额显式配置。
  热更新完整验证后发布，旧 session 保留旧 generation；最多两代，第三代返回
  RESOURCE_LIMIT。close 撤销新 admission，已有 session 可继续并正常释放。
  当前仅实现 stateful Program；连续会话、异步计算和外部副作用回调未暴露。
- **原生加载排障（MED，事实）**：单纯在测试 executable 的 POST_BUILD 复制 provider
  会遗漏仅 provider 重编译后的 fixture 更新。现改为依赖真实 module 文件的生成输出，
  正式测试始终使用当前 provider。RulesForge 本身引用选定 Script runtime 并保持其加载，
  generation lease 再固定 adapter。安装不复制外部 SDK；部署需匹配的运行时库。
- **API 使用排障（MED，事实）**：DataBind 单例嵌套配置使用 composite，不能以 message
  代替。Toad 内存策略要求 max_external <= max_task <= max_context；task 上限取
  retained_bytes，实例 stack_bytes 独立设置，不能用 stack 配额代替 task 配额。
- **复现与结果**：使用前节同一恢复入口（TOAD_VERSION=0.1.2），从生成的
  `build/native-sdk-environment.json` 加载选定 roots 到父环境后，执行
  `cmake --fresh --preset native-release-user`、
  `cmake --build --preset native-release-user --parallel 3`、
  `ctest --preset native-release-user --output-on-failure --no-tests=error --timeout 60`、
  `ctest --preset native-release-user -R '^script_program_test$' --repeat until-fail:20 --output-on-failure`、
  `cmake --build --preset install-native-release-user`。完整 **43/43**、Program 重复
  **20/20**、安装均通过，日志在忽略目录 `build/toad-*.log`。回归包含并发初始化与
  跨代发布、配额、失败候选、事务回滚、类型/所有权、schema 展开上限及示例执行。
  安装 DLL 确认导出五个 Program API，依赖选定 Script runtime；provider 和精确
  Toad CMake dependency 随正常安装生成。actionlint、Python/PowerShell 语法、文档
  链接、diff 与 CodeGraph 检查通过。
- **验证限制**：RulesForge 实测为本机 LLVM-MinGW Release；没有运行 RulesForge 的
  其他平台 CI、sanitizer、benchmark 或 NuGet 打包。Toad 四平台 CI 成功不能替代这些
  消费者验证；Android 是交叉构建/安装验收，不代表设备运行通过。

## 2026-11-15：文档与示例审查修复

- **HIGH / JSON 失败语义**：`capi_demo` 曾在未知输入类型、缺失数组、非对象记录或
  插入失败后跳过输入并返回成功。现于首次错误停止，返回非零，且不对部分加载的数据
  fire；空数组仍是合法输入。KB/session/query 由一个局部 owner 按依赖顺序清理，
  异常也走同一归属；销毁失败不重试、不继续销毁尚有 dependent 的 owner。
- **MED / schema 唯一来源**：示例 CLI 的 `-s` / `--schema` 只读取而不参与加载。
  已删除该失效参数并同步帮助、文档和正式测试命令；使用旧参数会明确失败。JSON/CSV
  均只使用 RFL import 的 schema。外部类型迁移时需移动原 declare，不能保留重复类型。
- **MED / 规则包语法**：贷款、保险、履约、旅行四组 RHS 的 `:` 改为 `=`；复合值使用
  当前解析器接受的括号表达式，负数用减法表达，RHS 累加标量直接引用而不调用旧
  `.intValue()` / `.doubleValue()`。未改变引擎语法或加入兼容后端。
- **MED / 模块化示例**：文件名改为 `validation_rules.rfl`、`pricing_rules.rfl`；
  import 与现有相对目录解析一致，文件共享 ecommerce.common 命名空间。删除损坏的
  C++ 文档片段，改链到正式编译的回归；Gold、五件、subtotal=250 的最终价格断言为
  212.50。新增 PricingResults 查询用于观察输出。
- **MED / 预处理入口**：提供 `rulesforge/examples/flatten_example_data.py`，仅使用
  Python 标准库，覆盖 fraud/order/travel。保留原始记录与场景顺序，订单明细继承
  orderId、冲突则失败。校验和序列化成功后才写输出；拒绝输入输出同路径。
  文档将生成数据写入 build，未覆盖仓库原样本。
- **文档事实源**：修正旧 capi 路径、无效 include、旧 build/bin 命令和本地链接；
  示例说明当前 preset、已删除的 schema 参数、Toad 的运行入口及真实覆盖范围。
  DataBind/部署指南明确固定线程，不把串行 executor 当作线程归属保证。
- **正式回归与复现**：现有 core/test 增加 documented_examples_test.engine（六个
  case，含规则包编译与模块化真实执行）；rulesforge/tests 增加 examples_cli_and_data
  （十四个 CLI/预处理 case），由 CTest 调用标准库 unittest。测试所需宿主 Python
  由 find_package REQUIRED 选择，未增加功能开关。加载已恢复的 SDK 环境后执行
  `cmake --preset native-release-user`、
  `cmake --build --preset native-release-user --parallel 3`、
  `ctest --preset native-release-user --output-on-failure --no-tests=error --timeout 60`、
  `cmake --build --preset install-native-release-user`：完整 **45/45** 和安装通过。
  日志在 `build/docs-fix-*.log`。三种预处理 CLI 及文档中的 fraud runner 命令实际
  执行成功；文档本地链接与代码块、diff 检查通过。
- **范围限制**：未修改 presets 或 CmakeUtils，未提交/推送本仓库，保留已有工作区。
  实测仍为本机 LLVM-MinGW Release。领域样本的外部 schema 迁移及完整业务结果尚未
  资格化；预处理、RFL 编译和程序成功退出不代表其示意业务断言全部通过。

## 2026-11-15：Drools CEP 启发的复杂窗口＋Toad 示例

- **交付**：`rulesforge/examples/maintenance` 提供完整 schema/RFL/两个脚本/bundle
  模板与设计说明，maintenance_demo 使用公开 C ABI 输出八步决策轨迹。参考 Drools
  官方传感器告警与 sliding-window/accumulate 概念，未复制 Java 代码或引入 Drools
  依赖。继续使用发布的 Toad 0.1.2 和既有共享 SDK/cache，未修改 SDK、presets 或选项。
- **职责与边界**：DataBind 绑定四点波形和固定 Model；Toad signal 计算方差、粗糙度、
  峰度与风险，decision 比较许可约束下的预期成本；RETE 维护 press-7 最近四条观测、
  count/average/max、关联及后续 ActionRequest。实例每次调用重新创建，所有 Fact
  状态归当前 owner-thread session。主示例仅八条主设备输入＋一条其他设备输入，
  结束后销毁 session；窗口容量不等于全会话事实/输出保留上限。
- **schema 排障（MED，事实）**：选定 SDK 的 composite 只允许固定大小字段；把含
  list 的 message 嵌入 Reading 也不能通过当前 JSON 原生绑定。示例在设计准入阶段
  改为 `composite Waveform { double[4] values; }`，schema 明确约束输入容量，不复制
  SDK 实现、禁用校验或改用其他解析后端。原错误保留在忽略的构建日志中。
- **结果约束缺失（HIGH，事实）**：真实联合测试发现 `Total(result >= 3) from
  accumulate(...)` 的结果条件此前完全未参与传播，暖机时也可激活决策。Accumulate
  现复制并保留结果 AND/OR 树，复用现有字段解析/typed predicates；每个 left item
  记录 result_propagated，更新和父 token 撤销先清除实际发布过的下游匹配。
  合成结果 Fact 的生命周期仍归原 session，不增加公开 API 或并行执行后端。
- **IN 缺失（HIGH，事实）**：动作派生测试发现静态 IN 的 literal list 曾被当作
  nil RHS 比较，导致 ActionRequest 恒为空。现用既有标量 EQ 语义比较列表元素，
  保留 IN/NOT IN；alpha、join 和 derived-result 路径共享该实现。测试覆盖 source
  MODIFY、聚合结果 membership 及字符串动作派生。
- **相关时间表达式（HIGH，事实）**：修复结果约束后，原 fraud_example_test 的
  ATM 断言暴露 `$ts - 1800000` 被错划为 alpha、丢失前序绑定字段。现将未加引号
  的 `$binding` 算术交给 beta，并向 Accumulate 传递既有 inline 字段映射；不改
  原反欺诈业务断言。新增严格下界、前序绑定及结果算术回归，原测试再次通过。
- **复杂回归**：maintenance_program_test 的八个 case 覆盖每步精确窗口聚合、六条
  其他设备干扰、许可变化、非法校准的完整 RHS 回滚、决策失败保留前一阶段提交、
  嵌套类型拒绝、两个并发 owner 会话、G1/G2 策略隔离与第三代限额。保守 bundle
  只选择另一命名 export；旧 session 原窗口仍给 MONITOR，新 session 给 STOP。
  核心新增七个结果约束/绑定 case；均使用 TinyTest 和现有模块 CTest 注册。
- **复现**：加载 `build/native-sdk-environment.json` 中的精确 roots 与现有 LLVM
  环境后，完整 `cmake --build --preset native-release-user --parallel 3`、
  `ctest --preset native-release-user --output-on-failure --no-tests=error --timeout 60`。
  完整 **48/48** 通过；两个新增回归 executable 各 repeat until-fail:20，通过。
  示例入口为 `ctest --preset native-release-user -R '^maintenance_(program_test|demo\.example)$' -V`。
  构建/测试/重复/安装日志归忽略目录 `build/maintenance-*.log`。

## 2026-11-15：maintenance 接入 Toad 0.1.3 原生计算模块

- **范围与授权**：用户指定更新 maintenance，并确认加入 RulesForge → Toad → C/C++
  原生模块示例。保留已有 staged/unstaged 重组、文档及引擎修复；没有提交或推送
  RulesForge，没有修改 presets、ProjectOptions 或 CmakeUtils。
- **SDK 来源**：现有 restore-native-sdks 入口恢复正式 Toad 0.1.3，源提交
  `c4cb667b90fac23902afa76fcdfb6540dd34a889`、producer `43666836727`；新版本独立
  安装目录，原始发布资产/摘要/manifest/producer 及 Base/CMeta/Utils/CNet/cache 组合
  校验通过。继续共享原有依赖与工具安装树，不复制或修改上游 SDK。首次共享 cache
  resolve 因父环境缺少 GH_TOKEN 失败；从已登录 gh 的认证传入进程环境后恢复通过，
  凭据未输出或写入文件。相关 .env 字段、.env.example 和 CI TOAD_VERSION 同步为 0.1.3。
- **所有权协议**：bundle 可选 native_modules 数组由 DataBind schema 显式声明 optional，
  保持旧 script-only bundle 有效；普通必需 list 会导致旧 bundle 在 JSON 验证阶段失败。
  最多 16 个部署目录内的唯一原生路径，每代拥有稳定地址的独立 CMeta registry。
  provider 在任何脚本编译前调用 Toad register_plugin；context leases 随全部保留对象
  清理结束。session scope 先关闭 Toad，generation drain 后才逆序 stop/quiesce/unload。
  候选失败不发布，也不切换实现。内部 open 请求增加借用 registry/ref 信息，公开 C
  入口保持不变。部署方需用不同的不可变 DSO 路径发布新原生实现。
- **示例计算**：signal.toad 保留两遍特征提取，C++ maintenance_risk_score 完成校准/
  权重/封顶；decision.toad 调用 maintenance_expected_loss 后完成动作选择。原生
  producer 只依赖 Toad::PluginABI，函数无可变共享状态，可由多个 owner 并发调用。
  非有限结果经 Toad 转为 HOST_ERROR，进入既有 RHS rollback；原生外部副作用不属于
  事实回滚范围。maintenance 与邻近 Program fixture 的四份脚本统一为 .toad。
- **验证**：SDK 切换后 cmake --fresh --preset native-release-user，完整 build graph、
  CTest **48/48**、install-native-release-user 全部通过。maintenance 正式 suite 为
  11 个 case，新增 JIT 调用链、八类原生候选拒绝/旧代存活、原生算术下溢回滚，原有
  八步精确决策、并发 owner 与两代 pin 均继续通过。结果仅限 LLVM-MinGW x64 Release，
  未运行 RulesForge 其他平台 CI、sanitizer 或性能验证。日志为 build/maintenance-upgrade-*。

## 2026-11-15：示例目录统一到根 examples

- 用户要求将 rulesforge/examples 迁至根 examples；目标已有领域示例。迁移保留这些
  文件，Windows 下 readme.md/README.md 的大小写冲突通过合并为 examples/README.md
  解决，保留 payments、Program、maintenance 和领域数据工具的说明。原目录已移除。
- 根 CMake 直接组装 examples；更新测试 include、Python 输入/工具路径、CTest bundle
  路径及文档链接。maintenance 生成目录为 build/<preset>/examples/maintenance-bundle。
  同时修复此前领域目录迁移留下的相对文档链接；历史日志仍描述当时的路径。
- native-release-user configure/完整构建通过，相关示例 7/7、全量 CTest 48/48 通过；
  10 份示例/入口文档的本地链接核对通过。没有改 presets/选项，也没有提交或推送。
  日志为 build/examples-move-*。

### 后续：每个示例独立子目录

- 用户进一步确认按 payments/、script_program/、maintenance/ 组织；源文件和 CMake
  分别归属其示例，公共预处理工具与共享 JSON 进入 tools/、data/。examples 根仅保留
  聚合 CMake 和入口 README。原有领域子目录保留。
- maintenance 的原生 MODULE 只收集 native_math.cpp，宿主单独收集 demo/session；
  不能对同目录所有 cpp 一起组装原生模块。生成 bundle 归 examples/maintenance/bundle。
- configure/完整 build graph、相关示例 7/7、全量 CTest 48/48 及本地文档链接检查通过；
  没有修改 presets/选项，没有提交或推送。日志为 build/examples-folders-*。
- **限制**：本机 LLVM-MinGW Release、Toad interpreter；未验证本用例 JIT、其他
  平台、sanitizer、性能或真实设备策略效果。没有新增 ContinuousSession＋Toad
  watermark/replay 入口、隐式分区窗口、外部副作用执行或持久化 outbox。没有提交/
  推送本仓库；既有工作区改动保留。语义修复会纠正以往错误的激活/漏激活结果，
  不提供旧行为开关。

## 2026-11-15：锅炉房九线程模拟输入

- 新增 examples/boiler_room：九个独立源线程产生 MQTT 风格 JSON 与 Modbus 风格
  寄存器快照，各自使用已发布 Base 的 32 KiB SPSC ring；满时阻塞，异常取消唤醒，
  join 后才回收存储。单一 owner 合并显式 delivery_time、封存 T+3 s 帧并拥有六个
  Program 会话。无 sleep 驱动业务时间，没有新增网络依赖、presets 或项目开关。
- 四个 .toad 脚本及三个 CMeta 原生模块执行热功率、对齐 30 s 效率、泵 EWMA/风险、
  热平衡/天气需求与告警状态机。RFL 显式时钟到期与 RETRACT 清理输入、窗口和输出；
  ACK 使用 occurrence/revision，独立于故障恢复。默认故事在 80/95/108/110/180 s
  分别验证泵风险、关联事件、源过期、确认和清除；P1 停流不影响其他源。
- **发布 SDK 排障**：Toad 0.1.3 的 step 是内建名字，不能作为 EXPLICIT_CALLS 的
  自定义导出；改为 advance_alert。逻辑表达式和部分 MIR 标量算术返回 numeric 值，
  不能直接当作强类型 bool/int64 结果。示例采用字面 bool、record 状态及声明式 CMeta
  int 返回函数完成检查后转换，范围 0..1000000，错误 -1 由脚本抛出，不放宽宿主
  schema 校验、不修改已发布 SDK。计数、phase 和 revision 的实际行为均由正式测试
  覆盖。类型错误增加 Argument/Field 上下文，嵌套输入/结果诊断归 core/test 回归。
- **当前语法边界**：RFL 约束不接受 tick <= $clock.tick - 30000 的算术右项，故 owner
  从 tick 单向派生 cutoff，一次 UPDATE 时钟两个字段。RHS 不支持 $input.reading.tick
  的多级取字段；计算参数仍传整个 nested record，窗口时间则引用 Clock.tick。
- **验证**：native-release-user 完整构建通过，全量 CTest **50/50**；锅炉房正式 suite
  12 个 case 覆盖重复/retained/乱序/迟到、相同输入可重复、源隔离、精确阈值时间、
  有界窗口、坏 JSON/半包、燃气表 epoch、ID 冲突、队列绕回/背压/取消、源异常及
  原生失败后的 RHS 回滚。core 类型诊断另有 2 个 case。git diff --check 通过。
  日志 build/boiler-room-{configure,build,tests,full-ctest}.log。
- **范围**：真实 MQTT/Modbus、TLS、持久化 outbox/通知、崩溃恢复与在线迁移尚未实现；
  模拟最多 240 s，没有把目标设计的服务容量说成压测结果。本次仅 LLVM-MinGW x64
  Release，未验证其他平台、sanitizer 或 JIT。未提交或推送 RulesForge，保留既有改动。

### LLVM-MinGW Release preset 的 CTest 注册

- 用户使用 llvm-mingw-x64-release-user 时没有发现测试。事实源为 presets/Options.json
  的 default-options（BUILD_TESTS=OFF）及该构建目录 CMakeCache.txt 的 OFF；此前验证
  使用的 native-release-user 继承 full-native，默认开启测试，两个目录不能混用。
- 用既有开关配置：cmake --preset llvm-mingw-x64-release-user -DBUILD_TESTS=ON，随后
  完整 cmake --build --preset llvm-mingw-x64-release-user --parallel 3 通过；同 preset
  的 ctest -R '^boiler_room' -V 两项通过（12 个用例与 demo）。没有修改 presets，
  boiler_room/README.md 已明确每次配置此 Release preset 时需要显式开启测试。
  日志 build/boiler-room-preset-{configure,build,ctest}.log；本轮没有重复全量 CTest。

## 2026-11-15：受约束的锅炉房方案选择与 agenda 正确性

- 用户授权把运维冲突做进示例：新增 dispatch 场景和第五个 Toad 模块。ROOM 的
  PumpRoute/PlanSpec 与当前帧 PumpSnapshot/DispatchRequest 事实形成诊断、四候选、
  结构准入、供热余量/剩余风险约束、最低成本与稳定 planId tie-break；宿主只传
  观测/操作输入并复制结果，不另写动作选择。Toad 使用共享流体模型，组合热能力、
  原生三次方泵功率与示例风险成本；CMeta physics 模块新增纯标量功率函数。
- 时间线：0 s KEEP，60 s REQUEST_SUPPORT，70 s DERATE，90 s REQUEST_SUPPORT，
  100 s 检修释放后 SWITCH，133 s 备用数据过期后 REQUEST_SUPPORT，135 s SWITCH，
  160 s KEEP。每个变更打印所有候选、拒绝原因、可用指标与异常证据。建议不执行
  设备动作，不改变模拟真值，也不替代告警恢复计时。模型是离散四方案与演示权重，
  没有宣称连续全局优化、真实失效概率或现场安全策略已经验证。
- **HIGH／已修复**：旧 fire_all_rules 一次从 agenda 移出最多 64 个 activation。
  前一 RHS 的 UPDATE/新 blocker 会取消后续匹配，或产生更高 salience 工作，但
  已预取项仍会执行。事实存活检查不能证明谓词/NOT 仍成立；实测出现负余量候选
  被改为 ACCEPTED、存在可行方案时提前输出 NO_SAFE_PLAN。core/test/engine/batch_test
  的新增回归先复现旧实现两项失败；修复为每次一个 RHS 后重新选择，三项新回归
  全部通过。批量事实插入与 Agenda API 保持。选择次数增加，未做吞吐 benchmark；
  后续优化必须保留 UPDATE 取消、salience 重选及 NOT blocker 回归，不能恢复旧语义。
- **所有权/清理**：沿用九源 SPSC 协议与六个 owner 会话。每帧上限一个 request、
  三个 snapshot、四个 candidate、一个 decision、三个 incident，Report 最多 241
  次决策。复制结果后 RFL RETRACT 清理；producer 规则必须带 not DrainDispatch，
  否则清理候选时 request 尚存，会重新生成事实。未排空的失败请求不能再次提交。
- **验证**：既有 llvm-mingw-x64-release-user，以 -DBUILD_TESTS=ON 配置，完整构建
  通过、全量 CTest **51/51**。锅炉房 suite 共 **24 个 case**（新增 12），覆盖硬约束、
  成本驱动选择、同成本稳定选择、错误拓扑、检修/可用性/质量、备用异常振动、
  安全停泵、脚本/原生失败传播、清理与九线程完整时间线；引擎 batch suite 7 case
  （新增 3）通过。日志 build/boiler-dispatch-{configure,build,tests,full-ctest}.log，
  旧实现失败证据为 build/boiler-dispatch-before-fix.log。三份示例文档本地链接通过。
- 仅验证本机 LLVM-MinGW x64 Release；未测其他平台、JIT、sanitizer 或性能。没有
  修改 presets、公开 C ABI 或依赖版本，没有提交/推送；保留用户原有暂存与工作区改动。

## 2026-11-15：RETE 窗口证据进入调度

- 每泵保留 (T-30 s,T] 的 1 Hz 帧（含无效标记），由 RETE 计算有效/异常计数、
  平均/峰值、前后 15 s 计数/平均、最近中断和首末时间。Toad 仅派生覆盖率、连续
  秒数和趋势；不存在宿主第二份统计队列。初始源暖机后至少 24/30 帧才可用，趋势
  另需两半窗各 12/15。epoch 变更清旧帧；时钟可独立推进到期，缺测打断连续异常。
- Runtime::pump 强制同一时间轴连续 1 s 帧，重复/回退/跳帧在插入前失败。缺测需
  显式 advance_pump_clock，生成 UNKNOWN 标记；新遥测不能把旧空档计成连续有效。
  连续秒数由端点计算，30 帧最多证明 29 s，11 个连续异常帧才证明 10 s。
- ROOM 接收同 tick 的三份窗口，RETE 分类 UNKNOWN/NORMAL/TRANSIENT/DEGRADING/
  CONFIRMED。未确认不建议 DERATE/SWITCH；TRANSIENT 的最优 KEEP 输出 OBSERVE，
  保留 KEEP planId。风险成本不低于当前风险，并纳入窗口均值与有覆盖保证的上升
  趋势外推；历史不会因一个好读数马上消失。四个物理候选、公开 C ABI、SDK、
  presets 与九线程/六 owner 会话协议不变，每帧最多额外三份窗口与三个分类并清理。
- window 场景在相同瞬时风险 50 下，60 s 单次尖峰 OBSERVE，130 s 持续异常 DERATE；
  119 s 已由恶化趋势驱动降负荷，160 s SWITCH，173 s 源过期 REQUEST_SUPPORT。
  180 s 遥测恢复仍 UNKNOWN，232 s 新证据覆盖达标才恢复 SWITCH。既有 dispatch
  增加 0..51 s 窗口不足阶段；160 s 当前恢复后仍保留历史风险，174/175 s 才逐步
  转 KEEP/OBSERVE。直接调度单测使用 tests/dispatch_fixture.hpp 显式构造历史，
  默认 DispatchInput 不伪造健康窗口。
- **HIGH／已修复：嵌套记录条件**。四个最小 core 用例在旧实现全失败。既有
  Fact::get_field 点路径只遍历 FactList，无法访问 DataBind ValueMap；类型查找又
  回退外层 Object，未取得声明的 bool/long/double 叶子类型。补齐 ValueMap 值读取
  和有限命名记录的类型遍历；复制子值后再释放当前拥有值，保留 NULL-safe/index
  语义。nested_constraint_test.engine 的 8 case 覆盖值、类型、两侧绑定、UPDATE、
  false 与空引用，已通过；未通过宿主展开/重复字段绕开引擎契约。
- **HIGH／已修复：聚合来源 OR**。窗口把正常帧误排除出中断集，安静窗口和尖峰
  因而被误判 CONFIRMED。三个正式回归在旧实现失败。含 OR 来源保留 AST，用既有
  短路谓词按左 token/源事实计算；两分支同时满足也只计一次。纯 AND 保持原 alpha/
  join 路径。回归覆盖 MODIFY/RETRACT、外部绑定变更和分支重叠；没有更换统计口径
  或放宽窗口阈值掩盖错误。OR 每对 token/来源的判定开销未做吞吐 benchmark。
- **验证**：既有 llvm-mingw-x64-release-user、BUILD_TESTS=ON，完整 build graph
  通过，全量 CTest **53/53**；锅炉房 **34 case**（新增窗口 10），nested 8 case，
  聚合约束 suite 10 case（新增来源 OR 3）。当前仅 LLVM-MinGW x64 Release，未验证
  JIT、其他平台或 sanitizer；真实设备及风险参数未验证。日志 build/boiler-window-
  {build,tests,full-ctest}.log，旧失败证据为 boiler-window-nested-before.log 与
  boiler-window-or-before.log。未提交/推送，用户其他改动保留。

## 2026-11-16：锅炉房 DataBind、字符串所有权与失败边界

- **MED／已修复**：runtime 原先逐帧用 `std::string` 拼接 JSON、用 stream 格式化
  数值，并为固定标签创建拥有型字符串。改用已发布 SaltsUtils 0.1.4 的
  `salts_idl_target`，从 boiler.schema 生成原生记录/C++ owner；字段由宿主填写，
  JSON 序列化和 schema 绑定统一交给 DataBind，最终提交 DataBindValue。七份
  调度输入先完整绑定再提交，绑定失败不留下半组事实；不承诺多次插入的原子性。
  生成 owner 持有 tstr，vstr 同步借用；报表标签经 re2c 整词校验后持有静态
  string_view，不借用 query 或调用方缓冲区。re2c 不处理 JSON。
- **SDK 事实**：0.1.4 使用 salts-idlc/CMeta，不能套用旧 TBE_TYPED_* 接口。
  生成 Message 的文本转换已接纳；composite root 转换明确返回不支持。初始
  WindowEvidence 继续用 DataBind 绑定固定 JSON fixture，动态窗口走生成的
  WindowSnapshot Message；没有捕获失败后换实现。CMeta 头会定义 Schema(...)
  宏，消费方类型使用 BoundSchema 避免构造函数名称被宏展开。SDK/工具均使用
  当前恢复的发布包，不改 presets、不增加 cache/environment 选项。
- **HIGH／已修复**：不完整寄存器组和 JSON 解码失败原先转换成无效 Sample
  后继续。现在在解码边界直接失败，保留 DataBind 原始诊断；缺失/过期仍是业务
  UNKNOWN。测试分别注入寄存器和 JSON 错误。线程集合固定为数组，先构造 owner
  再启动，部分启动失败由析构 cancel/join；删除启动处 catch/rethrow。线程入口
  的异常传输、进程入口的非零退出及 C ABI 错误码转换保留。
- **MED／已修复**：嵌套 ScriptValues 字段诊断使用栈上 RAII 路径作用域，在失败
  点一次生成完整字段路径，去掉每层 catch/rethrow；原先两个严格类型诊断用例
  保持通过。任意业务字符串的 owning ConstraintValue 表示没有改为借用 view。
- **HIGH／已修复**：RHS 数字字面量溢出原先落到 ExpressionDescriptor 重新求值，
  可把越界整数变成近似 double。改用 from_chars 整词解析，越界/非有限值直接
  返回 parser 错误；新增 int64 两端精确值、正负整数溢出、double 溢出三个正式
  回归。其余正常表达式仍由既有表达式接口处理。
- 去重载荷比较复用 Salts::SIMD，仅比较声明长度内的字节；不足 16 字节的尾部
  零填充，不越界读。正式用例覆盖 0..512 字节、非对齐输入、尾字节变化和空 view。
  固定标签/场景使用 re2c DFA；模拟时间线和输入校验的分支不冒充业务规则，RFL
  继续承担窗口、关联、硬约束与最低成本选择。未做吞吐 benchmark，不宣称提速。
- **验证**：llvm-mingw-x64-release-user、BUILD_TESTS=ON，完整 build graph 通过；
  相关 CTest 6/6，全量 **53/53**。锅炉房 **41 case**，RHS parser **45 case**，
  ScriptValues **2 case**。日志 build/boiler-refactor-{configure,build,tests,
  full-ctest,codegraph}.log；CodeGraph 已同步。只验证 LLVM-MinGW x64 Release，
  未验证其他平台、sanitizer、JIT 或真实设备。未执行提交、推送或发布。

## 2026-11-16：Core 所有权、统一前端与严格失败传播

- **HIGH／已修复**：SessionPool 构造失败原先缩小池，归还失败又尝试 reset，甚至
  将失败会话重新入队。现在完整创建固定槽位，以 Available/Borrowed/Returning
  单一状态维护租约；替代会话创建失败保留原租约并传播错误。priming 会调用应用
  谓词，必须在池锁外创建；正式回归在回调中读取 available_count，防止锁内调用
  造成死锁。池只限制并发租约数，不限制单会话事实内存；归还时暂有新旧两份会话。
  自动 SessionGuard 析构不能返回错误，需要处理归还失败时应显式管理 release。
- **HIGH／已修复**：默认 fire_all_rules 吞掉 RHS 错误并继续执行。两个现有 firing
  入口现在共享严格路径，保留事务回滚并传播原始失败；激活指针由 RAII 清理，
  失败不调用 after_rule_fired。C ABI 的异常到错误码转换及既有有界 committed-
  operation 恢复协议仍属合法边界，不能为减少 catch 删除清理与错误传输契约。
- **MED／已修复**：第二套决策表正则/手写语法与失败后重试 RFL 的路径删除；统一
  为 DecisionTableConverter → RFL re2c/Lemon。主语法补齐文本逻辑词、括号、裸
  布尔否定和裸算术右值。NOT 的 raw AST 使用主语法 nested_patterns 包装；不要
  为兼容旧测试形状重新实现第二套语法。未知 directive、非法 salience 直接失败。
  历史统计名保留，parser_path_success 计数唯一前端，旧 direct/reason 计数为零；
  编译统计使用 thread_local。
- 普通 RHS token、字段路径输入和标量表达式切片改用 string_view。转义 token 与
  interned key 使用 Base tstr 的 OwnedText 单一 owner，vstr 与 C++ view 同步借用。
  字典 rehash 不移动字符存储；全局 interner 仍是进程生命周期，clear 要求借用者
  全部静默，未实现 generation 回收。任意业务字符串和持久动作保留拥有型存储。
  已有字符串比较直接借用，UUID/日期等格式化结果由局部 owner 持有。
- RHS lexer 与有限操作符／函数／表头词汇改用 re2c。LHS/RHS 数学函数共享枚举、
  固定两项参数和 arity/domain 校验。from_chars 区分非数值、越界和非有限；整数
  溢出不重试 double。ExpressionDescriptor 删除未使用的 sanitized 字符串副本。
- 继承降低后为 processed_rules 赋予 RHS 计划下标，激活直接索引计划。OR 展开
  多个网络终端，但仍共享逻辑规则及其 RHS 计划，不增加 processed_rules 数量。
  正式回归用两条逻辑规则、三个 OR/普通匹配验证计划选择（3 次 firing，Marker
  合计 33）。共享 Alpha/Beta、HashedJoin、会话内存和 Command 计划已存在；标量
  表达式仍是递归解释路径，未实现完整指令 IR 或无分配执行。
- **性能取证**：正式 text_compare_benchmark 比较 string_view、Base vstr 与公开
  128 位 Salts SIMD 逐块 load/compare/reduce；5 个样本×10000 对、长度 4..4096，
  半数相等、半数尾字节不同。测试输入每个长度重新初始化，避免前一长度的差异
  污染下一组；尾部零填充防止越界。最后一次 16 字节 string_view/SIMD 约
  5.18/11.23 ns，4096 字节约 85.95/2112.61 ns，因此不将此候选放入 core 热路径。
  这不代表所有 SIMD 算法或其他平台，也没有整应用吞吐收益结论。
- **验证**：既有 llvm-mingw-x64-release-user，BUILD_TESTS=ON、BUILD_BENCHMARK=ON，
  完整构建通过；相关 CTest **7/7**，全量 **61/61**（其中 benchmark 6），独立重跑
  text_compare_benchmark **1/1**。日志 build/core-refactor-{configure,build,tests,
  full-ctest,codegraph}.log 和 build/core-text-compare-benchmark.log。详细边界见
  docs/architecture/core-ownership-and-dispatch.md。只验证 Windows LLVM-MinGW
  x64 Release；未验证其他平台、sanitizer 或整应用剖析。SDK、presets 与公开 C ABI
  声明未修改；没有新增配置开关，没有提交、推送或发布，保留其他已有改动。

## 2026-11-16：Core MIR-JIT 与传播失败状态

- Core 标量表达式在加载时经独立 re2c lexer/有界语法树降低到 MIR 原生程序；
  原 ExpressionDescriptor 名称保留，但现在持有代码及编译后的字段路径。RETE
  alpha/join 算术、eval、外部谓词数值参数、聚合值表达式及 RHS 赋值/条件/switch
  共用该能力。删除 LHS/RHS 的递归字符串解释与重复语法扫描。字段绑定/global/
  脚本结果仍由其 owner 读取，不固化 Fact 地址或可变容器下标。
- **HIGH／已修复**：整数经 double 运算再转回会丢精度甚至越界；旧字符串运算还
  将同级减除按右结合求值。现在 int64 加减乘使用 MIR ADDO/SUBO/MULO + BO，
  精确并检查溢出；除零/非有限值失败。混合整数与 double 的比较先检查转换范围，
  保留 int64/uint64 身份；除法、数学函数及含 uint64 算术继续采用浮点结果。
- 每表达式上限为 16 KiB 源、256 操作、64 变量、64 层嵌套。程序持有 MIR context/
  代码/字面量，规则与节点共享不可变程序，调用使用固定容量独立栈帧；不同线程
  可共享生成代码，不并发调用同一 MIR context 的编译/释放 API。变量短路延迟
  读取并按次缓存，结果转回拥有型 ConstraintValue。没有全局代码缓存或解释器
  fallback；上限不等于整个进程或 MIR 分配器的 RSS 硬限制。
- **SDK 排障**：共享 vcpkg-cache 0.1.3 已含 mir-jit 1.0.0#3 / MIR::MIR，新增
  required find 和既有 manifest 声明，不恢复下载器/开关。MIR_new_proto_arr 的
  prototype 必须命名；传空名称曾使首轮编译测试崩溃，修正后全部编译用例通过。
  MIT 原始许可随根 THIRD_PARTY_NOTICES 分发，未修改 SDK 或上游源码。
- **HIGH／已修复**：Windows LLVM-MinGW 首轮从 JIT helper 抛出再捕获异常仍
  出现 libunwind pc not in table。改为显式 host request/continuation：退出
  生成代码后由 C++ 读取变量、比较/格式化或调用数学函数，再从指定位置继续。
  这不是失败重试，也不重复执行之前节点。原生 helper 仅做不分配/不抛错操作；
  异常直接在 C++ 栈传播。RHS 将标量类型/domain 错误转换为既有 runtime_error
  契约，由原事务 owner 回滚。最终详细日志无该 libunwind 诊断。
- **HIGH／已修复**：非 RHS 传播抛错可能留下部分网络更新，却仍允许继续运行。
  RAII 失败作用域标记已有会话不一致；firing/query/事实变更失败，显式 reset
  成功后才恢复。RHS 内仍按原回滚结果决定一致性；不把已成功回滚的会话封死。
  批量 flush、入口流、事件时间、query 刷新和 reset/priming 同样受保护。
  borrowed Fact 需存活至 reset/销毁；失败的非事务操作不承诺逐项回滚。
  MODIFY 的 changed-fields hint 用 RAII 清空，抛错不留下悬空借用。
- 未知函数与错误 arity 在加载期失败；移除 concat/to_upper/to_lower 的历史
  frontend 改写（原执行器未实现这些函数）。保留缺失 LHS 字段不匹配；已提供
  但非数值的算术输入现在明确失败。修正履约示例的 Number.intValue() 为既有
  intValue 字段。RHS 重建表达式时重新转义已解码字符串，避免引号/换行失真。
- 正式 Toad 0.1.3 的现有 mode=jit 接口无需新配置；新增 C API Program 回归验证
  两个脚本文件、JIT 计算及旧 session 的 generation 固定。没有以本地 Toad 修复
  替代发布包，core 的整数语义也不经 Toad NUMBER 转换。
- **测量**：scalar_program_benchmark 分开测重复求值和整个程序生命周期。最后
  一次简单式 $x*3+7，重复求值约 234 ns（含栈帧/变量读取/结果恢复）；解析/降低/
  编译/一次调用/销毁约 0.611 ms。5 个样本，分别每样本 10000/10 次。C++ 纯
  算术是成本参考，不是旧解释器基线；无整引擎提速或盈亏平衡结论。
- **验证**：llvm-mingw-x64-release-user，既有 BUILD_TESTS=ON、BUILD_BENCHMARK=ON，
  完整 build graph 通过，全量 CTest **64/64**（benchmark 7），相关 **9/9**，
  verbose **3/3**。新增 scalar_program suite **13 case**、scalar_jit **6 case**，
  覆盖精度/溢出/短路/资源限额/异常/并发/事务/部分传播失败与 reset。
  日志 build/core-mir-{configure,build,tests,full-ctest,verbose,codegraph}.log；
  CodeGraph 已同步。仅验证 Windows LLVM-MinGW x64 Release，未验证其他平台、
  sanitizer 或真实设备；没有更改 presets/环境变量或公开 C ABI，没有提交/推送/
  发布；保留用户已有暂存与其他改动。

## 2026-11-16：Payments CLI 改用发布 SDK 的 CmdParser

- cxxopts 的唯一消费方是 examples/payments/capi_demo.cpp。移除其 include、
  find_package、链接目标、vcpkg manifest 依赖与活动文档要求；改用已发布
  SaltsBase 0.1.8 的 Salts::CmdParser。target 名区分大小写，不是 CMDParser；
  公开头是 cmd_arger.h，C++ 消费方需 extern "C" 包装其 C 声明。没有新增 SDK、
  配置变量、CMake 开关或下载逻辑，没有修改共享缓存。
- 参数绑定直接借用 argv/SDK 保留至进程退出的 response storage；默认 binding
  由进程级 DemoOptions 持有。validation 使用 SDK enum descriptor，普通选项、
  flag 和重复 map 使用对应 descriptor。保留长短选项、长选项等号值、可选位置
  路径、-- 分隔符、重复及逗号分隔映射；源码 token/映射切片使用 string_view。
- **SDK 契约与兼容**：公开 parse 在错误或内建 help 时直接 exit(1)，并用 stdout
  打印。解析在任何 RulesForge 资源创建前执行；按同一 descriptor 的 value arity
  将显式 -h/--help 归一为普通 help 请求，正常解析后由应用显示 usage 并返回 0。
  选项值或 -- 后的 --help 保持字面量；help 不掩盖未知选项。解析期临时将 stdout
  FD 指向 stderr，成功后恢复，业务输出不受影响。没有将 SDK 的失败码改为成功。
- **MED／CLI 边界**：短选项独立书写（如 -v -h、-r file），组合或附带值的短
  token 在进入 SDK 前明确拒绝。公开 list parser 满容量后不追加也不报错，因此
  map 提供 1024 个有界槽位，并用其 validator 在下一项进入前失败，禁止静默截断。
  1024 指 --map 参数次数，单次参数仍支持逗号分组。文件输入冲突/缺失类型在创建
  RulesForge 资源前检查。C++ 入口只保留统一异常到非零退出的边界。
- **验证**：llvm-mingw-x64-release-user 完整构建通过；相关 CTest **4/4**，
  当前完整 graph **57/57**（BUILD_TESTS=ON、BUILD_BENCHMARK=OFF）。现有
  examples_cli_and_data 正式入口共 22 个脚本 case，新增覆盖 help 退出/字面量、
  非法选项/缺值/enum/flag、位置参数、等号值、多映射及容量失败。没有独立验证
  工程或新增 CTest 功能开关。日志 build/cmdparser-{build,tests,full-ctest,codegraph}.log。
  git diff --check 通过，CodeGraph 已同步。只验证 Windows LLVM-MinGW Release；
  没有更改 presets，没有提交、推送或发布，保留其他已有改动。

## 2026-11-16：七类业务场景与 mock API

- 新增 quality_release、energy_dispatch、cold_chain、production_routing、
  release_gate、reconciliation、service_entitlements。各目录拥有自己的 schema、
  RFL、Toad 模块、输入响应、main、部署 bundle 与 tests。全部走已安装公开 C ABI，
  使用正式 Toad 0.1.3；没有改 engine、SDK、依赖、presets 或公开接口。
- examples/support 仅共享同步 RAII 与有界 mock API：最多 128 条输入、64 条决策／
  动作、每个文本字段 95 字节。决策从查询复制后才能越过 session 销毁；存储以
  整批 staged table 提交，ID 重放相同则幂等、冲突则失败。投递成功才标记已送达，
  失败立即传播，后续必须显式调用投递。没有自动重试、无界队列或磁盘持久性。
- **MED／已修复**：RFL 的 within、enabled 是保留词，不能直接用作普通字段。
  新示例改为 inSpec、offered。对同一事实两个字段作比较时使用显式绑定，如
  eval($comparison.difference <= $comparison.tolerance)；不要把裸 tolerance 当成
  已绑定字段。首轮正式 CTest 暴露这些问题，最终示例与回归已通过。
- mock 输入的 checkpoint 在下一条记录前显式 firing，保证冷链逐次生成 Exposure
  后再观察窗口，而不依赖同批 agenda 的插入顺序。窗口为明确 cargo-1 的 length(3)，
  不声称隐式按键分区、事件时间或 ContinuousSession + Toad 已集成。
- 能源示例穷举三时段 {-2,0,2} 的 27 个计划；Toad 计算轨迹及成本，RETE 负责约束
  和最小成本／rank 选择。独立 C++ 枚举验证 P08、成本 2.72；只证明该有限集合的
  最优性，未新增外部求解器。工序分配是逐请求建议，不是全局排程／资源预留。
- **验证**：现有 llvm-mingw-x64-release-user，BUILD_TESTS=ON、
  BUILD_BENCHMARK=ON，完整构建通过；相关 CTest 15/15，全量 **79/79**（benchmark
  7），新 TinyTest 共 **47 case**，七个业务 suite 均有正式 Toad JIT 场景。日志为
  build/scenarios-{configure,build,tests,full-ctest}.log 与
  build/scenarios-full-results.xml。只验证 Windows LLVM-MinGW x64 Release；
  未验证其他平台、sanitizer、真实网络服务或磁盘持久化。
- 配置必须加载当前 .env 的非密钥 SDK/工具字段，包括 RE2C_ROOT、LEMON_ROOT；
  CMakeCache 已有工具路径不能代替 FindTools 所需父环境。首次配置在缺少
  RE2C_ROOT 时明确失败，加载既有配置后通过；没有改查找策略或切换工具来源。
  本次 Ninja 日志报告 premature end of file 并按工具自身行为恢复；完整构建退出码
  为 0，未调查该本地增量日志告警的成因，不将构建描述为无告警。

## 2026-11-16：Core 目录与编译单元重构

- core/include、core/src 按 model、text、memory、runtime、execution、script、
  diagnostics、rete 归属组织；RETE 再区分 compiler、nodes 与私有 detail。
  70 个文件迁移后同步更新 parser、C ABI 实现、provider、测试、benchmark、
  内部示例和活动文档的 include／源码引用；历史 HANDOFF 路径保留原记录。
- 删除未使用的两层 rfl_rete_defs 转发头；原 912 行模型实现拆成 value_types、
  constraint_types、fact、token、parsed_rule 五个编译单元。TypeParameter 的
  构造／赋值定义属于 constraint_types；值哈希直接使用 CSTL hash_bytes，
  value_types.cpp 必须显式包含 cstl.h，不能依赖其他头间接导入声明。
- 原 rete_node.cpp 和七个 .inc 合并关系改成 base、join、transform、query
  四个独立 cpp，与节点族头文件路径对应。跨节点求值 helper 的声明／实现归
  src/rete/detail；聚合局部 helper 保留在 transform.cpp。四个 profiling
  atomic counters 在 diagnostics/rete_profiler.cpp 单一定义，节点共享。
- 模型源仅由既有内部 rfl_parser target 组装一次，engine 显式依赖它；
  src 的私有 include 路径不经 parser PUBLIC 传播。CMake 使用既有 source
  helper／CONFIGURE_DEPENDS 和 source_group；没有新增模块、测试或诊断开关，
  没有修改 presets、SDK、公开 C ABI 或安装 target。
- **HIGH／已修复**：收窄新模型文件的 include 时，对 CRLF 文本用裸
  partition('\n\n') 会匹配失败并产生空 body；本次链接检查报告模型定义
  缺失，已从本轮保存的原始模型源恢复五个实现。脚本应先统一行尾，并断言
  分隔符与正文非空；明确写出行尾，不能在切分失败时覆盖正文。失败日志保留
  build/core-layout-include-split-failure.log。另一次直接 include 检查揭示
  TypeParameter 和 hash_bytes 的真实声明依赖，已按上面的归属修正；原始错误见
  build/core-layout-direct-includes-failure.log。
- 迁移期间旧 engine 路径出现一份重新格式化的 schema_validator.hpp。已比较
  正文 token 与 include 集合，确认仅格式／行尾不同；当前 runtime 实现保留，
  等价副本无覆盖移动到 build/core-layout-preserved/schema_validator-formatted.hpp。
  覆盖移动及旧空目录清理命令曾被自动审查以 blocked by policy 拒绝；最终使用
  无覆盖归档完成文件布局，未删除或覆盖该格式化副本。
- **验证**：llvm-mingw-x64-release-user，BUILD_TESTS=ON、
  BUILD_BENCHMARK=ON，完整构建成功；重点 CTest 7/7，全量 **79/79**（benchmark
  7、scenario 15），无失败、无跳过。公开 rules_forge.h 与重构前快照逐字节相同，
  DLL 的 **121 个导出名称**集合相同。日志 build/core-layout-{configure,build,
  focused-ctest,full-ctest,codegraph}.log，JUnit 为 core-layout-full-results.xml，
  导出比较为 core-layout-exports-{before,after}.txt。CodeGraph 已同步。
  仅验证 Windows LLVM-MinGW x64 Release；未验证其他平台、sanitizer 或性能收益。
  本地 Ninja 仍报告 premature end of file 自恢复告警，未调查其成因。未提交、
  推送或发布，保留已有暂存与其他工作区改动。

## 2026-11-16：0.1.0 发布前的平台验证

- **MED／已修复**：首次发布 CI 43812881731 在四个平台恢复 SDK 时要求过期的
  CHTTP_VERSION；恢复入口现与 CMake 一致，选择 Toad 0.1.3、SALTS_CORE_ROOT
  和各 triplet 的既有缓存描述文件。Toad 包内含 RID 目录，必须选择该 payload，
  并核对内嵌 manifest 的依赖、提交及成功 producer run，不能按其他 SDK 的平铺
  tar 布局恢复。没有新增 SDK 版本、依赖来源或 presets 开关。
- **MED／已修复**：CI 43813665378 的 macOS SDK 标准库没有浮点 from_chars。
  两个标量浮点入口改用统一的 C numeric locale 转换，NUL 终止的临时文本由
  既有 OwnedText／tstr 持有。Windows 使用 _strtod_l，POSIX 使用 strtod_l；
  这是平台实现选择，不在转换失败后重试。拒绝前导空格／加号、十六进制和尾随
  字节，保留整数溢出、浮点非有限值及范围错误；可表示的 subnormal 不因 libc
  设置 ERANGE 而误判溢出，不修改宿主全局 locale。
- **MED／已修复**：同轮 Linux GCC 14 头文件对递归 pair<string, ScriptShape>
  过早实例化完整类型检查。改成 vector<ScriptShapeField>，字段在 ScriptShape
  完整定义后声明，保持名称、嵌套 shape、复制和遍历语义。
- Android 编译示例也需要在 Linux 主机运行 salts-idlc。恢复相同发布版本的主机
  Base、CMeta、Utils，并传入 Utils 已有的三项 SaltsUtils_IDLC_* 配置；主机和
  目标 SDK 根独立。不得用 Android 的可执行文件作为主机生成器，或跳过示例。
- **验证**：Python／PowerShell 恢复脚本语法及 diff 检查通过；现有 Windows
  LLVM-MinGW preset 的 rhs_parser_test.parser、scalar_function_test.engine、
  script_values_test.engine **3/3** 通过。新增数字用例覆盖无终止符切片、嵌入 NUL、
  科学计数、最小 subnormal、零下溢和不支持的 C 转换扩展。日志
  build/release-portability-{build,focused-ctest}.log。发布仍由完整平台 CI、
  原生 CTest、Android ELF 对齐和最终 pack/publish gate 决定。
