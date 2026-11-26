/**
 * @file scene_depth.hpp
 * The solid scene's depth as shaders can read it: for what is drawn after the solid scene and lies over it (water), how
 * far the solid scene is behind each pixel. A sampler and a descriptor set (binding 0: the depth image, read by
 * fragment shaders with texelFetch) that point at a depth image; Point it again whenever the image is made again.
 *
 *     Wotah::SceneDepth sceneDepth;
 *     sceneDepth.Create(device);
 *     sceneDepth.Point(depthImage);              // the image needs VK_IMAGE_USAGE_SAMPLED_BIT
 *     // after the solid scene, between two renderings into the same image:
 *     Wotah::RecordDepthReadable(commands, depthImage.GetHandle());
 *     sceneDepth.Bind(commands, waterPipeline.GetLayout(), 1);
 */
#pragma once

#include "render/descriptor_set.hpp"

#include <vulkan/vulkan_core.h>

#include <cstdint>

namespace Wotah
{
    class Device;
    class Image;

    /**
     * The layout a depth image is in while depth tests read it and shaders sample it, with nothing writing it: what
     * RecordDepthReadable changes it to, and what the drawing that follows names for its depth attachment.
     */
    inline constexpr VkImageLayout ReadableDepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    /**
     * Records the change of a depth image from being drawn into (DEPTH_STENCIL_ATTACHMENT_OPTIMAL) to
     * ReadableDepthLayout: every depth write so far finishes before fragment shaders read it and later depth tests use
     * it. Outside of a rendering: end it before, begin the next after.
     */
    void RecordDepthReadable(VkCommandBuffer commandBuffer, VkImage depth);

    /** The sampler and descriptor set that show a depth image to fragment shaders. */
    class SceneDepth final
    {
    public:
        SceneDepth() = default;
        ~SceneDepth();

        SceneDepth(const SceneDepth&)            = delete;
        SceneDepth& operator=(const SceneDepth&) = delete;

        /** Creates the sampler and the set; false (and logged) when Vulkan refuses. The device must outlive it. */
        [[nodiscard]] bool Create(const Device& device);

        /** Frees the set and the sampler; safe to call more than once or after a failed Create. */
        void Destroy();

        /** Points the set at a depth image (made with VK_IMAGE_USAGE_SAMPLED_BIT); not while the GPU reads the set. */
        void Point(const Image& depth);

        /** Binds the set at set index setIndex of a graphics pipeline's layout. */
        void Bind(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, uint32_t setIndex) const;

        /** The set's layout, for pipelines that read the depth; VK_NULL_HANDLE before Create. */
        [[nodiscard]] VkDescriptorSetLayout GetSetLayout() const noexcept { return m_set.GetLayout(); }

    private:
        VkDevice      m_device  = VK_NULL_HANDLE;
        VkSampler     m_sampler = VK_NULL_HANDLE;
        DescriptorSet m_set;
    };
} // namespace Wotah
