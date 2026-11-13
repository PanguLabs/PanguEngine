#include "Emergency.h" // NOLINT(misc-include-cleaner): 校验平台实现与 Fatal 的共享声明。

#include <Windows.h> // NOLINT(misc-include-cleaner): 为 winnt.h 提供基础类型与架构定义。
#include <intrin.h>
#include <winnt.h>

namespace pg::detail
{
[[noreturn]] void EmergencyTerminate(const EmergencySnapshot& snapshot) noexcept
{
    // 项目五平台统一 Clang：完整内存输入迫使优化器保留真实栈现场，memory 屏障阻止越过终止边界。
    // 这是编译器约束，不执行指令、输出或线程同步，也不承诺系统必然生成 dump。
    // NOLINTNEXTLINE(portability-no-assembler): Clang 统一工具链的零指令保留约束。
    asm volatile("" : : "m"(snapshot) : "memory");
    // non-continuable fast fail 绕过进程异常处理器和普通清理，不访问输出句柄。
    __fastfail(FAST_FAIL_FATAL_APP_EXIT);
}
} // namespace pg::detail
