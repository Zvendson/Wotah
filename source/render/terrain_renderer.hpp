/**
 * @file terrain_renderer.hpp
 * Draws the land of a TerrainShape (terrain/terrain_shape.hpp) to the horizon around the viewer: a geometry clipmap
 * (render/clipmap.hpp) whose vertex shader puts every vertex at the land's height, computed on the GPU from the
 * shape's numbers (shaders/terrain_shape.glsl), and a fragment shader that covers it in layers: wet and dry sand, grass
 * and rock (shaders/terrain.frag), each from the ground's textures it makes at Create (render/terrain_materials.hpp).
 * It draws the land into the lights' shadow maps and the sea's mirror too.
 *
 *     Wotah::TerrainRenderer land;
 *     land.Create(device, colorFormat, depthFormat, lighting);
 *     land.SetShape(shape);
 *     // every frame:
 *     land.FollowViewer(camera.GetPosition());
 *     land.DrawShadow(commands, lightViewProjection, reach);  // into each shadow map that is drawn
 *     land.DrawMirrored(commands, *mirrored, lighting);        // into the mirror (render/mirror_view.hpp)
 *     land.Draw(commands, camera.GetViewProjection(), lighting);
 */
#pragma once

#include "render/buffer.hpp"
#include "render/clipmap_levels.hpp"
#include "render/descriptor_set.hpp"
#include "render/pipeline.hpp"
#include "render/terrain_materials.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <vulkan/vulkan_core.h>

#include <array>
#include <cstdint>

namespace Wotah
{
    class Device;
    class Lighting;
    struct TerrainShape;

    /**
     * The set index the land's shaders read the shape from (shaders/terrain_shape.glsl: set 1), after the lights at
     * set 0. Other pipelines that read the shape (tests) bind it there too, with BindShape.
     */
    inline constexpr uint32_t TerrainShapeSet = 1;

    /** The set index the land's fragment shader reads the ground's textures from. */
    inline constexpr uint32_t TerrainMaterialsSet = 2;

    /**
     * The land's GPU side: the shape's uniform buffer and its set, the clipmap's grid, and the pipelines that draw it
     * into the scene and into shadow maps. The destructor runs Destroy, so nothing leaks when Create fails part way.
     */
    class TerrainRenderer final
    {
    public:
        TerrainRenderer() = default;
        ~TerrainRenderer();

        TerrainRenderer(const TerrainRenderer&)            = delete;
        TerrainRenderer& operator=(const TerrainRenderer&) = delete;

        /**
         * Makes the shape's buffer and set, uploads the grid, and builds the pipelines for frames with these color and
         * depth formats, reading the lights (and drawing into their shadow maps); false (and logged) when one fails.
         * The land is flat at height 0 until SetShape. Device and lighting must outlive the renderer.
         */
        [[nodiscard]] bool Create(const Device& device, VkFormat colorFormat, VkFormat depthFormat,
                                  const Lighting& lighting);

        /** Frees everything Create made; safe to call more than once or after a failed Create. */
        void Destroy();

        /**
         * Draws the land of this shape from now on; false (and logged) for a shape that is not usable, which leaves the
         * last one. Not while the GPU draws a frame (between frames, as Lighting::Update).
         */
        [[nodiscard]] bool SetShape(const TerrainShape& shape);

        /** Moves the clipmap's levels around the viewer, for the draws that follow. */
        void FollowViewer(const glm::vec3& viewer);

        /** Records the land, seen through viewProjection and lit by lighting, into the frame's scene. */
        void Draw(VkCommandBuffer commandBuffer, const glm::mat4& viewProjection, const Lighting& lighting) const;

        /**
         * Records the land into the mirror's picture (render/mirror_view.hpp), seen through the mirrored view
         * (MakeMirrorViewProjection), lit by lighting: as Draw, with the mirrored view's clockwise front faces.
         */
        void DrawMirrored(VkCommandBuffer commandBuffer, const glm::mat4& mirroredViewProjection,
                          const Lighting& lighting) const;

        /**
         * Records the land into a shadow map being drawn (between ShadowMaps::BeginLayer and EndLayer), as the light
         * sees it: the levels that reach within reach of the viewer, so far land does not cost shadow time.
         */
        void DrawShadow(VkCommandBuffer commandBuffer, const glm::mat4& lightViewProjection, float reach) const;

        /** The shape's set layout, for other pipelines that read the shape at TerrainShapeSet. */
        [[nodiscard]] VkDescriptorSetLayout GetShapeSetLayout() const noexcept { return m_shapeSet.GetLayout(); }

        /** Binds the shape's set at TerrainShapeSet of another pipeline's layout. */
        void BindShape(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout) const;

    private:
        /** Records every level with one of the lit pipelines (the scene's or the mirror's), its lights and shape. */
        void DrawLit(VkCommandBuffer commandBuffer, const Pipeline& pipeline, const glm::mat4& viewProjection,
                     const Lighting& lighting) const;

        Buffer           m_shapeBuffer;
        DescriptorSet    m_shapeSet;
        TerrainMaterials m_materials;
        ClipmapLevels    m_levels;
        Pipeline         m_pipeline;
        Pipeline         m_mirrorPipeline;
        Pipeline         m_shadowPipeline;
    };
} // namespace Wotah
