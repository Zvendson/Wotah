/**
 * @file swapchain.cpp
 * Creates and frees the swapchain and its image views, and takes and shows its images.
 */
#include "render/swapchain.hpp"

#include "core/logger.hpp"
#include "platform/window.hpp"
#include "render/device.hpp"
#include "render/vulkan_utils.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace
{
    /** No time limit when waiting for the next image: vsync paces the loop. */
    constexpr uint64_t NoTimeout = UINT64_MAX;

    /**
     * currentExtent's width when the system leaves the image size to the swapchain (Vulkan spec,
     * VkSurfaceCapabilitiesKHR).
     */
    constexpr uint32_t SizeChosenBySwapchain = UINT32_MAX;

    /** The surface formats the GPU offers for the surface; empty (and logged) on failure. */
    std::vector<VkSurfaceFormatKHR> FindSurfaceFormats(VkPhysicalDevice gpu, VkSurfaceKHR surface)
    {
        uint32_t count = 0;
        if (!Wotah::Succeeded(vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &count, nullptr),
                              "list the surface formats"))
        {
            return {};
        }
        std::vector<VkSurfaceFormatKHR> formats(count);
        // VK_INCOMPLETE: the list changed between the two calls; the formats that fit are still good.
        const VkResult result = vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &count, formats.data());
        if (result != VK_INCOMPLETE && !Wotah::Succeeded(result, "list the surface formats"))
        {
            return {};
        }
        formats.resize(count);
        return formats;
    }

    /** The image size: the surface's own, or the window's drawable size where the system leaves it to us (Wayland). */
    VkExtent2D ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities, const Wotah::Window& window)
    {
        if (capabilities.currentExtent.width != SizeChosenBySwapchain)
        {
            return capabilities.currentExtent;
        }
        const Wotah::WindowSize framebufferSize = window.GetFramebufferSize();
        return {framebufferSize.Width, framebufferSize.Height};
    }

    /** One image more than the minimum, so the GPU rarely waits for the screen, within the surface's maximum. */
    uint32_t ChooseImageCount(const VkSurfaceCapabilitiesKHR& capabilities)
    {
        const uint32_t count = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0 && count > capabilities.maxImageCount) // 0 means "no maximum"
        {
            return capabilities.maxImageCount;
        }
        return count;
    }
} // namespace

Wotah::Swapchain::~Swapchain()
{
    Destroy();
}

bool Wotah::Swapchain::Create(const Device& device, const Window& window)
{
    m_device = device.GetHandle();

    const VkPhysicalDevice gpu     = device.GetGpu();
    const VkSurfaceKHR     surface = device.GetSurface();

    VkSurfaceCapabilitiesKHR capabilities;
    if (!Succeeded(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &capabilities),
                   "read the surface's capabilities"))
    {
        return false;
    }

    const std::vector<VkSurfaceFormatKHR> formats = FindSurfaceFormats(gpu, surface);
    if (formats.empty())
    {
        Logger::Error("The surface offers no image format");
        return false;
    }
    // shortcut: takes the first listed format as before, whatever its color space; prefer an sRGB format once colors
    // are tuned for it.
    m_format = formats.front();
    m_extent = ChooseExtent(capabilities, window);

    VkSwapchainCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    createInfo.surface          = surface;
    createInfo.minImageCount    = ChooseImageCount(capabilities);
    createInfo.imageFormat      = m_format.format;
    createInfo.imageColorSpace  = m_format.colorSpace;
    createInfo.imageExtent      = m_extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; // one queue draws and presents
    createInfo.preTransform     = capabilities.currentTransform;
    createInfo.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode      = VK_PRESENT_MODE_FIFO_KHR; // vsync, the one mode every system supports (Vulkan spec)
    createInfo.clipped          = VK_TRUE;
    if (!Succeeded(vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapchain), "create the swapchain"))
    {
        return false;
    }

    uint32_t count = 0;
    if (!Succeeded(vkGetSwapchainImagesKHR(m_device, m_swapchain, &count, nullptr), "list the swapchain images"))
    {
        return false;
    }
    m_images.resize(count);
    if (!Succeeded(vkGetSwapchainImagesKHR(m_device, m_swapchain, &count, m_images.data()),
                   "list the swapchain images"))
    {
        return false;
    }
    m_images.resize(count);

    // Views are added one by one, so a failure part way leaves only real handles behind for Destroy.
    m_views.reserve(m_images.size());
    for (VkImage image : m_images)
    {
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image            = image;
        viewInfo.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format           = m_format.format;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}; // the color, one mip level, one layer

        VkImageView view = VK_NULL_HANDLE;
        if (!Succeeded(vkCreateImageView(m_device, &viewInfo, nullptr, &view), "create a swapchain image view"))
        {
            return false;
        }
        m_views.push_back(view);
    }
    return true;
}

void Wotah::Swapchain::Destroy()
{
    if (m_device != VK_NULL_HANDLE)
    {
        // The views before the swapchain whose images they show.
        for (VkImageView view : m_views)
        {
            vkDestroyImageView(m_device, view, nullptr);
        }
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr); // accepts VK_NULL_HANDLE (Vulkan spec)
    }

    m_device    = VK_NULL_HANDLE;
    m_format    = {};
    m_extent    = {};
    m_swapchain = VK_NULL_HANDLE;
    m_images.clear();
    m_views.clear();
}

std::optional<uint32_t> Wotah::Swapchain::AcquireNextImage(VkSemaphore imageAvailable)
{
    uint32_t       index = 0;
    const VkResult result =
        vkAcquireNextImageKHR(m_device, m_swapchain, NoTimeout, imageAvailable, VK_NULL_HANDLE, &index);
    // Suboptimal still gives a usable image. shortcut: any other result only skips the frame; make the swapchain
    // again on VK_ERROR_OUT_OF_DATE_KHR once the window can be resized.
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        return std::nullopt;
    }
    return index;
}

void Wotah::Swapchain::Present(VkQueue queue, VkSemaphore renderFinished, uint32_t imageIndex)
{
    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = &renderFinished;
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &m_swapchain;
    presentInfo.pImageIndices      = &imageIndex;

    // shortcut: the result is ignored as before, since a fixed-size window keeps its swapchain valid; handle
    // VK_ERROR_OUT_OF_DATE_KHR together with resizing.
    (void)vkQueuePresentKHR(queue, &presentInfo);
}

VkImage Wotah::Swapchain::GetImage(uint32_t index) const noexcept
{
    return index < m_images.size() ? m_images[index] : VK_NULL_HANDLE;
}

VkImageView Wotah::Swapchain::GetView(uint32_t index) const noexcept
{
    return index < m_views.size() ? m_views[index] : VK_NULL_HANDLE;
}
