#pragma once

#include <ModuleIdentity.h>
#include <ModuleMetaBorrow.h>
#include <PanguModuleExport.h>
#include <Result.h>
#include <typeinfo>

namespace pg::module
{
class ModuleManager;
namespace detail
{
class ModuleMetadataAccess final
{
public:
    [[nodiscard]] PG_MODULE_API static Result<ModuleMetaBorrow> Acquire(const ModuleManager& manager, ModuleId owner,
                                                                        const std::type_info& type) noexcept;
};
} // namespace detail
} // namespace pg::module
