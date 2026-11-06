# 测试分类与运行契约

> 状态：Partially Frozen（分类、运行集合与隔离契约已确定；平台验收按实际结果记录）

测试按 `boundary.unit`、`boundary.integration`、`boundary.system` 标明验证边界，按 `area.*` 标明领域，按 `owner.engine` 或 `owner.project` 标明来源。显式启用且其所属方允许测试的 Plugin 沿用来源方 owner，附带 `plugin.*`；资源要求用 `requires.*` 标识。`run.fast`、`run.precommit`、`run.full` 是累计集合，不是互斥类别。回归与契约可叠加 `type.*`。

GoogleTest 按用例登记；CTest 工作目录和 XML 输出位于宿主 BuildRoot 中隔离的测试目录。直接批量运行时也须指定 BuildRoot 内工作目录。并发仅保证单一验证会话内经过隔离的测试；不得同时运行多个共享同一 BuildRoot 的 configure、build 或 test 会话。嵌套构建应计入处理器预算。

桌面使用根 preset 的 `<platform>-fast`、`<platform>-precommit`、`<platform>-full` workflow；依次配置、构建和执行。正式 Engine full 需要 BUILD_TESTING、PANGU_BUILD_ENGINE_TESTS 均开启；接入 Engine 测试时始终配置 Toolchain 探针。fast 60 秒及 precommit 5 分钟是测试执行优化目标，构建单独记录，超时不删除覆盖。空测试选择应失败。详细命令和维护约定见 Tests README。

BuildTools 日常 bootstrap 与独立宿主测试使用不同构建目录和 cache；根 full 调度独立工具测试，独立 CTest 可单独检查运行能力，不应将两次执行合并计数。Benchmark 默认关闭，独立于正确性测试开关；dry run 仅验证可运行，不能替代性能测量。正式测量记录源码及依赖版本、工具链、配置、宿主负载、原始样本和离散程度，运行时避免其他本机构建和测试。

Android/iOS 交叉编译只保留 Toolchain 编译探针，不发现或执行目标 GoogleTest；最终链接和独立 Project Game/Editor、SHARED 部署需显式执行专项检查。链接和宿主检查均不代表设备运行。三桌面、移动端和基准的当前验收范围以验证记录为准。

结果须区分首次与重跑、跳过及未验证范围。新测试替代旧场景时核对触发条件、关键断言和失败路径，不以测试数量替代等效性证明。正式测试和可变产物的维护规则见 [Tests README](../../Tests/README.md)。
