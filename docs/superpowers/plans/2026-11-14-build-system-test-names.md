# 构建系统测试命名实施计划

> 执行方式：superpowers:executing-plans，当前共享工作区内实施并统一评审；用户随后明确要求提交，不推送或合并。

**目标：** 完成用户指定的四文件重命名及相关 CTest 职责命名。

**架构：** 注册名与脚本路径显式映射，避免通过名字推导旧文件名；运行属性和覆盖保持一致。

**技术：** CMake 4.0+、CTest、Python、PowerShell。

**规格：** [命名设计及完整映射](../specs/2026-11-14-build-system-test-names-design.md)。

## 全局约束

- 仅修改指定文件名及相关名称引用；内部函数、夹具项目和真实测试断言不为统一前缀而重命名。
- 使用同一共享源码和现有 Windows BuildRoot；不新增永久测试验证机械重命名。
- 历史验收保留旧名称，当前说明提供映射。

## 评审关注点

- Project 脚本内的嵌套分类筛选必须精确匹配点分名称。
- 分类检查同时识别 PluginIntegration、ProjectIntegration、TestInfrastructure 三个前缀。
- TestInfrastructure 注册名与实际脚本名显式对应，不把 CTest 名称当作路径。
- Darwin 专属 Owner 三场景及 Engine 1500 秒设置保持有效。
- 比较全部注册的名称、命令和属性，防止漏项或改变累计运行集合。

## Task 1：重命名与引用

- [x] 保存迁移前 CTest JSON；按规格移动四个文件。
- [x] 更新两个 CMakeLists.txt、include、嵌套筛选和分类断言。
- [x] 同步当前 README/wiki；保留历史正文并添加映射入口。

## Task 2：验证与收尾

- [x] 正式 Windows preset 重新配置，核对完整 CTest JSON 的映射等价性。
- [x] 运行既有定向回归及精确筛选验证，检查跨平台条件注册。
- [x] 统一只读评审，归档实际结果与未验证范围，清理本计划临时目录。

## 验收摘要

2026-11-14，基于 `2035844a48abbff8d87aa95ea36b14ec37465723` 加本次未提交改动。
四个文件全部迁移，Windows 的 21 个相关注册改名；Darwin 另有三个 Owner 分场景名称同步。
实施验收时未提交；用户随后要求提交，最终提交以 Git 历史为准。未推送或合并。

| 验证 | 结果 |
| --- | --- |
| `cmake --preset windows` | 通过 |
| `cmake --build --preset windows-full` | 通过，准备既有正式测试依赖；不等同于运行 Full |
| 完整注册 JSON 对比 | 315 → 315，全部映射等价；仅允许名称、四个脚本路径、名称派生测试根及 SHA256 工作目录变化 |
| 既有定向回归 | 首轮 13/13 通过，247.42 秒；包含 PluginIntegration 全部5项、ProjectIntegration.EngineRevision 和 TestInfrastructure 全部7项 |
| 精确分类筛选 | `^TestInfrastructure[.]ScriptClassification$` 只匹配预期的一项；分类回归实际执行通过 |
| Windows/Linux/Darwin 条件登记比较 | Windows 本机最小注册工程按三个系统条件展开，分别21/20/23项与旧注册映射等价；Darwin Owner 的 Project/Engine/Provider 超时保持900/1500/900秒 |
| 独立只读评审 | 无 Critical、Important 或 Minor |
| 差异检查 | `git diff --ignore-submodules --check` 通过 |

定向命令：

```powershell
ctest --preset windows-full -R '^(PluginIntegration[.]|ProjectIntegration[.]EngineRevision$|TestInfrastructure[.])' --no-tests=error --output-on-failure
```

沙箱内首次配置因宿主工具规范化路径时访问被拒而未完成，自动批准后在沙箱外执行同一正式
preset 成功；不改变源码路径或平台 BuildRoot。条件登记比较首次因模拟工程新旧二进制根不同
报告差异，核对 JSON 后仅归一化该路径，三种条件均通过。

没有修改 C++ 源码，因此未执行 clang-format/clang-tidy。没有执行完整引擎 Full、Project Git 锁
矩阵、Linux/macOS 实际构建运行或移动平台验收；条件展开只验证登记分支，不代表跨平台运行。
历史设计和验收保留旧名称，当前 README 保存完整迁移映射，原目录规格标明名称约定已被替代。

注册清单、映射、配置/构建/测试日志及评审摘要归档到 `Build/Windows/Tests/BuildSystemTestNamesMigration/`。
确认相关工具和评审已完成后，清理本计划 `.superpowers/sdd/2026-11-14-build-system-test-names/`。

## Task 3：后续两项范围命名

基于 `a9a786c`，按用户确认的名称继续整理；用户在验证后明确要求提交，不推送或合并。
规格见原设计文档的“后续补充”，前述验收保留第一次实施时的结果。

- [x] 保存当前完整注册基线，将 StaticCatalogIntegrationTests 改为 HostModuleConsumptionTests、ScriptClassificationTests 改为 TestClassificationTests。
- [x] 更新两个注册入口、分类必选列表、Project 嵌套筛选和当前 README 的映射；历史验收不改写。
- [x] 正式 Windows 配置/构建、完整注册映射比较和 TestInfrastructure.TestClassification 回归通过；核对 Host 脚本内容未变。
- [x] 只读评审、记录验证边界并归档，确认无运行依赖后清理本次 followup 临时目录。

### Task 3 验证结果

基于 `a9a786c` 加本次未提交改动，`cmake --preset windows`、`cmake --build --preset windows-full`
通过。完整 CTest 注册315→315，只有两个约定名称、两个脚本路径及名称派生工作根变化；全部属性保持一致。
Host 脚本 Git blob 仍为 `5a09199bee0c931badfaa5827f05979dc98a0709`，内容未变；分类脚本仅替换两个必选 CTest 名称。

```powershell
ctest --preset windows-full -R '^TestInfrastructure[.]TestClassification$' --no-tests=error --output-on-failure
```

上述回归首轮1/1通过，用例11.72秒，CTest总计12.21秒；精确筛选只匹配预期的一项。
没有重新运行 Host Module 两模式真实运行矩阵、完整 Project/Full 或跨平台矩阵；本轮不改变这些测试的行为。
没有修改 C++，不重复执行 clang-format/clang-tidy。
独立只读评审无 Critical、Important 或 Minor；差异检查通过。日志及注册对比归档到
`Build/Windows/Tests/BuildSystemTestNamesMigration/Followup/`，本次 followup 临时目录已清理。
用户随后要求提交，最终提交以 Git 历史为准；未推送或合并。
