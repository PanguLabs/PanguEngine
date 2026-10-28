#include "FixtureModules.h"

#include <ModuleDescriptor.h>
#include <RegistrationContext.h>
#include <StaticModuleHost.h>

#include <iostream>

int main()
{
    using namespace pg::module;
    RegistrationScope scope;
    ModuleOperationReport report;
    const auto catalog = fixture::GeneratedCatalog();
    auto host = StaticModuleHost::Create(catalog, scope, {}, report);
    if (!host || host->Modules().size() != 2 || catalog.Entries.size() != 2 ||
        host->RegistrationOrder().front() != fixture::kSecondId ||
        host->RegistrationOrder().back() != fixture::kFirstId)
    {
        std::cerr << "Catalog must retain both archive Query roots and resolve dependency order\n";
        return 1;
    }
    for (const auto id : host->RegistrationOrder())
    {
        if (!host->Register(id, report))
        {
            std::cerr << "Real STATIC Module registration failed\n";
            return 1;
        }
    }
    const auto declarations = scope.Declarations();
    if (declarations.size() != 2 || declarations.front().Id != fixture::kSecondDeclaration ||
        declarations.front().Owner != fixture::kSecondId || declarations.front().DisplayName != "second" ||
        declarations.back().Id != fixture::kFirstDeclaration || declarations.back().Owner != fixture::kFirstId ||
        declarations.back().DisplayName != "first")
    {
        std::cerr << "Declarations from both STATIC archives must be published in dependency order\n";
        return 1;
    }
    return 0;
}
