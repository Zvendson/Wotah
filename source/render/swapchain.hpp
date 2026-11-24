/**
 * @file swapchain.hpp
 * The images the GPU draws into and the screen shows, one after the other: the swapchain, its images and a view for
 * each image.
 *
 *     Wotah::Swapchain swapchain;
 *     if (swapchain.Create(device, window))
 *     {
 *         if (const std::optional<uint32_t> index = swapchain.AcquireNextImage(imageAvailable))
 *         {
 *             // draw into swapchain.GetView(*index), then:
 *             swapchain.Present(queue, renderFinished, *index);
 *         }
 *     }
 *     swapchain.Destroy();
 */
#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace Wotah
{
    class Device;
    class Window;

    /**
     * The swapchain of the device's surface with one view per image. The destructor runs Destroy, so nothing leaks when
     * Create fails part way.
     */
    class Swapchain final
    {
    public:
        Swapchain() = default;
        ~Swapchain();

        Swapchain(const Swapchain&)            = delete;
        Swapchain& operator=(const Swapchain&) = delete;

        /**
         * Creates the swapchain for the device's surface in the surface's first listed format, at the surface's size
         * (or the window's drawable size where the system leaves the size to us), with vsync, and a view for every
         * image; false (and logged) when a step fails. The device must outlive the swapchain.
         */
        [[nodiscard]] bool Create(const Device& device, const Window& window);

        /** Frees the views and the swapchain; safe to call more than once or after a failed Create. */
        void Destroy();

        /**
         * The index of the next image to draw into; imageAvailable is signalled once the image can be written.
         * Empty when no image could be taken this frame, so the caller skips the frame.
         */
        [[nodiscard]] std::optional<uint32_t> AcquireNextImage(VkSemaphore imageAvailable);

        /** Shows the image at imageIndex once renderFinished is signalled. */
        void Present(VkQueue queue, VkSemaphore renderFinished, uint32_t imageIndex);

        /** The images' pixel format. */
        [[nodiscard]] VkFormat GetFormat() const noexcept { return m_format.format; }

        /** The images' size in pixels. */
        [[nodiscard]] VkExtent2D GetExtent() const noexcept { return m_extent; }

        /** How many images the swapchain has; indexes run from 0 to this minus 1. */
        [[nodiscard]] uint32_t GetImageCount() const noexcept { return static_cast<uint32_t>(m_images.size()); }

        /** The image at index; VK_NULL_HANDLE for an index outside the swapchain. */
        [[nodiscard]] VkImage GetImage(uint32_t index) const noexcept;

        /** The view of the image at index; VK_NULL_HANDLE for an index outside the swapchain. */
        [[nodiscard]] VkImageView GetView(uint32_t index) const noexcept;

    private:
        /** The device the swapchain belongs to; not owned, kept for Destroy and AcquireNextImage. */
        VkDevice m_device = VK_NULL_HANDLE;

        VkSurfaceFormatKHR       m_format{};
        VkExtent2D               m_extent{};
        VkSwapchainKHR           m_swapchain = VK_NULL_HANDLE;
        std::vector<VkImage>     m_images;
        std::vector<VkImageView> m_views; // one per image, same index
    };
} // namespace Wotah
