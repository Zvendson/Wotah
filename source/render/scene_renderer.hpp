/**
 * @file scene_renderer.hpp
 * The scene on the GPU and how a frame of it is drawn: the lights (with their shadow maps), the sky, the land, the sea
 * and the pipeline that draws meshes into the shadow maps. Draw records the scene into the renderer's frame
 * (render/renderer.hpp) in its order: every light's shadow maps, what the sea mirrors, then the sky, the land and the
 * meshes, then the sea over them. The UI goes after it, then Renderer::EndFrame.
 *
 *     Wotah::SceneRenderer scene;
 *     scene.Create(device, colorFormat, renderer, landShape, sea);
 *     // each frame, between Renderer::BeginFrame and EndFrame:
 *     scene.Draw(renderer, camera, lights, objects);
 */
#pragma once

#include "render/lighting.hpp"
#include "render/mesh_pipelines.hpp"
#include "render/pipeline.hpp"
#include "render/sky.hpp"
#include "render/terrain_renderer.hpp"
#include "render/water.hpp"
#include "render/water_renderer.hpp"

#include <vulkan/vulkan_core.h>

#include <span>

namespace Wotah
{
    class Camera;
    class Device;
    class Renderer;
    struct TerrainShape;

    /** The scene's lights, sky, land and sea on the GPU, and the recording of a frame of them. */
    class SceneRenderer final
    {
    public:
        /**
         * Builds everything for the renderer's frames (colorFormat: its swapchain's): the land of landShape, the sea
         * at sea, reading the renderer's copy of the solid scene and its mirror; false (and logged) when a part fails.
         * The device and the renderer must outlive it.
         */
        [[nodiscard]] bool Create(const Device& device, VkFormat colorFormat, const Renderer& renderer,
                                  const TerrainShape& landShape, const Sea& sea);

        /** Frees everything; safe to call more than once or after a failed Create. */
        void Destroy();

        /** The lights on the GPU: meshes' pipelines read them (MeshPipelines::Create). */
        [[nodiscard]] const Lighting& GetLighting() const noexcept { return m_lighting; }

        /**
         * Records the scene into the renderer's frame (after Renderer::BeginFrame): land and sea follow the camera,
         * the lights are put on the GPU (bad ones logged, the last good ones kept), every light's shadow maps get the
         * land within lights.Shadows and the objects, the sea's mirror what stands over the water, then the sky, the
         * land and the objects, and last the sea. The renderer is left in its see-through part, ready for the UI.
         */
        void Draw(Renderer& renderer, const Camera& camera, const SceneLights& lights,
                  std::span<const MeshObject> objects);

    private:
        /** Records every shadow map of every light that casts shadows, each cascade: the objects and the land. */
        void RecordShadows(VkCommandBuffer commandBuffer, std::span<const MeshObject> objects, float landReach) const;

        /**
         * Records what the sea mirrors through the mirrored view: the sky without the sun's disk (the water adds the
         * sun's glints itself, from its ripples), the land and the objects.
         */
        void RecordMirror(VkCommandBuffer commandBuffer, const glm::mat4& mirrored, const SceneLights& lights,
                          std::span<const MeshObject> objects) const;

        Lighting        m_lighting;
        Sky             m_sky;
        TerrainRenderer m_land;
        WaterRenderer   m_sea;

        /** Draws the meshes into the lights' shadow maps (the land draws itself). */
        Pipeline m_shadowPipeline;

        /** Where the sea's surface lies. */
        Sea m_seaSurface;
    };
} // namespace Wotah
