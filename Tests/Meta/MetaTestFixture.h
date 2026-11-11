#pragma once
#include <EngineHost.h>
#include <HostOperation.h>
#include <HostStartupConfig.h>
#include <HostTestState.h>
#include <MetaActor.gen.h>
#include <MetaModule.h>
#include <MetaModulePaths.h>
#include <MetaService.gen.h>
#include <array>
#include <memory>
#ifndef PG_TEST_MODULAR
#include <GeneratedModuleCatalog.h>
#endif

namespace pg::test::meta_fixture
{
[[nodiscard]] inline module::ModuleLoadCandidate Candidate(module::ModuleId id)
{
#ifdef PG_TEST_MODULAR
    return module::SharedModuleCandidate{.ExpectedId = id, .Path = id == kModuleId ? kModulePath : kServiceModulePath};
#else
    // 顺序来自 pangu_meta_host_catalog 的显式集合，不在接纳前调用业务 Query。
    const auto entries = module::GeneratedStaticModuleCatalog().Entries;
    return module::StaticModuleCandidate{entries.subspan(id == kModuleId ? 0U : 1U).front().Query};
#endif
}
[[nodiscard]] inline Status Start(engine::EngineHost& host, engine::HostOperationReport& report, bool service = false)
{
    const std::array candidates{Candidate(kModuleId), Candidate(kServiceModuleId)};
    engine::HostStartupConfig config;
    config.Modules = std::span(candidates).first(service ? 2U : 1U);
    return host.Start(config, report);
}
[[nodiscard]] inline Status Shutdown(engine::EngineHost& host, engine::HostOperationReport& report)
{
    const auto begun = host.BeginShutdown(report);
    return begun ? host.TryFinishShutdown(report) : begun;
}
} // namespace pg::test::meta_fixture
