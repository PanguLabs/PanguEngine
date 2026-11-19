# CMake 命名规范

> 状态：Partially Frozen（继承构建系统已冻结的 Target 命名边界；其余为新增和修改 CMake 代码的统一命名规范，存量逐步对齐）
> 更新：2026-11-19
> 适用：PanguEngine 自有的 `CMakeLists.txt`、`cmake/*.cmake`、BuildTools 和 Tests 中的 CMake 脚本；第三方上游脚本遵循其原有约定。
> 关联：[构建系统](build-system.md)、[ThirdParty 接入](third-party.md)、[CMake 官方文档](https://cmake.org/cmake/help/v4.0/)

本规范覆盖八类名称：Command、Function、Variable、Cache Variable、Property、Target、Keyword 和 Generator Expression。规范中的 **MUST / SHOULD / MAY** 含义见 [wiki 索引](../README.md)。本规范只约束命名，不改变已有 CMake API、Target 身份或构建行为。

## 总览

| 类别 | 项目命名约定 | 示例 |
| --- | --- | --- |
| 1. Command（CMake 命令） | 内置命令用全小写 `snake_case`，官方模块命令沿用文档拼写 | `add_library`、`FetchContent_Declare` |
| 2. Function（自定义函数） | 对外 `pangu_snake_case`；内部辅助 `_pangu_snake_case` | `pangu_configure_module` |
| 3. Variable（普通变量） | 自有变量用 `lowerCamelCase`，在满足用途的最小作用域内定义，已有共享接口保留原名 | `targetName`、`exportDirectory` |
| 4. Cache Variable（缓存变量） | 项目自有 `PANGU_UPPER_SNAKE_CASE` | `PANGU_BUILD_ENGINE_TESTS`、`PANGU_LINK_MODE` |
| 5. Property（属性） | 内置原样；自定义 `PANGU_UPPER_SNAKE_CASE` | `CXX_STANDARD`、`PANGU_EXPORT_MACRO` |
| 6. Target（目标） | 自有实际目标 `PascalCase`；受控别名 `Namespace::PascalCase` | `PanguFoundation`、`Pangu::Foundation` |
| 7. Keyword（关键字） | 内置原样；自定义命名参数 `UPPER_SNAKE_CASE` | `PRIVATE`、`SOURCE_DIR`、`EXPORT_MACRO` |
| 8. Generator Expression（生成器表达式） | 保留 CMake 官方操作符拼写；参数按其自身类型约定 | `$<CONFIG:Debug>`、`$<TARGET_FILE:PanguFoundation>` |

CMake 内置命令、属性、关键字与 Generator Expression 操作符**不是项目可重命名的接口**；规范约束它们的书写与使用。普通变量和 Cache Variable 是按存储方式区分的类别，而大写前缀本身**不表示**变量一定是缓存项。

## 1. Command：内置命令与官方模块命令

- CMake 内置命令 **MUST** 沿用官方的全小写 `snake_case` 拼写，例如 `add_library()`、`target_link_libraries()` 和 `set_property()`。
- CMake 官方模块提供的命令 **MUST** 沿用模块文档的拼写，例如 `FetchContent_Declare()` 和 `ExternalProject_Add()`。CMake 以不区分大小写的方式查找命令，统一拼写便于阅读和检索。
- `file(TO_CMAKE_PATH ...)` 中的 `file` 是 Command，`TO_CMAKE_PATH` 是该命令的 Subcommand；子命令同样沿用官方拼写。

## 2. Function：自定义函数

- 引擎面向其他构建子系统、Project 或 Plugin 提供的公开函数 **MUST** 使用 `pangu_` 前缀和小写 `snake_case`，优先采用动词加职责，例如 `pangu_add_*` 和 `pangu_configure_*`。
- 仅供同一脚本、子系统或测试组使用的辅助函数 **SHOULD** 使用 `_pangu_` 前缀和小写 `snake_case`。同组脚本可以跨文件共享这类函数，例如 Meta 测试共用的 `_pangu_configure_meta_consumer()`。前缀表达接口归属，实际可见性由 CMake 的加载和执行过程决定。
- **SHOULD** 选择能描述副作用的动词：`add` 创建、`configure` 配置、`initialize` 初始化、`resolve` 解析、`validate` 校验。
- `function()` 和 `macro()` 都可以定义可调用命令。确需定义 Macro 时使用同样的命名风格，但优先使用具有独立变量作用域的 Function。
- 不因 C++ 命名空间使用 `pg`，就把已有 CMake `pangu_` 函数改成 `pg_`。

```cmake
function(pangu_configure_module targetName)
    # ...
endfunction()
```

## 3. Variable：普通变量

- 新增或重命名的 Pangu 自有普通变量 **MUST** 使用 `lowerCamelCase`，包括函数形参、局部计算值和脚本中的临时状态，例如 `targetName`、`exportDirectory` 和 `engineStandalone`。
- 普通变量 **SHOULD** 定义在能覆盖全部读写方的最小作用域。函数内的计算值在 `function()` 内定义。顶层脚本中的一段临时计算可用 `block(SCOPE_FOR VARIABLES)`，通过 `PROPAGATE` 显式带出结果。同一目录的多个配置步骤都要使用的值可放在目录作用域。
- 共用脚本 **SHOULD** 通过函数形参和显式输出来传值，例如用 `set(... PARENT_SCOPE)` 返回结果。 `include()` 在调用方的变量作用域执行。 `add_subdirectory()` 开始处理子目录时会继承父目录的变量，只有需要向子目录传入某个值时才依赖这种继承。
- 已有共享变量接口 **MUST** 保留约定拼写，例如 `PANGU_ENGINE_SOURCE_DIR` 和 `PANGU_SOURCE_OWNER`。CMake 变量名区分大小写，重命名时需要同步所有读写方。
- 内置变量（`CMAKE_SOURCE_DIR`、`CMAKE_CURRENT_LIST_DIR`、`PROJECT_NAME` 等）必须原样引用；**MUST NOT** 为项目自定义变量占用 `CMAKE_`、`_CMAKE_` 等保留命名空间。
- `cmake_parse_arguments(PARSE_ARGV ... ARG ...)` 的解析结果 `ARG_EXPORT_MACRO` 等由解析前缀决定，不按局部变量命名规则强行改成驼峰；`ARGC`、`ARGV`、`ARGN` 等内置参数名保持原样。

```cmake
block(SCOPE_FOR VARIABLES PROPAGATE exportDirectory)
    set(generatedDirectory "${CMAKE_CURRENT_BINARY_DIR}/Generated")
    set(exportDirectory "${generatedDirectory}/Export")
endblock()
```

## 4. Cache Variable：缓存变量

- 自有、需要跨次配置保留的缓存选项 **MUST** 使用 `PANGU_UPPER_SNAKE_CASE`。
- 含义清楚的前缀 **SHOULD** 优先使用：`PANGU_BUILD_*`（是否构建某类产物）、`PANGU_ENABLE_*`（是否启用功能）、`PANGU_*_MODE`（模式）、`PANGU_*_ROOT` / `PANGU_*_DIR`（路径）。
- **MUST** 保留 CMake 和第三方已有缓存名称，如 `CMAKE_BUILD_TYPE`、`BUILD_TESTING`、外部 SDK 的既定选项。
- 是否属于 Cache Variable 取决于 `option()`、`set(... CACHE ...)` 等定义方式，**不能**仅凭全大写名字判断；不要为简单局部临时值创建 Cache。

```cmake
option(PANGU_BUILD_ENGINE_TESTS "Build engine tests" ON)
set(PANGU_LINK_MODE "Modular" CACHE STRING "Pangu module link mode")
```

## 5. Property：属性

- CMake 内置 Property **MUST** 使用官方名称：`CXX_STANDARD`、`POSITION_INDEPENDENT_CODE`、`DEFINE_SYMBOL`。
- Pangu 自定义 Property **MUST** 使用 `PANGU_UPPER_SNAKE_CASE`，例如 `PANGU_SOURCE_OWNER`、`PANGU_EXPORT_MACRO`、`PANGU_CONTEXT_INCLUDED`。
- **MUST** 区分 Variable 和 Property：`CMAKE_CXX_STANDARD` 是变量，`CXX_STANDARD` 是 Target Property。
- Property 名属于读写双方共享的契约，不能只修改 `set_property()` 而遗漏 `get_property()` / `get_target_property()` 等使用位置。

```cmake
set_property(TARGET PanguFoundation PROPERTY PANGU_SOURCE_OWNER engine)
set_target_properties(PanguFoundation PROPERTIES CXX_STANDARD 23)
```

## 6. Target：目标

**Target 的命名空间和归属以 [构建系统规范](build-system.md#cmake-target-命名与职责frozen) 为权威约束。**

- Pangu 自有实际 Target **SHOULD** 使用 `PascalCase`：`PanguFoundation`、`PanguBuildOptions`、`PanguMetaTool`。
- `Pangu::PascalCase` **MUST** 仅由 `Source/Runtime/` 下真实的 Pangu Module 定义；例如实际目标 `PanguFoundation` 对外暴露 `Pangu::Foundation`。
- 引擎管理的第三方构建接口 **MUST** 使用 `ThirdParty::PascalCase`：`ThirdParty::Flecs`、`ThirdParty::NlohmannJson`。上游实际 Target 名可按上游约定保留，通过 Alias / Imported Target 提供统一消费名称。
- Project、Plugin、Tests、BuildTools 和普通构建策略目标 **MUST NOT** 冒用 `Pangu::`；使用明确的普通目标名，如 `MyGameGameplay`、`FixtureEnginePhysics`、`PanguBuildOptions`。
- 已有外部 SDK 的 Imported Target（如 `LLVM::Support`）遵循对应 package 的约定，不擅自重命名其上游接口。

```cmake
add_library(PanguFoundation STATIC Private/Error.cpp)
add_library(Pangu::Foundation ALIAS PanguFoundation)
```

以上示例仅展示命名和目标关系；真实库类型由 `PANGU_LINK_MODE` 决定。

## 7. Keyword：关键字

- 内置命令的关键字 **MUST** 按官方拼写使用：`PRIVATE`、`PUBLIC`、`INTERFACE`、`PROPERTIES`、`PARSE_ARGV`。
- 自定义函数的命名参数 **MUST** 使用 `UPPER_SNAKE_CASE`：`SOURCE_DIR`、`RELATIVE_BINARY_DIR`、`EXPORT_MACRO`。
- 参数较多，或多个参数容易混淆时，**SHOULD** 优先使用 `cmake_parse_arguments()` 的命名参数，而不是过长的位置参数列表。
- 自定义 Keyword 是某个函数自行解析的参数文本，不是 CMake 全局保留字；`OWNER` 在 `pangu_add_owned_subdirectory()` 中就是这种参数。

```cmake
pangu_add_owned_subdirectory(
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Source/Runtime/Foundation"
    OWNER engine
    RELATIVE_BINARY_DIR Source/Runtime/Foundation
)
```

## 8. Generator Expression：生成器表达式

- **MUST** 使用 CMake 官方操作符的拼写：`$<CONFIG:Debug>`、`$<TARGET_PROPERTY:PanguFoundation,CXX_STANDARD>`、`$<BUILD_INTERFACE:...>`。
- 冒号后面的 Target、Property、构建配置或其他参数仍遵循其**自身**的命名规则；例如 `Debug` 不是 Generator Expression 操作符。
- Generator Expression 不是 CMake Function，也不能通过 `function()` / `macro()` 注册新的操作符。
- **MUST** 确认使用位置支持 Generator Expression；不要把 `${variable}`（配置时变量展开）与 `$<...>`（生成阶段及部分构建阶段按上下文求值）混为一谈。

```cmake
target_compile_definitions(
    PanguFoundation
    PRIVATE
        "$<$<CONFIG:Debug>:PANGU_FOUNDATION_DEBUG>"
)
```

## 兼容性与检查

- **MUST** 遵守已经冻结的 Target 命名/归属边界；不要借命名整理改变 Module、ThirdParty 或 Project 的构建身份。
- **SHOULD** 在新增代码中立即采用本规范；修改存量代码时仅在不破坏跨脚本接口、CMake Cache、Property 消费者、Preset 和测试依赖的前提下逐步统一。
- 存量的混合命名按兼容性约束逐步迁移。重命名前 **MUST** 核对全部读写方，包括动态构造的属性名和生成脚本中的引用。
- **MUST NOT** 为单纯命名调整批量重写第三方上游 CMake；对依赖提供稳定 Target Alias 即可。
- Review 时依次检查：内置名是否原样、函数前缀是否统一、变量是否按作用范围命名、缓存是否真需要持久化、自定义 Property 是否有 `PANGU_` 前缀、Target 命名空间是否合规、自定义 Keyword 是否全大写、Generator Expression 是否处于支持的上下文。

官方索引：[Commands](https://cmake.org/cmake/help/v4.0/manual/cmake-commands.7.html) · [Variables](https://cmake.org/cmake/help/v4.0/manual/cmake-variables.7.html) · [Properties](https://cmake.org/cmake/help/v4.0/manual/cmake-properties.7.html) · [Generator Expressions](https://cmake.org/cmake/help/v4.0/manual/cmake-generator-expressions.7.html)
