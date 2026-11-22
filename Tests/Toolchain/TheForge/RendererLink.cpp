#include <ModuleDescriptor.h>
#include <RendererModule.h>

#include <cstdlib>

int main()
{
    // 引用公开 Query，确保最终链接包含真实 Renderer Module。
    const auto* definition = pg::renderer::QueryRendererModule();
    return definition != nullptr && definition->Descriptor.Id == pg::renderer::kRendererModuleId ? EXIT_SUCCESS
                                                                                                 : EXIT_FAILURE;
}
