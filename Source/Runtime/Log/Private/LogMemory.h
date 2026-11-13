#pragma once

#include <PanguLogExport.h>

#include <cstddef>

namespace pg::log::detail
{
// 仅服务 Log 内部；返回 max_align_t 对齐内存，零大小失败，同源释放。
[[nodiscard]] PG_LOG_API void* TryAllocateLogMemory(std::size_t bytes) noexcept;
PG_LOG_API void FreeLogMemory(void* memory) noexcept;
} // namespace pg::log::detail
