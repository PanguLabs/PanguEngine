#include "Backends/ForgeBackend.h"
#include "FrameBackend.h"
#include "RendererDeclaration.h"

#include <EngineHostModule.h>
#include <ModuleDescriptor.h>
#include <RegistrationContext.h>
#include <RendererModule.h>
#include <RendererSession.h>
#include <Result.h>
#include <ScopeBuildContext.h>
#include <ScopeFactory.h>
#include <ScopeIdentity.h>

#include <array>
#include <expected>
#include <memory>
#include <utility>

namespace pg::renderer
{
namespace detail
{
class RendererSessionFactory final
{
public:
    [[nodiscard]] static Status Build(module::ScopeBuildContext& context) noexcept
    {
        // 工厂只创建冷会话。Application 在借用期内打开并关闭设备。
        auto session = std::unique_ptr<RendererSession>{new RendererSession(MakeForgeBackend())};
        const auto owned = context.Own(std::move(session));
        return owned ? Status{} : std::unexpected(owned.error());
    }
};
} // namespace detail

namespace
{
[[nodiscard]] Status Register(module::RegistrationContext& context) noexcept
{
    return context.DeclareFactory(detail::kRendererSessionDeclaration, service::Scope::kHost,
                                  module::ScopeFactory(detail::RendererSessionFactory::Build), {}, "Renderer Session");
}

constexpr std::array kDependencies{
    module::ModuleDependency{
        .Id = engine::kEngineHostModuleId,
        .Version = {.Major = 1, .Minor = 0, .Patch = 0},
        .Kind = module::DependencyKind::kRequired,
    },
};
constexpr module::ModuleDefinition kDefinition{
    .Descriptor =
        {
            .Id = kRendererModuleId,
            .Version = {.Major = 1, .Minor = 0, .Patch = 0},
            .ProtocolVersion = 1,
            .Dependencies = kDependencies,
            .DisplayName = "Renderer",
            .BuildIdentity = {},
        },
    .RegisterModule = Register,
};
} // namespace

const module::ModuleDefinition* QueryRendererModule() noexcept { return &kDefinition; }
} // namespace pg::renderer
