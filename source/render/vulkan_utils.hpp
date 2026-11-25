/**
 * @file vulkan_utils.hpp
 * Small Vulkan helpers that more than one part of render/ uses. Every reusable Vulkan helper lives here, once; helpers
 * without Vulkan go into core/utils.hpp.
 *
 *     VkFenceCreateInfo info = Wotah::MakeVulkanInfo<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
 *     if (!Wotah::Succeeded(vkCreateFence(device, &info, nullptr, &fence), "create the frame fence"))
 *     {
 *         return false;
 *     }
 */
#pragma once

#include <vulkan/vulkan_core.h>

namespace Wotah
{
    /**
     * True for VK_SUCCESS; anything else is logged as "Could not <action> (VkResult <code>)".
     * The action reads as the end of that sentence, e.g. "create the swapchain".
     */
    bool Succeeded(VkResult result, const char* action);

    /**
     * A Vulkan info struct with every field zero (no pNext, no flags, no pointers) and its sType set. The short form
     * Info{type} leaves the same zeros, but GCC and Clang warn about the fields it leaves out (-Wextra).
     */
    template <typename Info>
    [[nodiscard]] constexpr Info MakeVulkanInfo(VkStructureType type) noexcept
    {
        Info info{};
        info.sType = type;
        return info;
    }
} // namespace Wotah
