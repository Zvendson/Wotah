/**
 * @file image.cpp
 * Images on top of the Vulkan Memory Allocator, and picking a depth format the GPU supports.
 */
#include "render/image.hpp"

#include "core/logger.hpp"
#include "render/device.hpp"
#include "render/vulkan_utils.hpp"

#include <vulkan/vulkan.h>

#include <vk_mem_alloc.h>

#include <array>

namespace
{
    /**
     * The depth formats tried, most precise first. Vulkan requires D16_UNORM and at least one of X8_D24_UNORM_PACK32
     * and D32_SFLOAT to work as depth attachments (Vulkan spec, "Required Format Support"), so one always fits.
     */
    constexpr std::array<VkFormat, 3> DepthFormats{
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_X8_D24_UNORM_PACK32,
        VK_FORMAT_D16_UNORM,
    };

    /** The part of an image a view of this format covers. */
    VkImageAspectFlags GetAspectOf(VkFormat format)
    {
        for (const VkFormat depthFormat : DepthFormats)
        {
            if (format == depthFormat)
            {
                return VK_IMAGE_ASPECT_DEPTH_BIT;
            }
        }
        return VK_IMAGE_ASPECT_COLOR_BIT;
    }
} // namespace

std::optional<VkFormat> Wotah::FindDepthFormat(const Device& device)
{
    for (const VkFormat format : DepthFormats)
    {
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(device.GetGpu(), format, &properties);
        if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
        {
            return format;
        }
    }
    Logger::Error("The GPU offers no depth format to draw into");
    return std::nullopt;
}

Wotah::Image::~Image()
{
    Destroy();
}

bool Wotah::Image::Create(const Device& device, VkExtent2D extent, VkFormat format, VkImageUsageFlags usage)
{
    Destroy(); // a second Create replaces the first image instead of leaking it
    if (extent.width == 0 || extent.height == 0)
    {
        Logger::Error("An image needs a size above 0, not {}x{}", extent.width, extent.height);
        return false;
    }
    m_device    = device.GetHandle();
    m_allocator = device.GetAllocator();
    m_format    = format;
    m_extent    = extent;
    m_aspect    = GetAspectOf(format);

    VkImageCreateInfo imageInfo = Wotah::MakeVulkanInfo<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    imageInfo.imageType         = VK_IMAGE_TYPE_2D;
    imageInfo.format            = format;
    imageInfo.extent            = {extent.width, extent.height, 1};
    imageInfo.mipLevels         = 1;
    imageInfo.arrayLayers       = 1;
    imageInfo.samples           = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling            = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage             = usage;
    imageInfo.sharingMode       = VK_SHARING_MODE_EXCLUSIVE; // one queue does everything
    imageInfo.initialLayout     = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO; // GPU-only memory, as the CPU never touches an image directly
    if (!Succeeded(vmaCreateImage(m_allocator, &imageInfo, &allocationInfo, &m_image, &m_allocation, nullptr),
                   "create an image"))
    {
        return false;
    }

    VkImageViewCreateInfo viewInfo =
        Wotah::MakeVulkanInfo<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    viewInfo.image            = m_image;
    viewInfo.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format           = format;
    viewInfo.subresourceRange = {m_aspect, 0, 1, 0, 1}; // the whole image: one mip level, one layer
    return Succeeded(vkCreateImageView(m_device, &viewInfo, nullptr, &m_view), "create an image view");
}

void Wotah::Image::Destroy()
{
    // The view before the image it shows; all of these accept null handles (Vulkan spec, VMA docs).
    if (m_device != VK_NULL_HANDLE)
    {
        vkDestroyImageView(m_device, m_view, nullptr);
    }
    if (m_allocator != nullptr)
    {
        vmaDestroyImage(m_allocator, m_image, m_allocation);
    }
    m_device     = VK_NULL_HANDLE;
    m_allocator  = nullptr;
    m_image      = VK_NULL_HANDLE;
    m_allocation = nullptr;
    m_view       = VK_NULL_HANDLE;
    m_format     = VK_FORMAT_UNDEFINED;
    m_extent     = {};
    m_aspect     = 0;
}
