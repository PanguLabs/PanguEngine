#pragma once

#include <ModuleIdentity.h>
#include <Result.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace pg::module
{
inline constexpr std::size_t kMaxCatalogModules = 256;
inline constexpr std::size_t kMaxModuleDependencies = 64;
inline constexpr std::size_t kMaxModuleTextBytes = 256;

enum class DependencyKind : std::uint8_t
{
    kRequired,
    kOptional,
};

struct ModuleDependency
{
    ModuleId Id{};
    ModuleVersion Version{};
    DependencyKind Kind{DependencyKind::kRequired};
};

// 仅在受保护的 Query 接纳窗口内借用；Manager 深复制诊断元数据。
struct ModuleDescriptor
{
    ModuleId Id{};
    ModuleVersion Version{};
    std::uint32_t ProtocolVersion{1};
    std::span<const ModuleDependency> Dependencies;
    std::string_view DisplayName;
    std::string_view BuildIdentity;
};

class RegistrationContext;
struct StaticModuleCatalog;
using RegisterModuleFn = pg::Status (*)(RegistrationContext&) noexcept;

struct ModuleDefinition
{
    ModuleDescriptor Descriptor{};
    RegisterModuleFn RegisterModule{};
};

using QueryModuleFn = const ModuleDefinition* (*)() noexcept;

struct StaticCatalogEntry
{
    QueryModuleFn Query{};
};

struct StaticModuleCatalog
{
    std::span<const StaticCatalogEntry> Entries;
};

struct OptionalDependencyBinding
{
    ModuleId Dependent{};
    ModuleId Dependency{};

    auto operator<=>(const OptionalDependencyBinding&) const noexcept = default;
};
} // namespace pg::module
