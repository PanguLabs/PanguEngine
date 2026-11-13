#include "Identity.h"
#include <Actor.gen.h>
#include <EngineHost.h>
#include <HostMetaAccess.h>
#include <HostOperation.h>
#include <HostStartupConfig.h>
#include <LogTypes.h>
#include <array>
#include <iostream>
#if PG_TEST_TYPED
#include <Extras/Actor.gen.h>
#include <中文目录/角色.gen.h>
#endif
#if PG_META_HAS_DESCRIPTION
#include <MetaOptions.h>
#include <ModuleMetaBorrow.h>
#include <ModuleMetaProvider.h>
#include <iterator>
#else
#include <ModuleDescriptor.h>
#endif

namespace
{
template <class T>
concept CanBorrow = requires(const pg::engine::EngineHost& host) { pg::meta::BorrowTypeInfo<T>(host); };
bool Run()
{
    using pg::test::Actor;
    static_assert(pg::meta::TypeTraits<Actor>::HasTypedAccess == (PG_TEST_TYPED != 0));
    static_assert(CanBorrow<Actor> == (PG_META_HAS_DESCRIPTION != 0));
    static_assert(pg::meta::TypeTraits<Actor>::Owner == pg::test::kActorModule);
#if PG_TEST_TYPED
    Actor actor;
    unsigned fields = 0;
    bool escaped = false;
    pg::meta::ForEachField<Actor>(
        [&](const auto& field)
        {
            ++fields;
            if constexpr (requires { field.Write(actor, 2.0F); })
            {
                field.Write(actor, 2.0F);
            }
            if (field.Info.Name == "Escaped")
            {
                escaped = field.Info.Key == "quote\"\\\n\001F中";
            }
        });
    if (actor.Speed != 2.0F || actor.Fixed != 7 || actor.Locked != 8 || actor.GetSecret() != 10 || fields != 5 ||
        !escaped)
    {
        return false;
    }
    pg::other::Actor other;
    pg::unicode::Actor unicode;
    pg::meta::ForEachField<pg::other::Actor>([&](const auto& field) { field.Write(other, 41); });
    pg::meta::ForEachField<pg::unicode::Actor>([&](const auto& field) { field.Write(unicode, 59); });
    if (other.Value != 41 || unicode.Value != 59)
    {
        return false;
    }
#endif
    pg::engine::EngineHost host;
    pg::engine::HostOperationReport report;
    const std::array queries{pg::test::QueryActor};
    pg::engine::HostStartupConfig config;
    config.AdditionalResidentQueries = queries;
    config.Logging.Enabled = pg::log::OutputMask::kNone;
    config.Logging.Required = pg::log::OutputMask::kNone;
    if (!host.Start(config, report))
    {
        return false;
    }
#if PG_META_HAS_DESCRIPTION
    const auto& definition = *pg::test::QueryActor();
    if (definition.Metadata == nullptr || definition.Metadata->Owner != definition.Descriptor.Id ||
        !definition.Metadata->Owner.IsValid())
    {
        return false;
    }
    auto info = pg::meta::BorrowTypeInfo<Actor>(host);
    if (!info || info->Get()->Fields.size() != 6 || info->Get()->Name != "pg::test::Actor" || !info->Get()->Id)
    {
        return false;
    }
    const auto all = info->Get()->Fields;
    const auto& fixed = *std::next(all.begin(), 1);
    const auto& locked = *std::next(all.begin(), 2);
    if (!fixed.IsConst || fixed.IsReadOnly || locked.IsConst || !locked.IsReadOnly ||
        std::next(all.begin(), 3)->Key != "quote\"\\\n\001F中" ||
        std::next(all.begin(), 4)->TypeName != "decltype(pg::test::Actor::Anonymous)" ||
        std::next(all.begin(), 5)->Visibility != pg::meta::Access::kPrivate)
    {
        return false;
    }
    if (!host.BeginShutdown(report) || host.TryFinishShutdown(report))
    {
        return false;
    }
    info->Release();
#else
    if (pg::test::QueryActor()->Metadata != nullptr || !host.BeginShutdown(report))
    {
        return false;
    }
#endif
    return static_cast<bool>(host.TryFinishShutdown(report));
}
} // namespace
int main()
{
    if (!Run())
    {
        std::cerr << "generated consumer contract failed\n";
        return 1;
    }
    return 0;
}
