/**
 * @file renderer.cpp
 * The frame loop's Vulkan work: synchronization, recording, layout changes, submitting and presenting.
 */
#include "render/renderer.hpp"

#include "render/device.hpp"
#include "render/swapchain.hpp"
#include "render/vulkan_utils.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>

namespace
{
    /** No time limit when waiting for the previous frame: vsync paces the loop. */
    constexpr uint64_t NoTimeout = UINT64_MAX;

    /** A swapchain image's change from one layout to another, with what must finish before and wait after it. */
    struct LayoutChange
    {
        VkImageLayout        From         = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageLayout        To           = VK_IMAGE_LAYOUT_UNDEFINED;
        VkAccessFlags        SourceAccess = 0;
        VkAccessFlags        TargetAccess = 0;
        VkPipelineStageFlags SourceStage  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkPipelineStageFlags TargetStage  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    };

    /**
     * Before drawing: the old content is not needed (UNDEFINED), and the color writes wait for the image, which the
     * acquire semaphore releases at the color output stage.
     */
    constexpr LayoutChange ToDrawTarget{
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        0,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    };

    /** After drawing: the color writes finish before the image is handed to the screen. */
    constexpr LayoutChange ToPresent{
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,      VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,          0,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
    };

    /** Records a layout change of a whole color image. */
    void ChangeLayout(VkCommandBuffer commandBuffer, VkImage image, const LayoutChange& change)
    {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcAccessMask       = change.SourceAccess;
        barrier.dstAccessMask       = change.TargetAccess;
        barrier.oldLayout           = change.From;
        barrier.newLayout           = change.To;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = image;
        barrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}; // the color, one mip level, one layer
        vkCmdPipelineBarrier(commandBuffer, change.SourceStage, change.TargetStage, 0, 0, nullptr, 0, nullptr, 1,
                             &barrier);
    }

    /** A binary semaphore (not CreateSemaphore, a <windows.h> macro); VK_NULL_HANDLE (and logged) on failure. */
    VkSemaphore CreateBinarySemaphore(VkDevice device)
    {
        VkSemaphoreCreateInfo createInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkSemaphore           semaphore = VK_NULL_HANDLE;
        if (!Wotah::Succeeded(vkCreateSemaphore(device, &createInfo, nullptr, &semaphore), "create a semaphore"))
        {
            return VK_NULL_HANDLE;
        }
        return semaphore;
    }
} // namespace

Wotah::Renderer::~Renderer()
{
    Destroy();
}

bool Wotah::Renderer::Create(const Device& device, Swapchain& swapchain)
{
    m_device    = device.GetHandle();
    m_queue     = device.GetQueue();
    m_swapchain = &swapchain;

    // The command buffer is reset and recorded again every frame.
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = device.GetQueueFamily();
    if (!Succeeded(vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool), "create the command pool"))
    {
        return false;
    }

    VkCommandBufferAllocateInfo bufferInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    bufferInfo.commandPool        = m_commandPool;
    bufferInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    bufferInfo.commandBufferCount = 1;
    if (!Succeeded(vkAllocateCommandBuffers(m_device, &bufferInfo, &m_commandBuffer), "allocate the command buffer"))
    {
        return false;
    }

    m_imageAvailable = CreateBinarySemaphore(m_device);
    if (m_imageAvailable == VK_NULL_HANDLE)
    {
        return false;
    }

    // One "drawn" semaphore per swapchain image, not per frame: the screen may still wait on an image's semaphore
    // when the next frame starts, so it is only reused once that same image comes back from the swapchain.
    const uint32_t imageCount = swapchain.GetImageCount();
    m_renderFinished.reserve(imageCount);
    for (uint32_t index = 0; index < imageCount; ++index)
    {
        const VkSemaphore semaphore = CreateBinarySemaphore(m_device);
        if (semaphore == VK_NULL_HANDLE)
        {
            return false;
        }
        m_renderFinished.push_back(semaphore);
    }

    // Created signalled, so the first BeginFrame does not wait for a frame that never ran.
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    return Succeeded(vkCreateFence(m_device, &fenceInfo, nullptr, &m_frameFence), "create the frame fence");
}

