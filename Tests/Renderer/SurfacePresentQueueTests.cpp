#include <gtest/gtest.h>

#include "Backends/SurfacePresentQueue.h"

#include <Common_3/Graphics/ThirdParty/OpenSource/VulkanSDK/include/vulkan/vulkan_core.h>
#include <cstdint>

namespace pg::renderer::detail
{
namespace
{
void TwoFamilies(VkPhysicalDevice /*gpu*/, std::uint32_t* count, VkQueueFamilyProperties* /*properties*/)
{
    *count = 2;
}

TEST(SurfacePresentQueueTest, AcceptsASeparatePresentationFamily)
{
    const auto support = [](VkPhysicalDevice, std::uint32_t family, VkSurfaceKHR, VkBool32* present)
    {
        *present = family == 1 ? VK_TRUE : VK_FALSE;
        return VK_SUCCESS;
    };
    EXPECT_TRUE(HasSurfacePresentQueue(VK_NULL_HANDLE, VK_NULL_HANDLE, TwoFamilies, support));
}

TEST(SurfacePresentQueueTest, RejectsUnsupportedFamiliesAndFailedQueries)
{
    const auto support = [](VkPhysicalDevice, std::uint32_t family, VkSurfaceKHR, VkBool32* present)
    {
        *present = family == 0 ? VK_FALSE : VK_TRUE;
        return family == 0 ? VK_SUCCESS : VK_ERROR_SURFACE_LOST_KHR;
    };
    EXPECT_FALSE(HasSurfacePresentQueue(VK_NULL_HANDLE, VK_NULL_HANDLE, TwoFamilies, support));
}
} // namespace
} // namespace pg::renderer::detail
