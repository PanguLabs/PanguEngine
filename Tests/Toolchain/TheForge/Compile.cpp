#include <cstdlib>
#include <type_traits>

#include <Common_3/Graphics/Interfaces/IGraphics.h>

static_assert(std::is_same_v<decltype(u8'P'), char8_t>);
static_assert(RENDERER_API_COUNT == 1);

#ifdef _WIN32
static_assert(RENDERER_API_D3D12 < RENDERER_API_COUNT);
#elifdef __APPLE__
static_assert(RENDERER_API_METAL < RENDERER_API_COUNT);
#else
static_assert(RENDERER_API_VULKAN < RENDERER_API_COUNT);
#endif

int main()
{
    // 保留符号引用，链接检查无需启动 GPU。
    auto* const volatile init = &initRenderer;
    auto* const volatile exit = &exitRenderer;
    return init != nullptr && exit != nullptr ? EXIT_SUCCESS : EXIT_FAILURE;
}
