# zstd

- 来源：<https://github.com/facebook/zstd>
- 版本：1.5.7；固定提交 `f8745da6ff1ad1e7bab384bd1f9d742439278e99`。
- 许可证：BSD-3-Clause 或 GPL-2.0；本接入采用 BSD-3-Clause，原文见 `Repo/LICENSE`。
- 获取：`git submodule update --init --recursive`；配置/构建阶段不联网、不更新源码。
- 接入：`ThirdParty::Zstd` 为上游 `libzstd_static` 的别名；只构建 STATIC/PIC，关闭程序、上游测试、多线程和旧格式支持，汇编沿用上游平台检测，无上游补丁。

当前为 PanguMetaTool 同图私有普通静态目标，不进入 Engine 主图，不安装到单独 SDK 前缀。外层校验固定 HEAD；工具读取 LLVM 导出元数据后匹配宿主 CRT。上游源码保持不变，实际验证见 [普通工具构建计划](../../../docs/superpowers/plans/2026-11-14-pangumeta-normal-build.md)。
