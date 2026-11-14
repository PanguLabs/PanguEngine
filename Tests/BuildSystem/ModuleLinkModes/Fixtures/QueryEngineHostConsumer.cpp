#include <EngineHostModule.h>
#include <ModuleDescriptor.h>
#include <PanguEngineHostExport.h>

extern "C" PG_ENGINE_HOST_API const pg::module::ModuleDefinition* PanguModuleQueryV1() noexcept;

int main()
{
    const auto* definition = PanguModuleQueryV1();
    return definition == pg::engine::QueryEngineHostModule() &&
                   definition->Descriptor.Id == pg::engine::kEngineHostModuleId
               ? 0
               : 1;
}
