#include "FixtureModules.h"
#include "ModuleDescriptor.h"

#include <array>

namespace pg::fixture
{
module::StaticModuleCatalog EditorCatalog() noexcept
{
    static constexpr std::array kEntries{
        module::StaticCatalogEntry{QueryEditorExtras},
        module::StaticCatalogEntry{QueryPhysics},
    };
    return {kEntries};
}
} // namespace pg::fixture
