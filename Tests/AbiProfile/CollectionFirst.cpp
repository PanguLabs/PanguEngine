#include "CollectionFixture.h"
#include <expected>

namespace pg::abi::test
{
std::expected<int, int> CollectionFirst() noexcept { return 1; }
} // namespace pg::abi::test
