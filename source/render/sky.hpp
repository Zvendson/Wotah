/**
 * @file sky.hpp
 * The sky behind everything, by day and night: its colors, the sun with its glow and rays, the full moon and the stars,
 * computed per pixel (shaders/sky.frag), so it needs no picture, is sharp at any size, and sun and moon always stand
 * where their light comes from.
 *
 *     Wotah::Sky sky;
 *     sky.Create(device, swapchain.GetFormat(), renderer.GetDepthFormat());
 *     // first thing in a frame, before everything else is drawn over it:
 *     sky.Draw(commands, camera.GetViewProjection(), sunLight, moonDisk, seconds);
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
     * The sky's small movements (the sun's rays turning, its glow breathing, the stars twinkling) repeat after this
     * many seconds: a clock wrapped at it never makes them jump.
     */
    inline constexpr float SkyLoopSeconds = 3600.0f;

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
         * Records the sky as seen through the camera, with the sun where sun shines from (SkyLights::SunDisk): a
         * round disk, white-hot by day and orange-red when its light is dim and red, with a glow and faint rays
         * around it; a sun with no light shows none. The moon stands where moon shines from: its disk
         * (render/sky_lights.hpp, SkyLights::MoonDisk) with Color its tint and Intensity its brightness, by day too
         * (pale over the blue); none hides it. Sunset colors, night and stars follow the sun's height; the small
         * movements follow seconds (any value; it wraps at SkyLoopSeconds). Draw it before everything else in the
         * frame.
         */
        void Draw(VkCommandBuffer commandBuffer, const glm::mat4& viewProjection, const DirectionalLight& sun,
                  const DirectionalLight& moon, float seconds) const;

    private:
        Pipeline m_pipeline;
    };
} // namespace Wotah
