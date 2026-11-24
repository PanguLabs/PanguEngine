# Wayland 窗口依赖

`ThirdParty::WaylandWindow` 是 Linux 图形 Application 的构建接口。它通过 pkg-config 链接系统提供的 [wayland-client](https://wayland.freedesktop.org/) 和 [libdecor](https://gitlab.freedesktop.org/libdecor/libdecor) 0.2.2 或更新版本。

Ubuntu 构建需要 `libwayland-dev` 和 `libdecor-0-dev`。GNOME 运行时使用 `libdecor-0-plugin-1-gtk` 提供客户端装饰。构建、运行及验收步骤见 [Toolchain 测试说明](../../../Tests/Toolchain/README.md)。
