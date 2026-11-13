# 测试分类与运行契约

> 状态：Partially Frozen（分类、运行集合与隔离契约已确定；平台验收按实际结果记录）

测试按 `boundary.unit`、`boundary.integration`、`boundary.system` 标明验证边界，按 `area.*` 标明领域，按 `owner.engine` 或 `owner.project` 标明来源。显式启用且其所属方允许测试的 Plugin 沿用来源方 owner，附带 `plugin.*`；资源要求用 `requires.*` 标识。`run.fast`、`run.precommit`、`run.full` 是累计集合，不是互斥类别。回归与契约可叠加 `type.*`。

GoogleTest 按用例登记；CTest 工作目录和 XML 输出位于宿主 BuildRoot 中隔离的测试目录。直接批量运行时也须指定 BuildRoot 内工作目录。并发仅保证单一验证会话内经过隔离的测试；不得同时运行多个共享同一 BuildRoot 的 configure、build 或 test 会话。嵌套构建应计入处理器预算。

桌面使用根 preset 的 `<platform>-fast`、`<platform>-precommit`、`<platform>-full` workflow；依次配置、构建和执行。正式 Engine full 需要 BUILD_TESTING、PANGU_BUILD_ENGINE_TESTS 均开启；接入 Engine 测试时始终配置 Toolchain 探针。fast 60 秒及 precommit 5 分钟是测试执行优化目标，构建单独记录，超时不删除覆盖。空测试选择应失败。详细命令和维护约定见 Tests README。

BuildTools 日常 bootstrap 与独立宿主测试使用不同构建目录和 cache；根 full 调度独立工具测试，独立 CTest 可单独检查运行能力，不应将两次执行合并计数。Benchmark 默认关闭，独立于正确性测试开关；dry run 仅验证可运行，不能替代性能测量。正式测量记录源码及依赖版本、工具链、配置、宿主负载、原始样本和离散程度，运行时避免其他本机构建和测试。

Android/iOS 交叉编译只保留 Toolchain 编译探针，不发现或执行目标 GoogleTest；最终链接和独立 Project Game/Editor、SHARED 部署需显式执行专项检查。链接和宿主检查均不代表设备运行。三桌面、移动端和基准的当前验收范围以验证记录为准。

结果须区分首次与重跑、跳过及未验证范围。新测试替代旧场景时核对触发条件、关键断言和失败路径，不以测试数量替代等效性证明。正式测试和可变产物的维护规则见 [Tests README](../../Tests/README.md)。

## 基础诊断与 Log 验收

正式入口位于 Tests/Foundation、Tests/Log、Tests/Module 与 Tests/EngineHost，后端、报告、Fatal、格式子集、拒收/FIFO、文件分区、Flush/Stop、Host 和真实卸载均保留在 Full。Assert 使用独立 Release/Debug × ON/OFF 子图，含第三方宏隔离与精确原生 Fatal 退出监督。移动通过显式 CrossCompileTests 验证 LogSystem/FailureReport/Check 的真实最终消费、ARM64 和动态依赖；Android caller 与实现共用类型声明。不运行交叉目标，最终矩阵、旧结果复用和未验证范围见[基础诊断验收](../../docs/superpowers/plans/2026-11-12-foundation-diagnostics-validation.md)。

独立 Project 的 Base 与 EngineOptIn Full 分别登记为 SourcePluginProject 和
SourcePluginEmbeddedEngine，各有独立冷根、原 900 秒监督和完整原断言。配置恢复与无改动构建
是不同契约：ProjectSwitch 同时验证实际原生产物配置、运行结果与 no-op，不能只看编译数据库。

macOS 的 Owner 验收分为独立 Base、Project、Engine、Provider 四项；原
SourcePluginTestOwnership 为 Base，另外登记 SourcePluginTestOwnershipProject、
SourcePluginTestOwnershipEngine、SourcePluginTestOwnershipProvider。每项从当前正式输入完整准备，
单选不依赖其他场景或历史日志，Full 必选且保留全部原断言。Base/Project/Provider 为900秒，
Engine完整准备为1500秒；Windows/Linux默认All及原块顺序不变，移动测试OFF图不进入这些注册。
仅Darwin的MyGamePluginCollaborationTests显式发现30秒，业务仍60秒，其他目标/平台默认发现5秒。
准备预算、发现预算和业务deadline分别记录，冷成本、原超时与OS关联的证据边界见正式验收。

## 双模式 ModuleManager 验收

正式矩阵以[当前验收记录](../../docs/superpowers/plans/2026-11-06-module-manager-link-modes-validation.md)为准。桌面默认 Modular；Monolithic 的旧有效 Module 覆盖保留，Modular 的 ModuleRegression 在独立子图自动执行，子标记只防递归。ModuleLinkModeMatrix/Switch 验证父模式、目标类型、非法值/中途改值和往返；真实 DLL 的 RTTI/Pin/Scope/物理卸载不能由静态归档替代。同根模式切换须等上一会话结束，最终 format/tidy/build/CTest 也串行。

Android/iOS 两模式除正式根编译，还须显式运行 SourcePluginProject/CrossCompileTests.cmake，检查真实 Runtime/业务 Query、动态依赖、目标架构和最小宿主最终链接；不运行交叉目标 GTest，不宣称设备 RTTI/动态卸载、打包或签名。两模式六场景 benchmark 单独显式开启/dry run，完成恢复 OFF，新增常驻根成本保留在原计时区间，不混旧基线。
