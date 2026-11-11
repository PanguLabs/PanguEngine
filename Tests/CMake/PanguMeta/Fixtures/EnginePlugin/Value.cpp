#include <Context.gen.h>
#include <ModuleDescriptor.h>
#include <PanguLayoutValueExport.h>
#include <tuple>

static_assert(std::get<0>(pg::meta::TypeTraits<layout::Context>::Fields).Info.Key ==
              (PANGU_PROJECT_VALUE == 17 ? "A" : "B"));

namespace layout
{
const pg::module::ModuleDefinition* Query() noexcept
{
    static const pg::module::ModuleDefinition kDefinition{
        .Descriptor = {.Id = kModuleId, .Dependencies = {}, .DisplayName = "Layout", .BuildIdentity = {}},
    };
    return &kDefinition;
}
} // namespace layout

// 独立 Host 跨静态/动态库调用此符号，必须保持外部链接。
// NOLINTNEXTLINE(misc-use-internal-linkage)
PG_LAYOUT_VALUE_API int EngineValue();

PG_LAYOUT_VALUE_API int EngineValue() { return PANGU_PROJECT_VALUE; }
