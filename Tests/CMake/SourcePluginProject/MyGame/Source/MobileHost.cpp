#include "MobileHost.h"
#include "FixtureModules.h"
#include "HostCheck.h"
#include <Check.h>
#include <Error.h>
#include <FailureContext.h>
#include <FailureReport.h>
#include <Log.h>
#include <LogConfig.h>
#include <LogReports.h>
#include <LogSystem.h>
#include <LogTypes.h>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <string_view>
#ifdef PANGU_FIXTURE_MODULAR
#include "ModuleCandidates.h"
#else
#include <GeneratedModuleCatalog.h>
#endif

namespace
{
// 直接消费公共诊断接口，保证移动最终产物包含真实调用而非只有头文件。
[[nodiscard]] int CheckMobileDiagnostics()
{
    pg::FailureReport failure;
    pg::FailureContext context(failure);
    context.SetPrimary(pg::MakeError(pg::ErrorDomainId::Constant<0x741>(), 8), "mobile diagnostics");
    const auto site = failure.PrimarySite();
    PG_CHECK(site.has_value(), "mobile failure report lost its primary site");

    pg::log::LogSystem log;
    pg::log::LogConfig config;
    config.Enabled = pg::log::OutputMask::kConsole;
    config.Required = pg::log::OutputMask::kConsole;
    pg::log::LogInitReport initialized;
    const auto started = log.Initialize(config, initialized);
    auto writer = log.GetWriter();
    if (started)
    {
        PG_LOG_INFO(writer, "MobileHost", "{} {} {} {}", std::string_view{"移动诊断"}, 42, 1.25, true);
    }
    pg::log::StopReport stopped;
    const auto closed = log.Stop(std::chrono::steady_clock::now() + std::chrono::seconds(5), stopped);
    PG_CHECK(closed && stopped.ResourcesReleased, "mobile diagnostics cleanup did not finish");
    return started ? 0 : 13;
}

// 复用真实 EngineHost 启动、Context 服务访问和显式关闭，参与移动端最终链接。
[[nodiscard]] int CheckMobileModules(const std::filesystem::path& directory)
{
    if (const auto diagnostics = CheckMobileDiagnostics(); diagnostics != 0)
    {
        return diagnostics;
    }
    const auto error = pg::MakeError(pg::ErrorDomainId::Constant<0x741>(), 7);
    if (error.Code() != 7)
    {
        return 10;
    }
    const auto logging = pg::fixture::ApplicationLogging(directory, std::byte{0x23});
#ifdef PANGU_FIXTURE_MODULAR
    return pg::fixture::VerifyCandidates(pg::fixture::ConsumerCandidates(), pg::fixture::kGameplayId, true, logging);
#else
    return pg::fixture::VerifyCatalog(pg::module::GeneratedStaticModuleCatalog(), pg::fixture::kGameplayId, logging);
#endif
}
} // namespace

#ifdef __ANDROID__
// Android 最终宿主是普通原生库，无 ModuleId，也不导出 Module Query。
// Android 应用负责传入自身沙箱的绝对可写分区，宿主不猜 cwd。
extern "C" __attribute__((visibility("default"))) int PanguMobileHostCheck(const char* logDirectory)
{
    if (logDirectory == nullptr || !std::filesystem::path(logDirectory).is_absolute())
    {
        return 12;
    }
    return CheckMobileModules(logDirectory);
}
#else
int main()
{
    const auto* home = std::getenv("HOME");
    if (home == nullptr)
    {
        return 12;
    }
    return CheckMobileModules(std::filesystem::path(home) / "Library" / "Logs" / "PanguMobileHost");
}
#endif
