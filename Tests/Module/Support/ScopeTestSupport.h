#pragma once

#include <PanguScopeTestSupportExport.h>
#include <ScopeBuildContext.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pg::test
{
enum class Event : std::uint8_t
{
    kNativeLoad,
    kRegister,
    kStart,
    kStop,
    kDestroy,
    kFactoryDestroy,
    kNativeUnload,
    kConsumerStart,
    kConsumerStop,
    kConsumerPoll,
    kConsumerDestroy,
    kDependencyUsed,
    kPoll,
    kContextCreate,
    kWorldCreate,
    kDeleterDestroy,
    kPrivateDestroy,
    kProviderDestroy,
};

struct SharedFixtureOptions
{
    bool Services{};
    bool ProviderInContext{};
    bool FailProviderFactory{};
    bool FailProviderStart{};
    bool MetadataCycle{};
    bool FailSecondConsumerStart{};
    unsigned ConsumerBusyPolls{};
    unsigned FailContextFactoryAt{};
    unsigned FailContextStartAt{};
    bool WrongScopeDependencyType{};
    std::optional<pg::Error> DiagnosticPollError = std::nullopt;
};

struct ProviderPrivateState;

struct PG_SCOPE_TEST_SUPPORT_API ProbeState
{
    ProbeState(std::string sceneName, std::vector<int> values);
    ~ProbeState();
    ProbeState(const ProbeState&) = delete;
    ProbeState& operator=(const ProbeState&) = delete;
    ProbeState(ProbeState&&) = delete;
    ProbeState& operator=(ProbeState&&) = delete;
    std::string SceneName;
    std::vector<int> Values;
    unsigned Starts{};
    unsigned Stops{};
    unsigned Polls{};
};

class PG_SCOPE_TEST_SUPPORT_API ScopeTestObserver final
{
public:
    void Reset() noexcept;
    void Record(Event event) noexcept;
    using NativeUnloadCallback = void (*)(void*) noexcept;
    // 测试持有回调与 context，直到清除；原生卸载时在释放观察器锁后同步调用。
    void SetNativeUnloadCallback(NativeUnloadCallback callback, void* context) noexcept;
    [[nodiscard]] std::size_t RegisterCount() const noexcept;
    [[nodiscard]] std::size_t NativeUnloadCount() const noexcept;
    [[nodiscard]] std::vector<Event> Events() const;
    void SetAccess(module::ScopeAccess<ProbeState> access) noexcept;
    [[nodiscard]] module::ScopeAccess<ProbeState> Access() const noexcept;
    void SaveAccess(module::ScopeHandle scope, module::ScopeAccess<ProbeState> access) noexcept;
    [[nodiscard]] module::ScopeAccess<ProbeState> Access(module::ScopeHandle scope) const noexcept;
    void SavePrivateAccess(module::ScopeAccess<ProviderPrivateState> access) noexcept;
    [[nodiscard]] module::ScopeAccess<ProviderPrivateState> PrivateAccess() const noexcept;
    void ConfigureSharedFixture(SharedFixtureOptions options) noexcept;
    [[nodiscard]] SharedFixtureOptions SharedOptions() const noexcept;
    [[nodiscard]] std::size_t Count(Event event) const noexcept;

private:
    mutable std::mutex Mutex;
    std::vector<Event> History;
    module::ScopeAccess<ProbeState> Token;
    std::vector<std::pair<module::ScopeHandle, module::ScopeAccess<ProbeState>>> ScopeTokens;
    module::ScopeAccess<ProviderPrivateState> PrivateToken;
    NativeUnloadCallback UnloadCallback{};
    void* UnloadContext{};
    SharedFixtureOptions Options;
};

[[nodiscard]] PG_SCOPE_TEST_SUPPORT_API ScopeTestObserver& Observer() noexcept;
[[nodiscard]] PG_SCOPE_TEST_SUPPORT_API module::ScopeHooks<ProbeState> ProbeHooks() noexcept;

// 通过真实 Own 路径产生令牌，关闭后才销毁资源，允许令牌继续存活。
struct PG_SCOPE_TEST_SUPPORT_API ScopeTestSink final : module::detail::ScopeBuildSink
{
    ~ScopeTestSink() override;
    ScopeTestSink() = default;
    ScopeTestSink(const ScopeTestSink&) = delete;
    ScopeTestSink& operator=(const ScopeTestSink&) = delete;
    ScopeTestSink(ScopeTestSink&&) = delete;
    ScopeTestSink& operator=(ScopeTestSink&&) = delete;
    [[nodiscard]] module::ModuleId Owner() const noexcept override;
    [[nodiscard]] module::ScopeHandle Target() const noexcept override;
    [[nodiscard]] pg::Error Reject(pg::Error error) noexcept override;
    [[nodiscard]] pg::Result<std::shared_ptr<module::detail::ScopeObjectControl>>
    Stage(std::unique_ptr<module::detail::ErasedScopeObject> object) noexcept override;
    [[nodiscard]] pg::Result<module::detail::ScopeDependencyObject> Resolve(module::DeclarationId id) noexcept override;
    void Publish() const noexcept;
    std::unique_ptr<module::detail::ErasedScopeObject> Object;
    std::shared_ptr<module::detail::ScopeObjectControl> Control;
    std::optional<pg::Error> Failure;
};
} // namespace pg::test
