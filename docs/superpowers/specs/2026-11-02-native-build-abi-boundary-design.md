# PanguEngine 原生增量构建与 ABI 交付边界

状态：本期原生构建改造已实施并通过验收；独立交付 SDK/原生插件的新版 ABI 接纳实现属于后续任务。日期：2026-11-02。分支：`feat/native-build-abi-boundary`。设计基点：`9a123d6f70b8`。验收：[实施验证记录](../plans/2026-11-02-native-build-abi-boundary-validation.md)。

**决定：只维护一套 CMake + Ninja 构建。普通目标从 configure 到最终链接均不运行 AbiProfile；Tests 同样如此。独立交付的 C++ 二进制才需要兼容性描述。删除没有实际消费者的旧构建实现，不留默认关闭的备用系统。nlohmann/json 作为项目第三方依赖保留，不随旧 AbiProfile 清理。**

## 1. 为什么改变原设计

`feat/final-executable-abi` 的验收记录（`6610a3f:docs/superpowers/plans/2026-10-29-final-executable-abi-validation.md`，第 19、188、195 行）暴露了三个不同问题：49 个编译包装器分别观测整库，工作量约为 49×49，Windows 收据共约 2.36 GB；一次 Linux 冷准备约 95 分钟，随后每个 EXE 又花 5–9 分钟重验依赖收据；采集器变化还使可用对象因旧收据不匹配而不能继续构建。这是该分支的历史数据，不是本设计的性能成绩。

| 方案 | 取舍 |
|---|---|
| 缓存或压缩逐 TU 收据 | 能减轻部分重复读取，但仍维护 launcher、对象身份、失效、恢复和五平台收集器。拒绝。 |
| `dev/verify` 双流水线 | 普通构建可绕开旧系统，但每次修改规则仍要维护两种行为；测试和工具容易重新接入旧路径。拒绝。 |
| 单一原生构建，独立交付才比较 ABI 配置 | 采用。借鉴 O3DE 的真实 CMake target、UE5 的编译与交付分工，不移植其包装层或调度器。接受不再证明每个本地对象来源的取舍。 |

## 2. 同一构建树内：谁负责什么

CMake 4.0+、现有五平台 Toolchain/Presets、C++23 和各平台 Clang 策略不变。`add_library`、`add_executable`、`target_link_libraries` 描述实际目标；保留 `pangu_configure_module()`、一个 Pangu Module 一个真实库、`Pangu::` 与 `ThirdParty::` 的命名边界。普通测试链接同一份引擎库，不另编测试版引擎。静态 Catalog 仍显式引用注册入口，注册、依赖排序、回滚和代码驻留契约不变。

CMake 产生规则，Ninja 根据输入、编译器报告的头依赖、输出与命令变化决定重建。Pangu 不扫描 `compile_commands.json` 来替代 Ninja，不重新导出链接图，不向编译命令强制注入 `inputs.h`。编译数据库保留给 clang-tidy、IDE 和人工诊断。允许用户选择编译缓存 launcher，但项目不安装 ABI launcher；性能对照使用一致的缓存策略。不自研 `PanguBuild` 或另一套调度器，也不引入新的 Module 描述语言；不在 CMake target 之外重复声明源码和链接依赖。

代码生成、资产处理和打包由消费其产物的目标或显式命令触发。需要生成源码的目标用 `OUTPUT`、`DEPENDS` 和必要的 `DEPFILE` 声明规则；打包目标可依赖资产处理，反向依赖不成立。普通 C++ 构建（包括 Foundation 和 Tests）不隐式处理资产或打包，不用 `ALL` 或通用 `POST_BUILD` 挂载这些步骤。

受控 C++ 接口共用工具链与 ABI 关键配置；引擎代码不在单个源文件私改 CRT、STL ABI 或公开类型布局。允许影响代码质量而不改变公共 ABI 的局部选项。不承诺发现偷偷覆盖的宏、被替换的本地对象，或同一路径原地替换 SDK 后的一切变化。工具链/SDK 原地升级时显式重新配置，必要时使用干净构建树，不恢复逐 TU 守卫。

