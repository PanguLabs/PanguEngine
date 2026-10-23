#include "CollectionFixture.h"
#include <expected>

namespace pg::abi::test
{
std::expected<int, int> CollectionSecond() noexcept { return 2; }
} // namespace pg::abi::test
