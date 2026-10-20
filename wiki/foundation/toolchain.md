# Pangu 工具链与元数据工具

> 状态：Partially Frozen（编译器政策已确定；工具实现与集成待定）
> 更新：2026-10-20

## 已确定

1. 引擎使用 **C++23**，全部目标平台统一使用 **Clang**。
2. **Windows 使用 Clang + MSVC STL；Linux、macOS、Android、iOS 使用 Clang + `libc++`**。全平台不使用 `libstdc++`。标准库的具体版本、ABI 选项和链接方式需按目标 Toolchain 明确。
3. **不 Fork llvm-project**，避免长期维护编译器分叉；以官方 Clang 发行物和可用扩展机制为基础。
4. 使用 **CMake** 作为构建底座；编译器与构建系统是两个不同的职责。
5. 元数据声明采用 `pga` 属性命名空间，不使用 `PG_CLASS`、`PG_PROPERTY` 等宏作为反射声明语言。
6. 讨论中使用 `PanguClang`、`PanguMeta` 指代语义提取/元数据处理的工具职责；名称并不证明它们已经是两个存在的可执行程序。

## 计划承担的工具职责（非已冻结 API）

| 职责 | 预期结果 |
| --- | --- |
| Clang 语义提取 | 取得 C++ 类型、成员、属性及源位置信息 |
| Meta 处理 | 校验、规范化元数据并生成必要的注册与序列化辅助代码 |
| 构建集成 | 与 CMake 的目标及增量依赖衔接 |
| ABI 采集 | 为 Module/镜像记录实际编译配置和兼容性信息 |
| 诊断反馈 | 在 C++ 代码位置报告属性滥用、配置错误或不可支持的声明 |

以上是职责边界方向，不是已经确定的进程边界、输入输出协议或生成代码格式。

## 不应假定

- 不因为 Clang 统一，就假定五个平台的系统 ABI、C++ 标准库、目标格式与 SDK 可以互换。
- 不因为选择 Clang 属性，就假定任意 `[[pga::...]]` 拼写可在当前官方 Clang 中直接被识别并参与生成；具体属性注册或提取路径要先验证。
- 不要求所有代码都具备反射元数据；仅对显式声明和确实需要生成代码的结构处理。
- 不提前确定 `PanguClang` 是 Clang Plugin、LibTooling 工具还是独立程序；这属于待冻结设计。

## 待冻结

Clang/LLVM 支持版本范围、Windows MSVC STL 与其余四平台 `libc++` 的具体版本和 ABI/运行库配置、各平台 SDK、Toolchain 文件、编译选项、Clang Plugin/LibTooling 策略、PanguClang 与 PanguMeta 职责和产物边界、Metadata Schema、增量构建和跨平台测试矩阵。

相关文档：[C++ 规范](cpp-guidelines.md)、[构建系统](build-system.md)、[Reflection / Meta](../core/reflection-meta.md)。
