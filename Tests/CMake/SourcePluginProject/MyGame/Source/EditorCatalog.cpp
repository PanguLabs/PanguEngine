#include "EditorExtrasModule.h"
#include "FixtureModules.h"
#include "ModuleDescriptor.h"
#include "PhysicsModule.h"

#include <array>

namespace pg::fixture
{
module::StaticModuleCatalog EditorCatalog() noexcept
{
    static const std::array kEntries{
        module::StaticCatalogEntry{QueryEditorExtras},
        module::StaticCatalogEntry{QueryPhysics},
    };
    return {kEntries};
}
} // namespace pg::fixture
