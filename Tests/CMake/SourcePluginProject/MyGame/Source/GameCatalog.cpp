#include "FixtureModules.h"
#include "ModuleDescriptor.h"

#include <array>

namespace pg::fixture
{
module::StaticModuleCatalog GameCatalog() noexcept
{
    // 依赖者在前，验证 Host 按依赖顺序注册；函数指针保留静态库引用。
    static constexpr std::array kEntries{
        module::StaticCatalogEntry{QueryGameplay},
        module::StaticCatalogEntry{QueryPhysics},
    };
    return {kEntries};
}
} // namespace pg::fixture