## 3. 交付边界：哪些地方才需要 Profile

| 场景 | 规则 |
|---|---|
| 同树源码库、普通 EXE、测试 EXE 与 fixture | 不生成、读取或比较 Profile；是否是最终 EXE 或 STATIC 归档不是触发条件。 |
| 第三方源码、header-only C++ 库 | 用项目现有 `ThirdParty::` target 接入，允许直接使用 C++ API；不强制稳定 C ABI、额外包装库或 Pangu Module。上游特有的编译要求由其 target 表达。 |
| 独立分发的 C++ SDK／预编译库 | 由包的交付方提供与实际二进制绑定的兼容信息，消费方在接受包、进入最终链接前比较。供应商不必伪造 Pangu 收据；无法建立可信映射时，不接受为兼容的 C++ 包，可改用受控源码构建。本设计不按语言标签另设第三方豁免。 |
| 与宿主同树、同配置的配套插件 | 在受控构建树内使用，或与宿主整套复制、安装、交付，均免 AbiProfile 预检。即使宿主在运行时发现并动态加载它，也不因加载方式或安装目录改变规则。 |
| 单独交付给已有宿主的原生 C++ 插件 | 在 OS Load **之前**无执行地确认实际待加载文件、ABI 配置、协议和 Module API 契约；无法确认兼容、绑定文件或防止检查后替换时拒绝。加载后的 C++ Query 只做二次复核，不能代替预检。 |

豁免跟随配套交付，不跟随路径。将另一棵树的同名 DLL 复制进 `Bin` 不会使它成为同树插件；手工混入或替换受控开发目录中的 DLL 属于不支持的用法，不为识别这种情况新增文件哈希、收据或逐次扫描。豁免只针对 AbiProfile，不改变 Module API、注册和生命周期契约。

未来的配置描述（暂称 `config-v2`）按**目标平台、架构和有效 ABI 配置**生成一次，供该配置的边界产物引用。配置指纹描述双方的编译约定；产物摘要识别交付文件；可信来源与 API 契约分别验证。摘要不是签名，配置相同不证明 ODR、内存所有权或每个对象的编译事实。新结果不得标为 `strict-v1`，也不接受把旧收据改标签作为迁移。

描述来自实际目标工具链/配置；需从标准库或编译器确认的字段可用小型目标探针，不在宿主运行移动目标程序。兼容字段关注目标 ABI、编译器、标准库/CRT、异常/RTTI、公开布局和必要的 SDK/部署目标；不含时间戳、绝对路径、Module 名和告警等级。本期不冻结序列化、CLI、签名或插件加载实现。**没有真实导出方和独立消费方时，不实现 v2 编码器、探针或比较工具，也不开放未经验证的独立交付二进制入口。**

## 4. 删除与保留的边界

| 基点上的位置 | 实施时的处理 |
|---|---|
| 根 `CMakeLists.txt`；`Source/Runtime/{Foundation,Module}/CMakeLists.txt` | 删除 ABI include、注册、全局 finalize 和默认工具目标；正常 configure 不构建、不下载、不查找 ABI 工具。保留工具链预检。 |
| `cmake/AbiProfile/`、`cmake/BuildTools.cmake`、`BuildTools/AbiProfile/` | 删除旧构建入口、专用 bootstrap、各平台 collector/probe、compile/guard/collect、闭包、收据、锁、恢复状态与私有辅助代码。没有其它调用方就连同旧 CLI、CMake 选项及专用说明删除；不迁入 Legacy 目录或旧功能分支。 |
| `Tests/Module/Fixtures/CMakeLists.txt` | 去掉 `First`、`Second` 的 ABI 注册；保留两个真实库、生成的 Catalog、dead-strip 链接和注册顺序测试。 |
| `Tests/Toolchain/`、`Tests/CMakeLists.txt`、`Tests/AbiProfile/` | 删除 `PanguAbiCompileAudit ALL`、专属脚本、旧协议夹具和只验证旧内部机制的测试。保留环境预检、真实第三方 C++ 库的编译/链接、Foundation 与 Module 功能测试。若某断言验证仍有效的契约，迁到真实消费者；不保留无人使用的工具来养测试。逐组记录淘汰理由或替代断言，遵守 `Tests/README.md`。 |
| `Source/ThirdParty/NlohmannJson` 与 `.gitmodules` | 保留 nlohmann/json 的上游子模块、gitlink、`.gitmodules` 条目、根 CMake 注册、`ThirdParty::NlohmannJson` target 和接入说明，即使旧 ABI 工具及其测试移除后暂时没有消费者。子模块初始化是配置前提：即使只构建 Foundation，缺少 `json.hpp` 或 `json_fwd.hpp` 也在 configure 阶段直接报错并提示初始化，不自动下载、不延迟注册。只更新过时的消费者说明；不保留旧 ABI 代码，也不让无关目标链接它。 |

