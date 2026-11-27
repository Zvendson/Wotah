/**
 * @file scene_behind.hpp
 * What lies behind see-through surfaces (water), for their fragment shaders: copies of the solid scene's depth and of
 * its colors, both taken once the solid scene is drawn (Renderer::BeginSeeThrough). A shader reads how far the solid
 * scene lies behind a pixel (the depth, texel by texel) and what it looks like there or a little to the side (the
 * colors, filtered), so water can show the ground under it bent by its surface. Being copies, the frame's own depth
 * stays free for the water to test and write: a wave's crest hides the water behind it.
 *
 *     // set 1 of the water's pipeline: binding 0 the depth (sampler2D, texelFetch), binding 1 the colors (sampler2D)
 *     Wotah::SceneBehind behind;
 *     behind.Create(device, depthImage, colorFormat);   // again whenever the depth image is made again
 *     // after the solid scene, outside of a rendering:
 *     behind.Record(commands, frameImage, depthImage);
 *     // in the rendering that follows (depth attachment in DEPTH_STENCIL_ATTACHMENT_OPTIMAL again):
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

    /** The bindings of the set: the copies of the solid scene's depth and of its colors. */
    inline constexpr uint32_t SceneDepthBinding = 0;
    inline constexpr uint32_t SceneColorBinding = 1;

    /**
     * The copies of the frame's depth and colors, the samplers and the descriptor set that show them to fragment
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
         * Creates the copies (as large as depth: its depth format, the frame's colorFormat), the samplers and the set
         * pointed at them; replaces what an earlier Create made. False (and logged) when Vulkan refuses. The device
         * must outlive it; not while the GPU reads the set.
         */
        [[nodiscard]] bool Create(const Device& device, const Image& depth, VkFormat colorFormat);

        /** Frees everything Create made; safe to call more than once or after a failed Create. */
        void Destroy();

        /**
         * Records the copies of frame (in COLOR_ATTACHMENT_OPTIMAL, the frame's color format) and depth (in
         * DEPTH_STENCIL_ATTACHMENT_OPTIMAL, the format Create had), both as large as Create's depth and made with
         * VK_IMAGE_USAGE_TRANSFER_SRC_BIT. Every color and depth write so far finishes first; both are back in their
         * layouts afterwards, ready to be drawn into again. Outside of a rendering: end it before, begin the next
         * after.
         */
        void Record(VkCommandBuffer commandBuffer, VkImage frame, const Image& depth) const;

        /** Binds the set at set index setIndex of a graphics pipeline's layout. */
        void Bind(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, uint32_t setIndex) const;

        /** The set's layout, for pipelines that read what lies behind; VK_NULL_HANDLE before Create. */
        [[nodiscard]] VkDescriptorSetLayout GetSetLayout() const noexcept { return m_set.GetLayout(); }

    private:
        VkDevice      m_device = VK_NULL_HANDLE;
        Image         m_color;
        Image         m_depth;
        VkSampler     m_depthSampler = VK_NULL_HANDLE;
        VkSampler     m_colorSampler = VK_NULL_HANDLE;
        DescriptorSet m_set;
    };
} // namespace Wotah
