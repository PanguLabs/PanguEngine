#include "Fixtures/ScopeLifecycleFixture.h"

#include "Fixtures/ManagerFixture.h"
#include "ModuleHandle.h"
#include "ModuleManager.h"
#include "ModuleOperation.h"
#include "RegistrationContext.h"
#include <ModuleDescriptor.h>
#include <Result.h>

#include <array>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <utility>

namespace
{
using namespace pg::module;
using namespace pg::test::scope_lifecycle;

// Query/注册回调不接受用户数据，只在此进程内借用本次场景状态。
struct Fixture
{
    inline static std::shared_ptr<Counters> gState;
    static pg::Status Register(RegistrationContext& context) noexcept { return RegisterObjects(context, gState); }
};

const ModuleDefinition* Query() noexcept
{
    static const ModuleDefinition kDefinition{
        .Descriptor = {.Id = kModule, .Dependencies = {}, .DisplayName = {}, .BuildIdentity = {}},
        .RegisterModule = Fixture::Register,
    };
    return &kDefinition;
}
} // namespace

int main(int argc, char* const* argv)
{
    const std::span args(argv, static_cast<std::size_t>(argc));
    if (args.size() != 2)
    {
        return 2;
    }
    RegistrationScope declarations;
    ModuleOperationReport report;
    const auto state = std::make_shared<Counters>();
    Fixture::gState = state;
    constexpr std::array kEntries{StaticCatalogEntry{Query}};
    auto result = pg::module::test::CreateManager({kEntries}, declarations, {}, report);
    if (!result)
    {
        return 2;
    }
    auto host = std::make_unique<ModuleManager>(std::move(*result));
    if (!host->Register(host->Find(kModule).value_or(ModuleHandle{}), report) ||
        !host->Activate(host->Find(kModule).value_or(ModuleHandle{}), report) ||
        !host->CreateScope(Scope::kContext, host->HostScope(), report))
    {
        return 2;
    }
    const auto borrow = state->Exports.front().Access.TryBorrow();
    if (!borrow)
    {
        return 2;
    }
    const std::string marker(args.back());
    state->DestroyMarker = marker;
    std::ofstream(marker + ".ready") << "ready";
    host.reset();
    return 0;
}
