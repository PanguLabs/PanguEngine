#include "FixtureModules.h"
#include "GameplayModule.h"
#include "ModuleDescriptor.h"
#include "PhysicsModule.h"

#include <array>

namespace pg::fixture
{
module::StaticModuleCatalog GameCatalog() noexcept
{
    // 依赖者在前，验证 Host 按依赖顺序注册；函数指针保留静态库引用。
    static const std::array kEntries{
        module::StaticCatalogEntry{QueryGameplay},
        module::StaticCatalogEntry{QueryPhysics},
    };
    return {kEntries};
}
} // namespace pg::fixture
