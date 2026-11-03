# BuildTools — 按需构建工具

此目录用于未来真正参与源码生成或资产预处理的宿主构建工具，不是 Pangu Runtime Module，也不属于 `Pangu::` 命名空间。

**当前没有需要在普通构建中启动的自制 BuildTool。** 原 `AbiProfile`、宿主 Bootstrap、Compiler Launcher、Guard/Collect 与逐 TU 收据已按 [ADR-0013](../wiki/decisions/0013-native-build-abi-boundary.md) 退役。

代码生成工具出现真实消费者后，再由相应 CMake target 显式声明 `OUTPUT`、`DEPENDS` 和必要的 `DEPFILE`；资产处理与打包仅由消费者或显式命令触发。不提前建立通用 Bootstrap、发布缓存、隐式 `ALL` 或 `POST_BUILD` 流水线。

独立交付 SDK/插件的下一版兼容描述另行设计，不纳入普通 CMake + Ninja 增量构建。
