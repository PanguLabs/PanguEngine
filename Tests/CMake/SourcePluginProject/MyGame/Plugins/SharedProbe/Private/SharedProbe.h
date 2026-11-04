#pragma once

namespace pg::fixture
{
// 仅用于 SHARED 构建/链接冒烟，不是运行时 Loader 协议。
[[nodiscard]] int SharedProbeValue() noexcept;
} // namespace pg::fixture
