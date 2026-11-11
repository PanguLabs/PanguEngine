#pragma once

#include <MetaInfo.h>
#include <ModuleIdentity.h>

#include <concepts>
#include <tuple>
#include <type_traits>
#include <utility>

namespace pg::meta
{
template <class T>
struct TypeTraits;

namespace detail
{
template <class T>
struct MemberPointerTraits;

template <class Owner, class Member>
struct MemberPointerTraits<Member Owner::*>
{
    using OwnerType = Owner;
    using MemberType = Member;
};
} // namespace detail

template <auto Member, bool PolicyReadOnly = false>
    requires std::is_member_object_pointer_v<decltype(Member)>
struct FieldAccess
{
    using OwnerType = detail::MemberPointerTraits<decltype(Member)>::OwnerType;
    using MemberType = detail::MemberPointerTraits<decltype(Member)>::MemberType;
    using ValueType = std::remove_cv_t<MemberType>;

    FieldInfo Info;

    [[nodiscard]] constexpr const MemberType& Read(const OwnerType& owner) const noexcept { return owner.*Member; }

    [[nodiscard]] const MemberType& Read(OwnerType&&) const = delete;
    [[nodiscard]] const MemberType& Read(const OwnerType&&) const = delete;

    template <class U>
        requires(!PolicyReadOnly && !std::is_const_v<MemberType> && std::same_as<std::remove_cvref_t<U>, ValueType> &&
                 std::is_assignable_v<MemberType&, U&&>)
    constexpr void Write(OwnerType& owner, U&& value) const noexcept(std::is_nothrow_assignable_v<MemberType&, U&&>)
    {
        owner.*Member = std::forward<U>(value);
    }
};

template <class T, class Visitor>
    requires(TypeTraits<T>::HasTypedAccess)
// 同一个 visitor 依次以左值调用，避免对多个字段反复移动其状态。
// NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
constexpr void ForEachField(Visitor&& visitor)
{
    std::apply([&visitor](const auto&... fields) { (static_cast<void>(visitor(fields)), ...); }, TypeTraits<T>::Fields);
}
} // namespace pg::meta
