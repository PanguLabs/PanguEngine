#pragma once

#include <cstdint>

namespace pg::meta
{
struct FieldOptions
{
    const char* Key{nullptr};
    bool ReadOnly{false};
};

enum class Access : std::uint8_t
{
    kPublic,
    kProtected,
    kPrivate,
};
} // namespace pg::meta
