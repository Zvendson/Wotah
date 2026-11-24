/**
 * @file vulkan_utils.cpp
 * The Vulkan helpers declared in vulkan_utils.hpp.
 */
#include "render/vulkan_utils.hpp"

#include "core/logger.hpp"

bool Wotah::Succeeded(VkResult result, const char* action)
{
    if (result == VK_SUCCESS)
    {
        return true;
    }
    Logger::Error("Could not {} (VkResult {})", action, static_cast<int>(result));
    return false;
}
