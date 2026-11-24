#pragma once

#include <Common_3/Graphics/ThirdParty/OpenSource/VulkanSDK/include/vulkan/vulkan_core.h>

#include <cstdint>

namespace pg::renderer::detail
{
// Forge 为每个非空队列族创建设备队列，呈现队列可以独立于 graphics 队列。
[[nodiscard]] inline bool HasSurfacePresentQueue(VkPhysicalDevice gpu, VkSurfaceKHR surface,
                                                 PFN_vkGetPhysicalDeviceQueueFamilyProperties getFamilies,
                                                 PFN_vkGetPhysicalDeviceSurfaceSupportKHR getSupport) noexcept
{
    std::uint32_t families{};
    getFamilies(gpu, &families, nullptr);
    for (std::uint32_t family = 0; family < families; ++family)
    {
        VkBool32 present{};
        if (getSupport(gpu, family, surface, &present) == VK_SUCCESS && present == VK_TRUE)
        {
            return true;
        }
    }
    return false;
}
} // namespace pg::renderer::detail
