# flecs

`Repo/` 使用 flecs `v4.2.0`，固定在 `6566d4b7bdc597d0b74d5e0a4104d4ae6ac49402`，许可证为 MIT。首次检出项目或切换依赖版本后，在仓库根运行 `git submodule update --init --recursive`。[上游源码](https://github.com/SanderMertens/flecs/tree/6566d4b7bdc597d0b74d5e0a4104d4ae6ac49402) · [许可证](Repo/LICENSE)

`PanguThirdPartyFlecs` 以目标平台的 Clang 和 C99 编译 `Repo/src/` 下的 `.c` 文件，通过 `ThirdParty::Flecs` 向 C/C++ 消费者公开 `Repo/include`。构建使用本地子模块的拆分源码与头文件，配置阶段无需联网。上游 `distr/flecs.h` 默认定义 `flecs_STATIC`，因此这里使用拆分头文件的导入与导出标注。

生产目标在同一构建图只生成一份 PIC 库。Monolithic 构建 STATIC 库并公开 `flecs_STATIC`，Modular 构建 SHARED 库并私有定义 `flecs_EXPORTS`。平台系统库由该目标链接。可选的 Release Monolithic 断言基准另外构建私有对照归档，只在独立的 Off 基准进程中使用。[断言基准](../../../Tests/ECS/README.md#release-断言对照基准)

以下定义同时用于 C 实现与公开头文件，以保持两侧配置一致。

- `FLECS_CUSTOM_BUILD` 启用受控的 addon 集合。
- `FLECS_CPP` 编译 C++ 绑定所用的 C 实现，并从 `<flecs.h>` 提供 C++ API。
- `FLECS_MULTI_WORLD` 支持多个 World 的 C++ 组件 ID。
- `FLECS_PIPELINE` 启用系统调度，并由上游依赖接通 SYSTEM、FRAME 与 CACHED_QUERIES。
- `FLECS_LOG` 输出上游断言消息，`FLECS_KEEP_ASSERT` 在 Release 仍保留上游断言。
- `FLECS_CPP_NO_AUTO_REGISTRATION` 要求显式登记用户组件。
