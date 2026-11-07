#include "SharedFixtureContract.h"

#include <ModuleDescriptor.h>

namespace pg::module::fixture
{
const ModuleDefinition* QueryNullDefinition() noexcept { return nullptr; }
} // namespace pg::module::fixture
