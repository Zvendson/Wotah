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
 *
 *     constexpr Wotah::LayoutChange ToReadable{VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
 *                                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, ...};
 *     Wotah::RecordLayoutChange(commands, image, ToReadable);
 *
 *     Wotah::DrawTarget color;   // the image drawn into, cleared first
 *     color.View   = view;
 *     color.Layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
 *     Wotah::RecordBeginDrawing(commands, extent, color, Wotah::DrawTarget{});   // no depth image
 */
#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>

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

    /** An image's change from one layout to another, with what must finish before it and what waits after it. */
    struct LayoutChange
    {
        /** The layout the image is in; UNDEFINED drops its content. */
        VkImageLayout From = VK_IMAGE_LAYOUT_UNDEFINED;
        /** The layout it goes to. */
        VkImageLayout To = VK_IMAGE_LAYOUT_UNDEFINED;
        /** The accesses that must be done before the change. */
        VkAccessFlags SourceAccess = 0;
        /** The accesses that wait for it. */
        VkAccessFlags TargetAccess = 0;
        /** The stages that must be done before the change. */
        VkPipelineStageFlags SourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        /** The stages that wait for it. */
        VkPipelineStageFlags TargetStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        /** Which part of the image: color or depth. */
        VkImageAspectFlags Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        /** The first layer changed, and how many from it (VK_REMAINING_ARRAY_LAYERS: all the rest). */
        uint32_t FirstLayer = 0;
        uint32_t LayerCount = VK_REMAINING_ARRAY_LAYERS;
    };

    /** Records a layout change of an image's layers (all of them unless change says), every mip level. */
    void RecordLayoutChange(VkCommandBuffer commandBuffer, VkImage image, const LayoutChange& change);

    /**
     * One image a rendering draws into (RecordBeginDrawing): its view (VK_NULL_HANDLE: none), the layout it is in
     * meanwhile, whether it starts cleared to Clear (or with what it holds), and whether what is drawn is kept (or the
     * image only read, as a depth test's depth).
     */
    struct DrawTarget
    {
        VkImageView   View      = VK_NULL_HANDLE;
        VkImageLayout Layout    = VK_IMAGE_LAYOUT_UNDEFINED;
        bool          IsCleared = true;
        VkClearValue  Clear{};
        bool          IsKept = true;
    };

    /**
     * Begins a rendering (dynamic rendering, one layer) of an area extent large into a color and a depth image (either
     * may have no view), with viewport and scissor covering it, as every pipeline takes them per draw
     * (render/pipeline.hpp). End it with vkCmdEndRendering.
     */
    void RecordBeginDrawing(VkCommandBuffer commandBuffer, VkExtent2D extent, const DrawTarget& color,
                            const DrawTarget& depth);

    /**
     * A sampler of one mip level that clamps at the image's edges, filtering as asked (nearest: texel by texel;
     * linear: blended between texels); VK_NULL_HANDLE (and logged as "Could not <action>") when Vulkan refuses.
     */
    [[nodiscard]] VkSampler CreateClampedSampler(VkDevice device, VkFilter filter, const char* action);

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
