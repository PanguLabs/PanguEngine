#include "SharedFixtureContract.h"

#include <ModuleDescriptor.h>
#include <PanguNullQueryExport.h>

namespace pg::module::fixture
{
const ModuleDefinition* QueryNullDefinition() noexcept { return nullptr; }
} // namespace pg::module::fixture

extern "C" PG_NULL_QUERY_API const pg::module::ModuleDefinition* PanguModuleQueryV1() noexcept
{
    return pg::module::fixture::QueryNullDefinition();
}
