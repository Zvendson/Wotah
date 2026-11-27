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

void Wotah::RecordLayoutChange(VkCommandBuffer commandBuffer, VkImage image, const LayoutChange& change)
{
    VkImageMemoryBarrier barrier = MakeVulkanInfo<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
    barrier.srcAccessMask        = change.SourceAccess;
    barrier.dstAccessMask        = change.TargetAccess;
    barrier.oldLayout            = change.From;
    barrier.newLayout            = change.To;
    barrier.srcQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                = image;
    barrier.subresourceRange     = {change.Aspect, 0, VK_REMAINING_MIP_LEVELS, change.FirstLayer, change.LayerCount};
    vkCmdPipelineBarrier(commandBuffer, change.SourceStage, change.TargetStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void Wotah::RecordBeginDrawing(VkCommandBuffer commandBuffer, VkExtent2D extent, const DrawTarget& color,
                               const DrawTarget& depth)
{
    const auto toAttachment = [](const DrawTarget& target)
    {
        VkRenderingAttachmentInfo attachment =
            MakeVulkanInfo<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
        attachment.imageView   = target.View;
        attachment.imageLayout = target.Layout;
        attachment.loadOp      = target.IsCleared ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        attachment.storeOp     = target.IsKept ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_NONE;
        attachment.clearValue  = target.Clear;
        return attachment;
    };
    const VkRenderingAttachmentInfo colorAttachment = toAttachment(color);
    const VkRenderingAttachmentInfo depthAttachment = toAttachment(depth);

    VkRenderingInfo rendering      = MakeVulkanInfo<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
    rendering.renderArea           = {{0, 0}, extent};
    rendering.layerCount           = 1;
    rendering.colorAttachmentCount = color.View != VK_NULL_HANDLE ? 1 : 0;
    rendering.pColorAttachments    = color.View != VK_NULL_HANDLE ? &colorAttachment : nullptr;
    rendering.pDepthAttachment     = depth.View != VK_NULL_HANDLE ? &depthAttachment : nullptr;
    vkCmdBeginRendering(commandBuffer, &rendering);

    const VkViewport viewport{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height),
                              0.0f, 1.0f};
    const VkRect2D   scissor{{0, 0}, extent};
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
}

VkSampler Wotah::CreateClampedSampler(VkDevice device, VkFilter filter, const char* action)
{
    VkSamplerCreateInfo info = MakeVulkanInfo<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
    info.magFilter           = filter;
    info.minFilter           = filter;
    info.mipmapMode          = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU        = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeV        = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeW        = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkSampler sampler        = VK_NULL_HANDLE;
    if (!Succeeded(vkCreateSampler(device, &info, nullptr, &sampler), action))
    {
        return VK_NULL_HANDLE;
    }
    return sampler;
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
