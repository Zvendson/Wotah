/**
 * @file image.hpp
 * A 2D image on the GPU with its memory from the Vulkan Memory Allocator and a view to use it through: depth images,
 * shadow maps (a layer per light and cascade), and later textures and offscreen targets.
 *
 *     const std::optional<VkFormat> depthFormat = Wotah::FindDepthFormat(device);
 *     Wotah::Image depth;
 *     depth.Create(device, extent, *depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
 */
#pragma once

#include "render/vulkan_handles.hpp"

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <optional>

namespace Wotah
{
    class Device;

    /**
     * A depth format the GPU can draw depth into: 32-bit float when supported (the most precise), else 24 or 16 bits.
     * Empty (and logged) when none works. Formats without stencil only, as nothing uses stencil yet.
     */
    [[nodiscard]] std::optional<VkFormat> FindDepthFormat(const Device& device);

    /**
     * A 2D image in GPU-only memory, one mip level and one or more layers, and a view of all of it. The destructor runs
     * Destroy, so nothing leaks when Create fails part way. The GPU must be done with the image before it is destroyed.
     */
    class Image final
    {
    public:
        Image() = default;
        ~Image();

        Image(const Image&)            = delete;
        Image& operator=(const Image&) = delete;

        /**
         * Creates the image and its view for the given uses (VK_IMAGE_USAGE_*); depth formats get a depth view, others
         * a color view. With more than one layer the view is an array of all of them. Replaces an image created
         * before. False (and logged) for a size or layer count of 0 or when Vulkan refuses. The device must outlive
         * the image.
         */
        [[nodiscard]] bool Create(const Device& device, VkExtent2D extent, VkFormat format, VkImageUsageFlags usage,
                                  uint32_t layerCount = 1);

        /** Frees the view, the image and its memory; safe to call more than once or after a failed Create. */
        void Destroy();

        /** The image, for barriers and copies. */
        [[nodiscard]] VkImage GetHandle() const noexcept { return m_image; }

        /** The view of the whole image (all layers), for drawing into it or reading from it. */
        [[nodiscard]] VkImageView GetView() const noexcept { return m_view; }

        /** Its pixel format. */
        [[nodiscard]] VkFormat GetFormat() const noexcept { return m_format; }

        /** Its size in pixels. */
        [[nodiscard]] VkExtent2D GetExtent() const noexcept { return m_extent; }

        /** How many layers it has. */
        [[nodiscard]] uint32_t GetLayerCount() const noexcept { return m_layerCount; }

        /** The aspect its view covers: depth for depth formats, color otherwise; for barriers. */
        [[nodiscard]] VkImageAspectFlags GetAspect() const noexcept { return m_aspect; }

    private:
        /** The device and allocator the image came from; not owned. */
        VkDevice     m_device    = VK_NULL_HANDLE;
        VmaAllocator m_allocator = nullptr;

        VkImage            m_image      = VK_NULL_HANDLE;
        VmaAllocation      m_allocation = nullptr;
        VkImageView        m_view       = VK_NULL_HANDLE;
        VkFormat           m_format     = VK_FORMAT_UNDEFINED;
        VkExtent2D         m_extent{};
        VkImageAspectFlags m_aspect     = 0;
        uint32_t           m_layerCount = 0;
    };
} // namespace Wotah
