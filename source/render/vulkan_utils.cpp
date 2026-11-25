/**
 * @file vulkan_utils.cpp
 * The Vulkan helpers declared in vulkan_utils.hpp.
 */
#include "render/vulkan_utils.hpp"

#include "core/logger.hpp"
#include "render/device.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>

namespace
{
    /** No time limit when waiting for one-time commands to finish. */
    constexpr uint64_t NoTimeout = UINT64_MAX;
} // namespace

bool Wotah::Succeeded(VkResult result, const char* action)
{
    if (result == VK_SUCCESS)
    {
        return true;
    }
    Logger::Error("Could not {} (VkResult {})", action, static_cast<int>(result));
    return false;
}

Wotah::OneTimeCommands::~OneTimeCommands()
{
    Free();
}

bool Wotah::OneTimeCommands::Begin(const Device& device)
{
    Free(); // a second Begin starts over instead of leaking the first
    m_device = device.GetHandle();
    m_queue  = device.GetQueue();

    VkCommandPoolCreateInfo poolInfo =
        Wotah::MakeVulkanInfo<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
    poolInfo.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT; // lives only for these commands
    poolInfo.queueFamilyIndex = device.GetQueueFamily();
    if (!Succeeded(vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_pool), "create a one-time command pool"))
    {
        return false;
    }

    VkCommandBufferAllocateInfo bufferInfo =
        Wotah::MakeVulkanInfo<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
    bufferInfo.commandPool        = m_pool;
    bufferInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    bufferInfo.commandBufferCount = 1;
    if (!Succeeded(vkAllocateCommandBuffers(m_device, &bufferInfo, &m_commands), "allocate a one-time command buffer"))
    {
        return false;
    }

    VkCommandBufferBeginInfo beginInfo =
        Wotah::MakeVulkanInfo<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    return Succeeded(vkBeginCommandBuffer(m_commands, &beginInfo), "start recording one-time commands");
}

bool Wotah::OneTimeCommands::SubmitAndWait()
{
    if (m_commands == VK_NULL_HANDLE)
    {
        Logger::Error("One-time commands were submitted without a successful Begin");
        return false;
    }

    // Each step runs only when the ones before it worked; the fence and the pool are freed at the end in any case.
    bool isDone = Succeeded(vkEndCommandBuffer(m_commands), "finish recording one-time commands");

    VkFenceCreateInfo fenceInfo = Wotah::MakeVulkanInfo<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
    VkFence           fence     = VK_NULL_HANDLE;
    isDone = isDone && Succeeded(vkCreateFence(m_device, &fenceInfo, nullptr, &fence), "create a one-time fence");

    VkSubmitInfo submitInfo       = Wotah::MakeVulkanInfo<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &m_commands;
    isDone = isDone && Succeeded(vkQueueSubmit(m_queue, 1, &submitInfo, fence), "submit one-time commands");
    isDone =
        isDone && Succeeded(vkWaitForFences(m_device, 1, &fence, VK_TRUE, NoTimeout), "wait for one-time commands");

    vkDestroyFence(m_device, fence, nullptr); // accepts VK_NULL_HANDLE (Vulkan spec)
    Free();
    return isDone;
}

void Wotah::OneTimeCommands::Free() noexcept
{
    if (m_device != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(m_device, m_pool, nullptr); // frees the command buffer too; accepts VK_NULL_HANDLE
    }
    m_device   = VK_NULL_HANDLE;
    m_queue    = VK_NULL_HANDLE;
    m_pool     = VK_NULL_HANDLE;
    m_commands = VK_NULL_HANDLE;
}