以构建入口、源码引用、脚本、测试注册、submodule 和文档交叉引用核对删除范围。没有业务消费者的占位实现、兼容开关和空目标不得进入交付。旧分支保留历史但不合并；历史 spec/plan/ADR 保留并标明替代关系，不作为活动实现。迁移不清空现有 BuildRoot 或其他工作的缓存；旧树不作为验收依据。

## 5. 验收：先数实际动作，再看耗时

| 场景 | 必须观察到的结果 |
|---|---|
| 干净 configure；仅构建 `PanguFoundation`；原样再构建 | 全程零 ABI 命令、零收据、零 ABI 目录；普通构建图中没有隐式资产处理或打包步骤。第二次 Ninja 报 `no work to do`，不是 guard 启动后返回 `current`。 |
| 改 `PanguModule` 一个私有 `.cpp` | 只重编该 TU，再归档所属库；仅在请求依赖它的最终目标时重链接。Foundation 和无关 TU 不重编。 |
| 改公共头；另改实际编译选项 | 各自触发原生依赖图中的受影响动作。用 `ninja -d explain` 和详细命令核对原因；不能只测“什么都不做”的情况。 |
| `BUILD_TESTING=ON` 默认构建，多个测试 EXE 共用引擎库，再运行 CTest | 在没有任何 ABI 工具或遗留证据的干净树通过。相同配置下引擎库只构建一份；增加 EXE 只增加其必要编译/链接，不重新审计共同依赖。另验 `BUILD_TESTING=OFF`，不能靠关测试提速。 |
| 桌面平台 | Windows、WSL Ubuntu、macOS 按正式 preset 做原生构建和 CTest。 |
| Android / iOS 链接冒烟 | 保持正式 preset 的 `BUILD_TESTING=OFF`。单独用相同的目标工具链和 SDK 将最小程序实际链接为目标平台二进制，记录链接命令及产物；不要求链接 Pangu 生产库，不执行设备程序，不加入默认 `ALL`。仅验证平台链接器能工作：configure 成功、编出 `.o` 或 `.a` 均不算最终链接，iOS 工具链将 `CMAKE_TRY_COMPILE_TARGET_TYPE` 设为 `STATIC_LIBRARY`，其配置探测不能代替这次链接。 |

这里的“零 ABI 动作”指零 **Pangu 自制 AbiProfile** 命令、进程和派生状态；不禁止 CMake 首次配置时正常的编译器 ABI 探测、Clang 头依赖扫描或原生链接。核对普通构建图和 configure 日志，两处都不能有旧工具。拆 hook 时应核对是否顺带改变 CMake 的 C++ module 扫描策略；A/B 必须一致，不为省步骤全局禁用未来有真实消费者的语言功能。

**性能只比较改造前后各自的默认 `ALL`。** 改造前使用设计基点 `9a123d6f70b8` 的原样构建，改造后使用候选提交；记录双方提交和命令。不另做“先拆 hooks”的参照，也不补回已删除的工具或测试以凑齐目标数。每个平台固定同一宿主、正式 preset、工具链、并行度、文件系统及缓存条件；保留 preset 的测试设置，不临时关测试或打开 Unity/PCH。

