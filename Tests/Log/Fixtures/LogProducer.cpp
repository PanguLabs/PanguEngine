#include "LogProducer.h"

#include <EngineHostModule.h>
#include <FailureContext.h>
#include <FailureReport.h>
#include <Log.h>
#include <LogTypes.h>
#include <LogWriter.h>
#include <ModuleDescriptor.h>
#include <RegistrationContext.h>
#include <Result.h>
#include <ScopeBuildContext.h>
#include <ScopeFactory.h>
#include <ScopeIdentity.h>

#include <array>
#include <expected>
#include <fstream>
#include <memory>
#include <string_view>

namespace pg::log::test
{
namespace
{
// 静态析构只作私有原生卸载观测；不保存 Borrow、Pin 或业务回调。
#ifdef PG_TEST_MODULAR
struct NativeLifetime
{
    NativeLifetime() = default;
    NativeLifetime(const NativeLifetime&) = delete;
    NativeLifetime& operator=(const NativeLifetime&) = delete;
    NativeLifetime(NativeLifetime&&) = delete;
    NativeLifetime& operator=(NativeLifetime&&) = delete;
    ~NativeLifetime() { std::ofstream(PG_LOG_NATIVE_MARKER) << "NativeUnload\n"; }
};
#endif
SubmitResult Produce(const LogWriter& writer, FailureReport& report, ProducerPhase phase) noexcept
{
    if (phase == ProducerPhase::kStatic)
    {
        return PG_LOG_INFO(writer, "plugin-static-category", "plugin static message");
    }
    const auto kMessage = std::to_array("plugin stack message");
    const auto kCategory = std::to_array("plugin-stack-category");
    const auto submitted = PG_LOG_INFO(writer, std::string_view(kCategory.data(), kCategory.size() - 1), "{} {}",
                                       std::string_view(kMessage.data(), kMessage.size() - 1), 42);
    const auto kFailure = std::to_array("plugin stack failure");
    FailureContext(report).SetPrimary(MakeLogError(LogError::kIoFailed),
                                      std::string_view(kFailure.data(), kFailure.size() - 1));
    return submitted;
}
Status Register(module::RegistrationContext& registration) noexcept
{
#ifdef PG_TEST_MODULAR
    static const NativeLifetime kLifetime;
#endif
    return registration.DeclareFactory(kProducerDeclaration, service::Scope::kHost,
                                       module::ScopeFactory(
                                           [](module::ScopeBuildContext& build) noexcept -> Status
                                           {
                                               const auto result =
                                                   build.Own(std::make_unique<Producer>(build.Log(), Produce));
                                               return result ? Status{} : Status{std::unexpected(result.error())};
                                           }));
}
} // namespace
const module::ModuleDefinition* QueryLogProducer() noexcept
{
    static constexpr std::array kDependencies{
        module::ModuleDependency{.Id = engine::kEngineHostModuleId, .Version = {.Major = 1}},
    };
    static constexpr module::ModuleDefinition kDefinition{
        .Descriptor =
            {
                .Id = kProducerId,
                .Version = {.Major = 1},
                .Dependencies = kDependencies,
                .DisplayName = "LogProducer",
                .BuildIdentity = {},
            },
        .RegisterModule = Register,
    };
    return &kDefinition;
}
} // namespace pg::log::test
