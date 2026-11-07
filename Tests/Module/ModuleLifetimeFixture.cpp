#include "SharedFixtureContract.h"
#include "SharedFixturePaths.h"
#include "Support/ScopeTestSupport.h"

#include <ModuleBuiltin.h>
#include <ModuleError.h>
#include <ModuleManager.h>
#include <ModuleOperation.h>
#include <RegistrationContext.h>
#include <ScopeAccess.h>
#include <ScopeIdentity.h>

#include <array>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <utility>

namespace
{
struct TerminationPhase
{
    bool Terminating{};
    std::string Marker;
};
TerminationPhase& Phase() noexcept
{
    static TerminationPhase phase;
    return phase;
}
void MarkUnload(void* context) noexcept
{
    // Windows 的 ExitProcess 还会执行 DLL 进程退出回调；只观察 Manager 仍可执行时的释放。
    if (!Phase().Terminating)
    {
        std::ofstream(*static_cast<std::string*>(context) + ".unloaded") << "NativeUnload";
    }
}
template <class T>
bool ExerciseAfterUnload(pg::module::ScopeAccess<T> access)
{
    // 调用方已验证 NativeUnload；TryBorrow 不得再触碰业务 RTTI 或销毁器。
    auto copy = access;
    access = {};
    const auto denied = copy.TryBorrow();
    if (denied || denied.error() != pg::module::MakeModuleError(pg::module::ModuleError::kNotReady))
    {
        return false;
    }
    auto final = copy;
    copy = {};
    if (final.TryBorrow())
    {
        return false;
    }
    final = {};
    return true;
}
} // namespace

int main(int argc, char* const* argv)
{
    using namespace pg::module;
    using namespace pg::module::fixture;
    const std::span args(argv, static_cast<std::size_t>(argc));
    if (args.size() != 3)
    {
        return 2;
    }
    std::set_terminate(
        [] noexcept
        {
            if (!Phase().Marker.empty())
            {
                std::ofstream(Phase().Marker + ".terminating") << pg::test::Observer().NativeUnloadCount();
            }
            Phase().Terminating = true;
            std::_Exit(91);
        });
    const std::string mode(args.subspan<1>().front());
    std::string marker(args.back());
    Phase().Marker = marker;
    auto& observer = pg::test::Observer();
    observer.Reset();
    observer.SetNativeUnloadCallback(MarkUnload, &marker);
    RegistrationScope declarations;
    ModuleOperationReport report;
    auto created = ModuleManager::Create(declarations, BuiltinResidentQueries(), {}, report);
    if (!created)
    {
        return 3;
    }
    auto manager = std::make_unique<ModuleManager>(std::move(*created));
    const auto scope = manager->CreateScope(pg::service::Scope::kContext, manager->HostScope(), report);
    if (!scope)
    {
        return 4;
    }
    const std::array candidates{
        ModuleLoadCandidate{SharedModuleCandidate{.ExpectedId = kScopeProviderId, .Path = kScopeProviderPath}},
    };
    const auto loaded = manager->Load(candidates, {}, report);
    if (!loaded)
    {
        return 5;
    }
    const auto provider = loaded->Modules.front();
    if (!manager->Register(provider, report) || !manager->Activate(provider, report))
    {
        return 6;
    }
    auto publicAccess = observer.Access(*scope);
    auto privateAccess = observer.PrivateAccess();
    if (!publicAccess.TryBorrow() || !privateAccess.TryBorrow())
    {
        return 7;
    }
    if (mode == "busy")
    {
        const auto borrow = publicAccess.TryBorrow();
        if (!borrow)
        {
            return 8;
        }
        std::ofstream(marker + ".ready") << "ready";
        manager.reset();
        return 9;
    }
    if (!manager->BeginUnload(provider, report) || !manager->TryFinishUnload(provider, report))
    {
        return 10;
    }
    if (observer.NativeUnloadCount() != 1 || observer.Count(pg::test::Event::kPrivateDestroy) != 1 ||
        observer.Count(pg::test::Event::kDeleterDestroy) != 1 || observer.Count(pg::test::Event::kFactoryDestroy) != 3)
    {
        return 11;
    }
    // 清空观察器持有，最终控制块释放只会发生在下面的卸载后访问路径。
    observer.Reset();
    const bool passed = mode == "private" ? ExerciseAfterUnload(std::move(privateAccess))
                                          : ExerciseAfterUnload(std::move(publicAccess));
    if (!passed)
    {
        return 12;
    }
    publicAccess = {};
    privateAccess = {};
    std::ofstream(marker + ".released") << "最后释放完成";
    return 0;
}
