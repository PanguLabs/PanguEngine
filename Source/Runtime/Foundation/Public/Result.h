#pragma once

#include <Error.h>

#include <expected>

namespace pg
{
template <typename T>
using Result = std::expected<T, Error>;

using Status = Result<void>;
} // namespace pg
