# fmt

来源为 [fmtlib/fmt](https://github.com/fmtlib/fmt)，固定 tag `12.2.1`、提交
`208219daff58f2a7438f6931cc2ddc358acf33ef`，由 `Repo/` 的 Git submodule 记录。
许可证为 MIT，完整许可及可选条款见 [Repo/LICENSE](Repo/LICENSE)。当前没有补丁。

适配层直接编译上游 `src/format.cc` 和 `src/os.cc`，提供静态库 `ThirdParty::Fmt`
及本树 `fmt::fmt`。使用所选平台的 Clang、C++23 和标准库，设置
`FMT_EXCEPTIONS=0`、`-fno-exceptions`、`-frtti`、PIC 和空 `DEBUG_POSTFIX`。
不执行上游 tests/examples/install，不修改 `BUILD_SHARED_LIBS`，不继承引擎警告策略。
这些普通翻译单元显式关闭 C++ Modules 扫描；共享 BuildRoot 在 Project/Debug/Release
往返时须恢复实际对象配置并保持无改动构建，正式回归检查对象及原生 CRT/Debug 信息。
配置阶段不联网；缺源码时显式要求初始化 submodule。

`ThirdParty::Spdlog` 只链接此 fmt。Log 原始文本后端没有调用 fmt 格式化函数；
后端无分配验证不能证明任意 fmt 格式、浮点或用户 formatter 无分配。

Log 首版受限前端位于 [LogFormat.h](../../Runtime/Log/Public/LogFormat.h)，只接受编译期字符数组。
外层 `LogFormatString` 内含 `fmt::format_string` 的编译期语法/类型检查；单用后者不能排除
`fmt::runtime` 的隐式转换。格式子集只有自动 `{}` 和 `{{`/`}}`，格式串不超过 4096 B、
参数不超过 16 个。任何格式说明项（包括 locale、动态宽度/精度）在调用 fmt 前生成构造失败标记。
运行期格式串、预先包装的 fmt 格式对象、裸指针、用户 formatter、long double 和宽字符均不支持。

支持 bool、char、标准有符号/无符号整数、IEEE binary32/binary64、具有受支持底层类型的枚举、
有界 char 数组、std::string/string_view、Foundation ModuleId/Error。文本输入先限制为 1024 B，
身份同步转为内联文本，随后只向 fmt 传内置标量或 string_view。输出经固定 span 迭代器写入，
消息最多 1024 B，保留完整 UTF-8 与转义边界；不使用可增长的 memory_buffer。

源码审计路径：base.h 的 `format_to`/`vformat_to` 使用栈上参数存储和通用
`iterator_buffer`（256 B 内联数组，grow 只 flush）；format.h 的 `default_arg_formatter`
把无说明项参数交给内置 `write`。float/double 的默认 `write` 直接用 Dragonbox
`to_decimal` 的整数运算与静态缓存，不进入精度格式化、Dragon4、locale 或 long double 回退。
整数和已知长度文本直接写出；ModuleId/Error 的编码也只使用这些路径。

正式入口为 `PanguLogUnitTests`、`LogFormatCompile` 和 `LogNoAllocation`。Windows/MSVC STL、
WSL/libc++、macOS/libc++ 的 Release/Modular 已实际运行支持类型极值、长文本、16 参数和满身份记录。
独立进程的 new 替换直接覆盖编入该进程的 LogFormat.cpp 及静态 fmt；Linux 另有 C 分配 --wrap。
Windows CRT/DLL、macOS dylib/malloc zone 与动态标准库内部 C 分配仍有探针盲区，结论同时依赖
上述限定路径的源码审计，不推广为任意 fmt 格式或所有系统内部调用的零分配保证。

升级时须核对许可证、禁异常选项和所用格式路径，重新执行对应格式测试及
[后端关卡](../Spdlog/README.md)。
