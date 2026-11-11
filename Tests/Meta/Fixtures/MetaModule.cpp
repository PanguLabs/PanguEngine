#include "MetaModule.h"
#include "ModuleDescriptor.h"
#include <HostTestState.h>
#include <Result.h>
#include <cstdlib>
#include <memory>
#include <string_view>
#ifdef PG_TEST_MODULAR
#include <vector>
#endif
#if PG_META_HAS_DESCRIPTION
#include <ModuleMeta.gen.h>
#endif

namespace pg::test::meta_fixture
{
namespace
{
Status Register(module::RegistrationContext& /*unused*/) noexcept { return {}; }
constexpr module::ModuleDefinition kDefinition{
    .Descriptor =
        {
            .Id = kModuleId,
            .Version = {.Major = 1},
            .Dependencies = {},
            .DisplayName = "MetaFixture",
            .BuildIdentity = "generated-meta",
        },
    .RegisterModule = Register,
#if PG_META_HAS_DESCRIPTION
    .Metadata = &meta::generated::T_PanguMetaFixture::kModuleMetadata,
#endif
};
#ifdef PG_TEST_MODULAR
struct NativeLifetime
{
    std::vector<std::shared_ptr<engine::test::HostTestState>> States;
    NativeLifetime() = default;
    NativeLifetime(const NativeLifetime&) = delete;
    NativeLifetime& operator=(const NativeLifetime&) = delete;
    NativeLifetime(NativeLifetime&&) = delete;
    NativeLifetime& operator=(NativeLifetime&&) = delete;
    ~NativeLifetime()
    {
        for (const auto& state : States)
        {
            ++state->NativeUnloadCount;
            if (state->OnNativeUnload)
            {
                state->OnNativeUnload();
            }
        }
    }
};
#endif
} // namespace
const module::ModuleDefinition* QueryMetaModule() noexcept
{
    if (const auto state = engine::test::CurrentHostTestState())
    {
        ++state->QueryCount;
#ifdef PG_TEST_MODULAR
        static NativeLifetime lifetime;
        lifetime.States.push_back(state);
#endif
        if (state->OnQuery)
        {
            state->OnQuery();
        }
    }
    // 仅夹具进程使用：在真实 Query 接纳边界损坏生成 Provider，不另造人工类型表。
    // 子进程只读固定环境，不与环境修改并发；保留标准跨平台接口。
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const auto* mode = std::getenv("PANGU_META_FIXTURE_MODE");
#pragma clang diagnostic pop
    if (mode != nullptr && std::string_view(mode) == "no-provider")
    {
        static const auto kNoProvider = []
        {
            auto value = kDefinition;
            value.Metadata = nullptr;
            return value;
        }();
        return &kNoProvider;
    }
#if PG_META_HAS_DESCRIPTION
    if (mode != nullptr && std::string_view(mode) == "invalid-provider")
    {
        static const auto kWrongOwner = []
        {
            auto value = meta::generated::T_PanguMetaFixture::kModuleMetadata;
            value.Owner = kServiceModuleId;
            return value;
        }();
        static const auto kInvalid = []
        {
            auto value = kDefinition;
            value.Metadata = &kWrongOwner;
            return value;
        }();
        return &kInvalid;
    }
#endif
    return &kDefinition;
}
} // namespace pg::test::meta_fixture
