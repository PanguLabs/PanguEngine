# 三角形着色器

`Graph.vert.fsl` 和 `Graph.frag.fsl` 来自 The Forge v1.63 的 `Examples_3/Unit_Tests/src/03_MultiThread/Shaders/FSL/`，上游提交为 `7443530ef7e888771f307de74b7872becfd64450`。本目录保留上游版权声明与 [Apache License 2.0](LICENSE)。两阶段使用二维裁剪空间位置与线性 RGBA，沿用示例中的着色器实现。清单按本项目的顶点格式选择两个阶段，并引用固定 Forge 的 `defaults.h` 生成 D3D12 默认图形根签名。

启用 `PANGU_BUILD_TRIANGLE_SHADER_CHECK` 后可构建 `PanguTriangleShaders`。应用使用 `pangu_add_application(TARGET <name> KIND GRAPHICS TRIANGLE_SHADERS SOURCES <files...>)` 将同一构建接入 `<name>TriangleShaders`。字节码存放在各自 BuildRoot 的 `Generated/TriangleShaders/<shader-target>/<config>/CompiledShaders/`，保留 FSL 的平台子目录。桌面应用在可执行文件旁部署 `CompiledShaders`，Android 打包从生成目录取用资源。

资源构建使用宿主 Python 3 和固定 Forge 的 FSL 工具。Windows D3D12 使用 `dxc.exe`，Vulkan 和 Android Vulkan 使用宿主 `glslangValidator`。可用 `PANGU_FSL_COMPILER_DIRECTORY` 显式指定相应工具所在目录。macOS 和 iOS 分别通过 `xcrun --sdk macosx metal` 和 `xcrun --sdk iphoneos metal` 使用官方工具。
