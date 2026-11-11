#if PG_META_HAS_DESCRIPTION
#include <ModuleMeta.gen.h>
#include <type_traits>
static_assert(
    std::is_same_v<decltype(pg::meta::generated::T_Owner::kModuleMetadata), const pg::module::ModuleMetaProvider>);
#endif
#include <Identity.h>
#include <ModuleDescriptor.h>
namespace fixture
{
const pg::module::ModuleDefinition* Query() noexcept
{
    static const pg::module::ModuleDefinition kDefinition{
        .Descriptor = {.Id = kModuleId, .Dependencies = {}, .DisplayName = "Owner", .BuildIdentity = {}},
#if PG_META_HAS_DESCRIPTION
        .Metadata = &pg::meta::generated::T_Owner::kModuleMetadata,
#endif
    };
    return &kDefinition;
}
} // namespace fixture
