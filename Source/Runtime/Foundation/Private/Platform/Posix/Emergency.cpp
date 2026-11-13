#include "Emergency.h"

#include <signal.h> // NOLINT(modernize-deprecated-headers): POSIX kill 与 SIGKILL 的原生声明。
#include <unistd.h>

namespace pg::detail
{
[[noreturn]] void EmergencyTerminate(const EmergencySnapshot& snapshot) noexcept
{
    // 项目五平台统一 Clang：完整内存输入迫使优化器保留真实栈现场，memory 屏障阻止越过终止边界。
    // 这是编译器约束，不执行指令、输出或线程同步，也不承诺系统必然生成 dump。
    // NOLINTNEXTLINE(portability-no-assembler): Clang 统一工具链的零指令保留约束。
    asm volatile("" : : "m"(snapshot) : "memory");
    // SIGKILL 不可捕获、阻塞或忽略；向自身发送，不进入 atexit、stdio 或 C++ 析构。
    (void)::kill(::getpid(), SIGKILL);
    // 正常契约下不会抵达；即使平台拒绝 syscall 也绝不返回到业务调用者。
    __builtin_trap();
}
} // namespace pg::detail
