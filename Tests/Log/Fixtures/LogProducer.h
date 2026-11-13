#pragma once

#include <FailureReport.h>
#include <HostServiceAccess.h>
#include <LogTypes.h>
#include <LogWriter.h>
#include <ModuleDeclaration.h>
#include <ModuleDescriptor.h>
#include <cstddef>
#include <cstdint>

namespace pg::log::test
{
inline constexpr module::ModuleId kProducerId{
    .Bytes =
        {
            std::byte{0xb6},
            std::byte{0xdd},
            std::byte{0xc3},
            std::byte{0xfd},
            std::byte{0xc5},
            std::byte{0xb3},
            std::byte{0x4a},
            std::byte{0x2e},
            std::byte{0x90},
            std::byte{0x8c},
            std::byte{0x56},
            std::byte{0x6d},
            std::byte{0x90},
            std::byte{0xdb},
            std::byte{0x1e},
            std::byte{0x22},
        },
};
inline constexpr module::DeclarationId kProducerDeclaration{
    .Bytes =
        {
            std::byte{0xb6},
            std::byte{0xdd},
            std::byte{0xc3},
            std::byte{0xfd},
            std::byte{0xc5},
            std::byte{0xb3},
            std::byte{0x4a},
            std::byte{0x2e},
            std::byte{0x90},
            std::byte{0x8c},
            std::byte{0x56},
            std::byte{0x6d},
            std::byte{0x90},
            std::byte{0xdb},
            std::byte{0x1e},
            std::byte{0x23},
        },
};

enum class ProducerPhase : std::uint8_t
{
    kStack,
    kStatic,
};

// 两阶段均在同一 ScopeBorrow 保护期间调用；记录不得携带函数指针。
#ifdef _WIN32
struct Producer
#else
struct __attribute__((visibility("default"))) Producer
#endif
{
    LogWriter Writer;
    SubmitResult (*Produce)(const LogWriter&, FailureReport&, ProducerPhase) noexcept;
};
[[nodiscard]] inline Result<module::ScopeAccess<Producer>> AcquireProducer(const engine::EngineHost& host,
                                                                           module::ScopeHandle scope) noexcept
{
    return engine::detail::HostResourceAccess::Acquire<Producer>(
        host,
        {.Scope = scope, .Owner = kProducerId, .Declaration = kProducerDeclaration, .Kind = service::Scope::kHost});
}
[[nodiscard]] const module::ModuleDefinition* QueryLogProducer() noexcept;
} // namespace pg::log::test
