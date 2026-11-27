/**
 * @file water_renderer.hpp
 * Draws the sea (water/sea.hpp) to the horizon around the viewer: the clipmap's grid (render/clipmap.hpp) at the
 * sea's height, moved by its waves and its surf (shaders/water.vert, sea_waves.glsl; the waves' numbers come from
 * render/sea_state.hpp), shaded as clear water with fine moving ripples, foam on its breaking waves and white crests
 * (shaders/water.frag): it mirrors
 * what stands above it (the mirror's picture, render/mirror_view.hpp) and shows what lies under it, bent by its
 * surface and tinted as deep as it is there, with foam where it runs shallow (the solid scene's depth and colors,
 * render/scene_behind.hpp). While the viewer is under the surface, everything it sees is tinted by the water first
 * (shaders/underwater.frag). It lies over the solid scene, so it is drawn after Renderer::BeginSeeThrough.
 *
 *     Wotah::WaterRenderer water;
 *     water.Create(device, colorFormat, depthFormat, lighting, renderer.GetSceneBehind(), renderer.GetMirror(),
 *                  seaState);
 *     // every frame, after Renderer::BeginFrame (and seaState.Update(sea)):
 *     water.FollowViewer(camera.GetPosition());
 *     // after the solid scene and renderer.BeginSeeThrough():
 *     water.Draw(commands, camera.GetViewProjection(), lighting, renderer.GetSceneBehind(), renderer.GetMirror(),
 *                seaState, sea);
 */
#pragma once

#include "render/clipmap_levels.hpp"
#include "render/pipeline.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <vulkan/vulkan_core.h>

#include <array>
#include <cstdint>

namespace Wotah
{
    class Device;
    class Lighting;
    class MirrorView;
    class SceneBehind;
    class SeaState;
    struct Sea;

    /**
     * The set indices the water's shaders read the solid scene behind and the mirror's picture from (lights at set 0,
     * the sea at SeaStateSet, render/sea_state.hpp).
     */
    inline constexpr uint32_t WaterSceneBehindSet = 1;
    inline constexpr uint32_t WaterMirrorSet      = 2;

    /**
     * The sea's GPU side: the clipmap's grid and the pipeline that draws it over the solid scene. The destructor runs
     * Destroy, so nothing leaks when Create fails part way.
     */
    class WaterRenderer final
    {
    public:
        WaterRenderer() = default;
        ~WaterRenderer();

        WaterRenderer(const WaterRenderer&)            = delete;
        WaterRenderer& operator=(const WaterRenderer&) = delete;

        /**
         * Uploads the grid and builds the pipeline for frames with these color and depth formats, reading the lights,
         * the solid scene behind, the mirror and the sea (their set layouts); false (and logged) when one fails. The
         * device must outlive it.
         */
        [[nodiscard]] bool Create(const Device& device, VkFormat colorFormat, VkFormat depthFormat,
                                  const Lighting& lighting, const SceneBehind& behind, const MirrorView& mirror,
                                  const SeaState& sea);

        /** Frees everything Create made; safe to call more than once or after a failed Create. */
        void Destroy();

        /** Moves the clipmap's levels around the viewer, for the draws that follow; under the surface, Draw tints. */
        void FollowViewer(const glm::vec3& viewer);

        /**
         * Records the sea, seen through viewProjection, lit by lighting, over the solid scene behind shows, mirroring
         * what mirror shows, its waves from seaState, which must hold sea (SeaState::Update). Only after
         * Renderer::BeginSeeThrough (or SceneBehind::Record), as it reads both.
         */
        void Draw(VkCommandBuffer commandBuffer, const glm::mat4& viewProjection, const Lighting& lighting,
                  const SceneBehind& behind, const MirrorView& mirror, const SeaState& seaState, const Sea& sea) const;

    private:
        ClipmapLevels m_levels;
        /** The surface seen from above (its top side), and from below (its underside). */
        Pipeline m_pipeline;
        Pipeline m_belowPipeline;

        /** Tints everything seen while the viewer is under the surface. */
        Pipeline  m_underwaterPipeline;
        glm::vec3 m_viewer{0.0f};
    };
} // namespace Wotah
