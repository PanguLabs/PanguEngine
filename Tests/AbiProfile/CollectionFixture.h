#pragma once

#include <expected>

namespace pg::abi::test
{
[[nodiscard]] std::expected<int, int> CollectionFirst() noexcept;
[[nodiscard]] std::expected<int, int> CollectionSecond() noexcept;
} // namespace pg::abi::test
