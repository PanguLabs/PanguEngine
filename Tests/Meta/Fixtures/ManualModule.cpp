#include "ManualModule.h"
#include "MetaInfo.h"
#include "ModuleDescriptor.h"
#include "Result.h"
#include <ModuleMetaProvider.h>
#include <array>
#include <optional>

namespace pg::test
{
namespace
{
Status Register(module::RegistrationContext& /*unused*/) noexcept { return {}; }
constexpr meta::TypeInfo kInfo{.Name = "pg::test::ManualType", .Id = std::nullopt, .Fields = {}};
const std::array kEntries{module::ModuleMetaEntry{.CppType = &typeid(ManualType), .Info = &kInfo}};
const module::ModuleMetaProvider kProvider{.Owner = kManualModuleId, .Entries = kEntries};
const module::ModuleDefinition kDefinition{
    .Descriptor = {.Id = kManualModuleId, .Dependencies = {}, .DisplayName = "ManualMeta", .BuildIdentity = {}},
    .RegisterModule = Register,
    .Metadata = &kProvider,
};
} // namespace
const module::ModuleDefinition* QueryManualModule() noexcept { return &kDefinition; }
} // namespace pg::test
