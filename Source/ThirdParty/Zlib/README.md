# zlib

- 来源：<https://github.com/madler/zlib>
- 版本：1.3.2；固定提交 `da607da739fa6047df13e66a2af6b8bec7c2a498`。
- 许可证：zlib，原文见 `Repo/LICENSE`。
- 获取：`git submodule update --init --recursive`；配置/构建阶段不联网、不更新源码。
- 接入：`ThirdParty::Zlib` 为上游 `zlibstatic` 的别名；只构建 STATIC/PIC，关闭上游测试与共享库，contrib 使用上游默认关闭，无上游补丁。

当前为 PanguMetaTool 同图私有普通静态目标，不进入 Engine 主图，不安装到单独 SDK 前缀。外层校验固定 HEAD；工具读取 LLVM 导出元数据后匹配宿主 CRT。上游源码保持不变，实际验证见 [普通工具构建计划](../../../docs/superpowers/plans/2026-11-14-pangumeta-normal-build.md)。
