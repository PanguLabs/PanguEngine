#pragma once

#include "FixtureModules.h"

#include <RegistrationContext.h>
#include <StaticModuleHost.h>

namespace pg::fixture
{
[[nodiscard]] inline int VerifyCatalog(const module::StaticModuleCatalog& catalog, module::ModuleId dependent) noexcept
{
    module::RegistrationScope scope;
    module::ModuleOperationReport report;
    auto host = module::StaticModuleHost::Create(catalog, scope, {}, report);
    if (!host || host->Modules().size() != 2)
    {
        return 1;
    }
    const auto order = host->RegistrationOrder();
    if (order.size() != 2 || order.front() != kPhysicsId || order.back() != dependent)
    {
        return 2;
    }
    for (const auto id : order)
    {
        if (!host->Register(id, report))
        {
            return 3;
        }
    }
    return scope.Declarations().size() == 2 ? 0 : 4;
}
} // namespace pg::fixture
