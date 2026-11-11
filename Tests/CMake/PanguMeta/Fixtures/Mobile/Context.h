#pragma once

#include <MetaOptions.h>
#include <MobileMetaExport.h>
#include <ModuleDescriptor.h>

namespace fixture
{
inline constexpr pg::module::ModuleId kModuleId{.Bytes = {std::byte{0xe3}}};
[[nodiscard]] MOBILEMETA_API const pg::module::ModuleDefinition* Query() noexcept;

#ifdef __ANDROID__
inline constexpr int kPlatformVersion = __ANDROID_API__;
#else
inline constexpr int kPlatformVersion = __ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__;
#endif
inline constexpr const char* kPointerKey = sizeof(void*) == 8 ? "pointer64" : "pointer32";
inline constexpr const char* kArchitectureKey = __aarch64__ == 1 ? "aarch64" : "other";
inline constexpr const char* kPlatformKey =
    kPlatformVersion == PANGU_EXPECTED_VERSION ? "target-version" : "wrong-version";

struct [[pga::Type]] Context
{
    [[pga::Field(pg::meta::FieldOptions{.Key = kPointerKey, .ReadOnly = sizeof(void*) == 8})]] int Pointer{};
    [[pga::Field(pg::meta::FieldOptions{.Key = kArchitectureKey, .ReadOnly = __aarch64__ == 1})]] int Architecture{};
    [[pga::Field(pg::meta::FieldOptions{.Key = kPlatformKey,
                                        .ReadOnly = kPlatformVersion == PANGU_EXPECTED_VERSION})]] int Platform{};
};
} // namespace fixture
