#include "IndexedQuadFixture.h"

#include <PanguApplication.h>
#include <RendererTypes.h>
#include <Result.h>

namespace pg::app
{
constexpr renderer::IndexedQuad kIndexedQuad = kFixtureQuad;

Result<LoopControl> OnFrame(GraphicsContext& graphics) noexcept
{
    return RunIndexedQuad(graphics, [](GraphicsContext& context) { return context.PresentIndexedMesh({.A = 1.0F}); });
}
} // namespace pg::app
