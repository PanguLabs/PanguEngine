#include "ThirdParty.h"

#include <EngineHost.h>
#include <EngineHostModule.h>
#include <Error.h>
#include <FixtureModules.h>
#include <HostOperation.h>
#include <HostStartupConfig.h>
#include <LogTypes.h>
#include <ModuleIdentityParse.h>

int main()
{
    pg::engine::EngineHost host;
    pg::engine::HostOperationReport report;
    pg::engine::HostStartupConfig config;
    config.Logging.Enabled = pg::log::OutputMask::kNone;
    config.Logging.Required = pg::log::OutputMask::kNone;
    if (!host.Start(config, report) || !host.BeginShutdown(report) || !host.TryFinishShutdown(report) ||
        host.GetState() != pg::engine::HostState::kClosed ||
        pg::engine::QueryEngineHostModule()->Descriptor.Id != pg::engine::kEngineHostModuleId)
    {
        return 2;
    }
    const auto error = pg::MakeError(pg::ErrorDomainId::Constant<1>(), 23);
    const auto id = pg::module::detail::ParseModuleId("a127f219664c4413b670e820625088e1");
    const auto* first = pg::module::fixture::QueryFirst();
    const auto* second = pg::module::fixture::QuerySecond();
    return error.Code() == 23 && ThirdPartyValue() == 23 && id && *id == first->Descriptor.Id &&
                   second->Descriptor.Id == pg::module::fixture::kSecondId
               ? 0
               : 1;
}
