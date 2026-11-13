#pragma once

#include <Error.h>
#include <ModuleDeclaration.h>
#include <ModuleIdentity.h>
#include <ScopeIdentity.h>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace pg::module
{
enum class ModuleFailureOrigin : std::uint8_t
{
    kUnknown,
    kRuntime,
    kRegister,
    kFactory,
    kStart,
    kPollStopped,
    kNativeLoader,
};

enum class ModuleWaitKind : std::uint8_t
{
    kBorrow,
    kModulePin,
    kDependency,
    kChildScope,
    kStop,
    kNativeClose,
};

inline constexpr std::size_t kMaxModuleWaitReasons = 8;

// Gate 计数是采样值；身份及已有错误按值保存，不追踪借用者调用栈。
struct ModuleWaitReason
{
    ModuleWaitKind Kind{};
    ModuleId Module{};
    ModuleId RelatedModule{};
    ScopeHandle Scope{};
    DeclarationId Declaration{};
    std::uint64_t Count{};
    std::optional<pg::Error> Error = std::nullopt;
};
} // namespace pg::module
