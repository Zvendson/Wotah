/**
 * @file water_renderer.hpp
 * Draws the sea (render/water.hpp) to the horizon around the viewer: the clipmap's grid (render/clipmap.hpp) at the
 * sea's height (shaders/water.vert), shaded as clear water that mirrors the sky and shows what lies under it as deep as
 * it is there, read from the solid scene's depth (shaders/water.frag, render/scene_depth.hpp). It lies over the solid
 * scene, so it is drawn after Renderer::BeginSeeThrough.
 *
 *     Wotah::WaterRenderer water;
 *     water.Create(device, colorFormat, depthFormat, lighting, renderer.GetSceneDepth());
 *     // every frame, after the solid scene and renderer.BeginSeeThrough():
 *     water.FollowViewer(camera.GetPosition());
 *     water.Draw(commands, camera.GetViewProjection(), lighting, renderer.GetSceneDepth(), sea);
 */
#pragma once

#include "render/clipmap.hpp"
#include "render/mesh.hpp"
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
    class SceneDepth;
    struct Sea;

    /** The set index water.frag reads the solid scene's depth from, after the lights at set 0. */
    inline constexpr uint32_t WaterSceneDepthSet = 1;

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
         * Uploads the grid and builds the pipeline for frames with these color and depth formats, reading the lights
         * and the scene's depth (its set layout); false (and logged) when one fails. The device must outlive it.
         */
        [[nodiscard]] bool Create(const Device& device, VkFormat colorFormat, VkFormat depthFormat,
                                  const Lighting& lighting, const SceneDepth& sceneDepth);

        /** Frees everything Create made; safe to call more than once or after a failed Create. */
        void Destroy();

        /** Moves the clipmap's levels around the viewer, for the draws that follow. */
        void FollowViewer(const glm::vec3& viewer);

        /**
         * Records the sea, seen through viewProjection, lit by lighting, over the solid scene whose depth sceneDepth
         * shows. Only after Renderer::BeginSeeThrough (or the same layout change), as it reads that depth.
         */
        void Draw(VkCommandBuffer commandBuffer, const glm::mat4& viewProjection, const Lighting& lighting,
                  const SceneDepth& sceneDepth, const Sea& sea) const;

    private:
        Mesh                                    m_grid;
        std::array<MeshRange, ClipmapPartCount> m_parts{};
        Pipeline                                m_pipeline;
        ClipmapPlacement                        m_placement{};
    };
} // namespace Wotah
