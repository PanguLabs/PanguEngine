#include <MetaIdentity.h>
#include <MetaInfo.h>
#include <MetaOptions.h>
#include <ModuleIdentity.h>

#include <cstddef>
#include <optional>
#include <type_traits>

namespace
{
using pg::meta::TypeId;

static_assert(!TypeId{}.IsValid());
constexpr TypeId kId{.Bytes = {std::byte{1}}};
static_assert(kId.IsValid());
static_assert(TypeId{} < kId);
static_assert(!std::is_convertible_v<TypeId, pg::module::ModuleId>);
static_assert(!std::is_convertible_v<pg::module::ModuleId, TypeId>);

constexpr pg::meta::TypeInfo kAnonymous{.Name = "Actor", .Id = std::nullopt, .Fields = {}};
static_assert(!kAnonymous.Id.has_value());
constexpr pg::meta::TypeInfo kStable{.Name = "Actor", .Id = kId, .Fields = {}};
static_assert(kStable.Id.has_value());
static_assert(*kStable.Id == kId);
static_assert(kStable.Id->IsValid());
constexpr pg::meta::FieldOptions kOptions;
static_assert(kOptions.Key == nullptr);
static_assert(!kOptions.ReadOnly);
} // namespace
