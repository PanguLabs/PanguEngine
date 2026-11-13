#include "LogMemory.h"

#include <cstdlib>

namespace pg::log::detail
{
// 无异常初始化不能借用 throwing new；分配/释放限定在 Log 常驻实现。
// NOLINTBEGIN(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
void* TryAllocateLogMemory(std::size_t bytes) noexcept { return bytes == 0 ? nullptr : std::malloc(bytes); }
void FreeLogMemory(void* memory) noexcept { std::free(memory); }
// NOLINTEND(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
} // namespace pg::log::detail
