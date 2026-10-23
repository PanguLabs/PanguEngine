# ThirdParty 接入与功能封装

> 状态：Partially Frozen（目录组织、固定提交和功能封装原则已确定；逐库补丁与完整部署接线按需要实现）
> 更新：2026-10-23
> 相关：[构建系统](build-system.md)、[工具链](toolchain.md)、[Plugin / Module](../core/plugin-module.md)、[ABI](abi.md)

## 源码与构建入口

第三方源码统一通过 Git submodule 引入：

```text
Source/ThirdParty/<DependencyName>/
  Repo/                  # 官方仓库或明确维护的 fork，Git submodule
  CMakeLists.txt         # PanguEngine 维护的构建接入规则
  README.md              # 来源、许可证、版本说明、构建选项与验证要求
  Patch/                 # 按需建立，保存对上游源码的补丁
```

`DependencyName` 是稳定的依赖接入名称，不强制与远程仓库 basename 相同。
例如 `NlohmannJson` 对应上游 `nlohmann/json`；一个 SDK 可以包含多个实际库。
`Repo/` 内的文件归上游仓库管理，外层文件归 PanguEngine 管理。

- submodule 固定到明确提交，由主仓库 gitlink 记录，不以浮动分支决定构建输入。
- 首次检出使用 `git clone --recurse-submodules <PanguEngine URL>`；已有工作区执行 `git submodule update --init --recursive`。
- 切换主仓库版本后重新执行初始化/更新命令，使源码与该版本 gitlink 一致。
- CMake 配置阶段不联网、不执行 submodule 更新；缺失必要文件时报告初始化命令并失败。
- 升级先检查 `Repo/` 的本地修改，再检出目标提交、复核许可证及补丁、执行验证，最后更新主仓库 gitlink。不得自动覆盖本地修改。

## 项目构建配置

外层 `CMakeLists.txt` 最低要求 CMake 4.0，并在根工程已校验的平台与工具链上下文中定义 Target。
编译型第三方 Target 使用项目指定的编译器、标准库与所需 ABI 配置，逐库显式设置必要选项；
不将引擎警告策略无差别施加到上游源码，也不直接调用 `pangu_configure_module()` 把依赖标记为引擎 Module。

可以由外层直接列出源码创建 STATIC/SHARED Target；需要上游构建逻辑时，也可设置上游选项后
使用 `add_subdirectory(Repo)`，再对实际 Target 调整必要配置。不得仅因同处一棵 CMake 构建树，
便假定上游没有覆盖项目默认值。header-only 库使用 INTERFACE Target，预编译 SDK 使用 IMPORTED Target；
其工具链兼容性必须另行核验，不能把预编译库视为已按本项目配置重新编译。

构建选项、Include、宏和链接依赖均通过 Target 表达，不使用全局 Include/Link 目录。
源代码获取方式与功能接口封装分开处理；submodule 不自动提供 ABI 兼容、安全卸载或状态迁移。

## 功能 Module 与第三方 API

第三方 API 的使用集中在负责接入的功能 Pangu Module 的私有实现中，其他引擎模块通过该功能
Module 的引擎接口使用能力。公共接口不暴露第三方类型和头文件；接入方负责错误转换、
对象/分配器所有权、任务与回调撤销，以及所需代码的驻留。

一个功能 Module 可以封装多个第三方依赖，不要求一个依赖创建一个 Pangu Module，
也不为每个依赖强制增加 Service、Registry 或运行时注册。独立宿主工具与依赖验证测试
可以直接使用第三方 API；当前 ABI Profile 工具对 JSON 的使用属于这个范围。

这一约束不改变一个 Pangu Module 对应一个实际 STATIC/SHARED Library Target 的规则。
ThirdParty Target 与 Plugin/Module 的身份、注册和生命周期契约仍是不同职责。
`PRIVATE` 链接关系有助于控制传播，但不能代替公共头文件检查和生命周期验证。

## Patch 管理

没有上游修改时不建立空的 `Patch/`。确需修改时：

1. 在 `Patch/` 保存标准 Git diff，使用有序文件名，并在依赖 README 中明确应用顺序、对应上游提交、原因和上游反馈位置。
2. 外层构建接入在 `Build/<Platform>/` 内准备源码副本，对该副本按明确顺序应用补丁；不要在配置/构建时直接修改 `Repo/`。
3. 补丁不匹配应终止配置，不得静默跳过。基线提交或补丁内容变化后重新准备副本，避免重复应用或沿用旧源码。
4. 验证补丁后的源码和功能，并检查 `git -C <Dependency>/Repo status --short` 保持干净。

当前 NlohmannJson 无补丁；本次不预建通用补丁应用工具。第一个需要补丁的依赖必须随接入实现上述步骤，
并加入补丁不匹配与重复配置的验证。若通过 fork 保存修改，也应固定明确提交并记录与上游的差异。

## 当前接入

`NlohmannJson/Repo` 固定到官方 v3.12.0 正式发布版，外层直接暴露 `single_include` 并设置
`JSON_USE_GLOBAL_UDLS=0`。该依赖只供宿主 ABI Profile 工具及测试使用，
没有新增 Runtime Module，没有启用上游测试，也没有在配置阶段执行下载。
