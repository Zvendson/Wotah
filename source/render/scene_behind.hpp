/**
 * @file scene_behind.hpp
 * What lies behind see-through surfaces (water), for their fragment shaders: the solid scene's depth and a copy of its
 * colors, both taken once the solid scene is drawn (Renderer::BeginSeeThrough). A shader reads how far the solid scene
 * lies behind a pixel (the depth, texel by texel) and what it looks like there or a little to the side (the colors,
 * filtered), so water can show the ground under it bent by its surface.
 *
 *     // set 1 of the water's pipeline: binding 0 the depth (sampler2D, texelFetch), binding 1 the colors (sampler2D)
 *     Wotah::SceneBehind behind;
 *     behind.Create(device, depthImage, colorFormat);   // again whenever the depth image is made again
 *     // after the solid scene, outside of a rendering:
 *     behind.Record(commands, frameImage, depthImage);
 *     // in the rendering that follows (depth attachment in ReadableDepthLayout):
 *     behind.Bind(commands, waterPipeline.GetLayout(), 1);
 */
#pragma once

#include "render/descriptor_set.hpp"
#include "render/image.hpp"

#include <vulkan/vulkan_core.h>

#include <cstdint>

namespace Wotah
{
    class Device;

    /**
     * The layout the depth image is in while depth tests read it and shaders sample it, with nothing writing it: what
     * SceneBehind::Record changes it to, and what the drawing that follows names for its depth attachment.
     */
    inline constexpr VkImageLayout ReadableDepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    /** The bindings of the set: the solid scene's depth, and the copy of its colors. */
    inline constexpr uint32_t SceneDepthBinding = 0;
    inline constexpr uint32_t SceneColorBinding = 1;

    /**
     * The copy of the frame's colors, the samplers and the descriptor set that show it and the depth image to fragment
     * shaders. The destructor runs Destroy, so nothing leaks when Create fails part way.
     */
    class SceneBehind final
    {
    public:
        SceneBehind() = default;
        ~SceneBehind();

        SceneBehind(const SceneBehind&)            = delete;
        SceneBehind& operator=(const SceneBehind&) = delete;

        /**
         * Creates the color copy (as large as depth, in the frame's colorFormat), the samplers and the set, pointed at
         * depth (made with VK_IMAGE_USAGE_SAMPLED_BIT) and the copy; replaces what an earlier Create made. False (and
         * logged) when Vulkan refuses. The device must outlive it; not while the GPU reads the set.
         */
        [[nodiscard]] bool Create(const Device& device, const Image& depth, VkFormat colorFormat);

        /** Frees everything Create made; safe to call more than once or after a failed Create. */
        void Destroy();

        /**
         * Records the copy of frame (in COLOR_ATTACHMENT_OPTIMAL, as large as the depth, the frame's color format;
         * made with VK_IMAGE_USAGE_TRANSFER_SRC_BIT) and the change of depth from DEPTH_STENCIL_ATTACHMENT_OPTIMAL to
         * ReadableDepthLayout. Every color and depth write so far finishes first; frame is in
         * COLOR_ATTACHMENT_OPTIMAL again afterwards. Outside of a rendering: end it before, begin the next after.
         */
        void Record(VkCommandBuffer commandBuffer, VkImage frame, const Image& depth) const;

        /** Binds the set at set index setIndex of a graphics pipeline's layout. */
        void Bind(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, uint32_t setIndex) const;

        /** The set's layout, for pipelines that read what lies behind; VK_NULL_HANDLE before Create. */
        [[nodiscard]] VkDescriptorSetLayout GetSetLayout() const noexcept { return m_set.GetLayout(); }

    private:
        VkDevice      m_device = VK_NULL_HANDLE;
        Image         m_color;
        VkSampler     m_depthSampler = VK_NULL_HANDLE;
        VkSampler     m_colorSampler = VK_NULL_HANDLE;
        DescriptorSet m_set;
    };
} // namespace Wotah
