/**
 * @file vulkan_utils.hpp
 * Small Vulkan helpers that more than one part of render/ uses. Every reusable Vulkan helper lives here, once; helpers
 * without Vulkan go into core/utils.hpp.
 *
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
} // namespace Wotah
