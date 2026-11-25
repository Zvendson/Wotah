/**
 * @file sky.hpp
 * The sky behind everything: a clear blue gradient and the sun with its glow, computed per pixel (shaders/sky.frag), so
 * it needs no picture, is sharp at any size, and the sun always stands where the sun light comes from.
 *
 *     Wotah::Sky sky;
 *     sky.Create(device, swapchain.GetFormat(), renderer.GetDepthFormat());
 *     // first thing in a frame, before everything else is drawn over it:
 *     sky.Draw(commands, camera.GetViewProjection(), sunLight);
 */
#pragma once

#include "render/pipeline.hpp"

#include <glm/mat4x4.hpp>
#include <vulkan/vulkan_core.h>

namespace Wotah
{
    class Device;
    struct DirectionalLight;

    /**
     * The sky's pipeline. It neither tests nor writes depth, so it is drawn first and everything after it covers it.
     * The destructor runs Destroy, so nothing leaks when Create fails part way.
     */
    class Sky final
    {
    public:
        /**
         * Builds the sky's pipeline for frames with these color and depth formats (Renderer's); false (and logged)
         * when Vulkan refuses. The device must outlive the sky.
         */
        [[nodiscard]] bool Create(const Device& device, VkFormat colorFormat, VkFormat depthFormat);

        /** Frees the pipeline; safe to call more than once or after a failed Create. */
        void Destroy();

        /**
         * Records the sky as seen through the camera, with the sun where sun shines from and in its color. Draw it
         * before everything else in the frame.
         */
        void Draw(VkCommandBuffer commandBuffer, const glm::mat4& viewProjection, const DirectionalLight& sun) const;

    private:
        Pipeline m_pipeline;
    };
} // namespace Wotah