每个版本至少做三轮。每轮用独立新构建目录：先计 `configure`，再从 `cmake --build` 启动到默认构建完成计 `ALL`（不指定 `--target`），另列同一轮 `configure + ALL` 的总耗时。记录原始值和中位数；CTest 单独运行、单独报告，不计入三项。只有首次默认 `ALL` 的构建耗时用于百分比门槛：若改造后中位数超过改造前中位数的 **1.10 倍**，暂停合入并定位。configure 变快不能抵消 `ALL` 的回退。

无改动、单文件增量及多个测试 EXE 共用依赖按上表的**实际动作**验收，不套用 10% 门槛。零额外 AbiProfile 动作仍是硬门槛。可用 `ninja -d explain`、详细命令或 `.ninja_log` 定位；诊断不接入日常构建。

## 6. 生效顺序与完成定义

**本节记录本期已执行的迁移顺序。** 先提交替代 ADR，逐项写明废止 ADR-0009 的逐 TU 证明及同树 STATIC 校验，修订 ADR-0010 对旧策略的引用，并列明仍有效的契约。替代 ADR 落地前不拆旧实现。随后按第 2、4 节移除普通构建和 Tests 的 AbiProfile 依赖、删除旧代码，再按第 5 节验收；同步 `wiki/foundation/{build-system,abi,abi-profile-fingerprint,third-party}.md`、相关核心规范及测试说明。旧 schema/vectors 如仅作历史资料则明确归档；不得继续作为当前生产协议。替代 ADR 须按第 3 节明确 ADR-0010 的适用范围：独立交付的原生插件保留装载前可信预检；与宿主同树、同配置的配套插件免 AbiProfile 预检。ADR-0011 的行为契约及 ADR-0012 的宿主本地 BuildRoot 不变。

**后续交付不阻塞本期。** 有真实 SDK 导出/导入或独立插件消费者后，另行冻结最小格式与信任来源，并用两套独立工程验证：配置匹配可用，错配能指出字段，缺失、旧格式及文件替换均拒绝。仅修改描述工具不得使未改动业务对象失效。未具备这些条件就不宣称支持该二进制边界。本期不改已有平台编译器选择，不实现 SDK 发布器、Loader 或新 BuildTools 框架。

依据：[构建规范](../../../wiki/foundation/build-system.md) · [第三方接入](../../../wiki/foundation/third-party.md) · [ABI 原决策](../../../wiki/decisions/0009-abi-profile-fingerprint.md) · [统一模块入口](../../../wiki/decisions/0010-unified-cpp-module-entry.md) · [测试维护规范](../../../Tests/README.md) · [Ninja 的重建与头依赖](https://ninja-build.org/manual.html) · [CMake 4.0 文件生成规则](https://cmake.org/cmake/help/v4.0/command/add_custom_command.html)。

## 7. 本期实施状态（2026-11-02）

- [ADR-0013](../../../wiki/decisions/0013-native-build-abi-boundary.md) 先于构建实现提交；历史 strict-v1 已不再是日常构建约束。
- 生产 CMake 和 Tests 已移除旧 AbiProfile launcher、guard/collect、逐 TU 收据、bootstrap 与默认审计；JSON 依赖与原生 Module/Catalog 测试保留。
- 五平台均完成至少三轮默认 `ALL` 前后比较，全部满足 `B/A ≤ 1.10`；Windows/Linux/macOS 共有 **39/39** 个 CTest 通过，Android/iOS 已验证最小程序的目标链接。原始样本、命令和实际 checkout 差异详见[验收记录](../plans/2026-11-02-native-build-abi-boundary-validation.md)。
- 本期仅实现**原生构建路径及交付边界政策**，未实现独立 SDK/Plugin 配置生成器、可信 Loader、代码签名或设备运行验证；这些仍属后续设计。