void Wotah::Renderer::Destroy()
{
    if (m_device != VK_NULL_HANDLE)
    {
        // Every vkDestroy* below accepts VK_NULL_HANDLE (Vulkan spec), so a part-way Create is fine.
        vkDestroyFence(m_device, m_frameFence, nullptr);
        for (VkSemaphore semaphore : m_renderFinished)
        {
            vkDestroySemaphore(m_device, semaphore, nullptr);
        }
        vkDestroySemaphore(m_device, m_imageAvailable, nullptr);
        vkDestroyCommandPool(m_device, m_commandPool, nullptr); // frees the command buffer too
    }

    m_device         = VK_NULL_HANDLE;
    m_queue          = VK_NULL_HANDLE;
    m_swapchain      = nullptr;
    m_commandPool    = VK_NULL_HANDLE;
    m_commandBuffer  = VK_NULL_HANDLE;
    m_imageAvailable = VK_NULL_HANDLE;
    m_renderFinished.clear();
    m_frameFence = VK_NULL_HANDLE;
    m_imageIndex = 0;
}

Wotah::FrameStart Wotah::Renderer::BeginFrame()
{
    // ---- Wait for the previous frame, take the next image ----------------------
    if (!Succeeded(vkWaitForFences(m_device, 1, &m_frameFence, VK_TRUE, NoTimeout), "wait for the previous frame"))
    {
        return FrameStart::Failed;
    }
    const std::optional<uint32_t> index = m_swapchain->AcquireNextImage(m_imageAvailable);
    if (!index)
    {
        return FrameStart::Skip; // the fence stays signalled, so the next try does not wait
    }
    m_imageIndex = *index;

    // Reset only now that this frame will be submitted; a skipped frame would leave it unsignalled forever.
    if (!Succeeded(vkResetFences(m_device, 1, &m_frameFence), "reset the frame fence") ||
        !Succeeded(vkResetCommandBuffer(m_commandBuffer, 0), "reset the command buffer"))
    {
        return FrameStart::Failed;
    }

    // ---- Start recording -----------------------------------------------------
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!Succeeded(vkBeginCommandBuffer(m_commandBuffer, &beginInfo), "start recording the frame"))
    {
        return FrameStart::Failed;
    }
    ChangeLayout(m_commandBuffer, m_swapchain->GetImage(m_imageIndex), ToDrawTarget);

    const VkExtent2D extent = m_swapchain->GetExtent();

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView        = m_swapchain->GetView(m_imageIndex);
    color.imageLayout      = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp           = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp          = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = m_clearColor;

    VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    renderingInfo.renderArea           = {{0, 0}, extent};
    renderingInfo.layerCount           = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments    = &color;
    vkCmdBeginRendering(m_commandBuffer, &renderingInfo);

    // Every pipeline takes viewport and scissor per frame (see Pipeline); both cover the whole image.
    const VkViewport viewport{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height),
                              0.0f, 1.0f};
    const VkRect2D   scissor{{0, 0}, extent};
    vkCmdSetViewport(m_commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(m_commandBuffer, 0, 1, &scissor);
    return FrameStart::Ready;
}

bool Wotah::Renderer::EndFrame()
{
    vkCmdEndRendering(m_commandBuffer);
    ChangeLayout(m_commandBuffer, m_swapchain->GetImage(m_imageIndex), ToPresent);
    if (!Succeeded(vkEndCommandBuffer(m_commandBuffer), "finish recording the frame"))
    {
        return false;
    }

    // The drawing waits for the image at the color output stage only; everything before may already run.
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    const VkSemaphore          drawn     = m_renderFinished[m_imageIndex];

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.waitSemaphoreCount   = 1;
    submitInfo.pWaitSemaphores      = &m_imageAvailable;
    submitInfo.pWaitDstStageMask    = &waitStage;
    submitInfo.commandBufferCount   = 1;
    submitInfo.pCommandBuffers      = &m_commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores    = &drawn;
    if (!Succeeded(vkQueueSubmit(m_queue, 1, &submitInfo, m_frameFence), "submit the frame"))
    {
        return false;
    }

    m_swapchain->Present(m_queue, drawn, m_imageIndex);
    return true;
}
