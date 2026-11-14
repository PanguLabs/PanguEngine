#include <Context.gen.h>
#include <ModuleDescriptor.h>

#include <tuple>

static_assert(sizeof(void*) == 8);
static_assert(__aarch64__ == 1);
static_assert(fixture::kPlatformVersion == PANGU_EXPECTED_VERSION);
using Traits = pg::meta::TypeTraits<fixture::Context>;
static_assert(Traits::HasTypedAccess && Traits::HasDescription);
static_assert(std::get<0>(Traits::Fields).Info.Key == "pointer64");
static_assert(std::get<1>(Traits::Fields).Info.Key == "aarch64");
static_assert(std::get<2>(Traits::Fields).Info.Key == "target-version");
static_assert(std::get<0>(Traits::Fields).Info.IsReadOnly);
static_assert(std::get<1>(Traits::Fields).Info.IsReadOnly);
static_assert(std::get<2>(Traits::Fields).Info.IsReadOnly);

#ifdef PANGU_MOBILE_LIBRARY
#include <ModuleMeta.gen.h>
namespace fixture
{
const pg::module::ModuleDefinition* Query() noexcept
{
    static const pg::module::ModuleDefinition kDefinition{
        .Descriptor = {.Id = kModuleId, .Dependencies = {}, .DisplayName = "MobileMeta", .BuildIdentity = {}},
        .Metadata = &pg::meta::generated::T_MobileMeta::kModuleMetadata,
    };
    return &kDefinition;
}
} // namespace fixture
#else
int main()
{
    // 强制最终链接包含真实描述提供者；交叉目标不在宿主运行。
    return fixture::Query()->Metadata == nullptr ? 1 : 0;
}
#endif
