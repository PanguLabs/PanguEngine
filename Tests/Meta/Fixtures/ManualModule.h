#pragma once

#include <MetaIdentity.h>
#include <MetaTraits.h>
#include <ModuleDescriptor.h>
#include <ModuleIdentity.h>
#include <PanguManualMetaFixtureExport.h>
#include <cstddef>
#include <optional>
#include <string_view>
#include <tuple>

namespace pg::test
{
inline constexpr module::ModuleId kManualModuleId = []
{
    module::ModuleId id{};
    id.Bytes.front() = std::byte{0xe1};
    id.Bytes.back() = std::byte{3};
    return id;
}();
inline constexpr module::ModuleId kWrongManualOwner = []
{
    auto id = kManualModuleId;
    id.Bytes.back() = std::byte{4};
    return id;
}();
// 公共夹具类型参与跨库 RTTI 比较；POSIX 必须允许类型信息合并。
#ifndef _WIN32
#pragma GCC visibility push(default)
#endif
struct ManualType
{
};
#ifndef _WIN32
#pragma GCC visibility pop
#endif
[[nodiscard]] PG_MANUAL_META_API const module::ModuleDefinition* QueryManualModule() noexcept;
} // namespace pg::test

namespace pg::meta
{
// 人工协议夹具；生成器接线由独立集成用例验证。
template <>
struct TypeTraits<test::ManualType>
{
    // 公共 Traits 协议固定这些成员名，与生成结果保持一致。
    // NOLINTBEGIN(readability-identifier-naming)
    static constexpr auto Owner = test::kManualModuleId;
    static constexpr std::string_view Name = "pg::test::ManualType";
    static constexpr std::optional<TypeId> Id = std::nullopt;
    static constexpr bool HasTypedAccess = false;
    static constexpr bool HasDescription = true;
    static constexpr std::tuple<> Fields{};
    // NOLINTEND(readability-identifier-naming)
};
} // namespace pg::meta
