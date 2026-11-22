#pragma once

#include <memory>

namespace pg::renderer::detail
{
class FrameBackend;

[[nodiscard]] std::unique_ptr<FrameBackend> MakeForgeBackend() noexcept;
} // namespace pg::renderer::detail
