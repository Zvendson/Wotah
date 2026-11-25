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
 *
 *     Wotah::OneTimeCommands setup;
 *     if (setup.Begin(device))
 *     {
 *         vkCmdCopyBuffer(setup.Get(), source, target, 1, &region);
 *         setup.SubmitAndWait();
 *     }
 */
#pragma once

#include <vulkan/vulkan_core.h>

namespace Wotah
{
    class Device;

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

    /**
     * A command buffer recorded once and run right away, for setup work outside the frames (uploads, an image's first
     * content): Begin, record into Get, then SubmitAndWait. The destructor frees everything, so nothing leaks when a
     * step fails. Not for the frame loop: it waits until the GPU is done.
     */
    class OneTimeCommands final
    {
    public:
        OneTimeCommands() = default;
        ~OneTimeCommands();

        OneTimeCommands(const OneTimeCommands&)            = delete;
        OneTimeCommands& operator=(const OneTimeCommands&) = delete;

        /**
         * Creates a command pool and buffer on the device's queue and starts recording; false (and logged) when Vulkan
         * refuses. The device must outlive this object.
         */
        [[nodiscard]] bool Begin(const Device& device);

        /** The command buffer to record into, between Begin and SubmitAndWait. */
        [[nodiscard]] VkCommandBuffer Get() const noexcept { return m_commands; }

        /**
         * Stops recording, runs the commands and waits until the GPU has finished them; false (and logged) when a step
         * fails. Frees the command buffer either way.
         */
        [[nodiscard]] bool SubmitAndWait();

    private:
        /** Frees the pool, which frees the command buffer with it. */
        void Free() noexcept;

        /** The device and queue the commands run on; not owned. */
        VkDevice m_device = VK_NULL_HANDLE;
        VkQueue  m_queue  = VK_NULL_HANDLE;

        VkCommandPool   m_pool     = VK_NULL_HANDLE;
        VkCommandBuffer m_commands = VK_NULL_HANDLE; // freed with the pool
    };
} // namespace Wotah
