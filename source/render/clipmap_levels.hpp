/**
 * @file clipmap_levels.hpp
 * The clipmap (render/clipmap.hpp) on the GPU, as land and sea draw it: the grid once, where its levels lie around the
 * viewer this frame, and recording the levels' draws with a bound pipeline. Every clipmap pipeline's push constants
 * start with ClipmapPushConstants (shaders/clipmap.glsl reads them); more may follow, which the caller pushes.
 *
 *     Wotah::ClipmapLevels levels;
 *     levels.Create(device);
 *     levels.FollowViewer(camera.GetPosition());   // each frame
 *     vkCmdBindPipeline(...);                       // and its descriptor sets
 *     levels.Draw(commands, pipeline.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, viewProjection);
 */
#pragma once

#include "render/clipmap.hpp"
#include "render/mesh.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <vulkan/vulkan_core.h>

#include <array>
#include <cstdint>

namespace Wotah
{
    class Device;

    /** How every clipmap pipeline's push constants start; matches the PushConstants blocks of its shaders. */
    struct ClipmapPushConstants
    {
        /** World space to clip space: the camera, the mirrored camera, or a light's shadow map. */
        glm::mat4 ViewProjection{1.0f};
        /** xy: the level's middle; z: its cells' size; w: half its width in cells (clipmap.glsl). */
        glm::vec4 Level{0.0f};
    };

    /** The clipmap's grid on the GPU and where its levels lie this frame. The destructor frees the grid. */
    class ClipmapLevels final
    {
    public:
        /**
         * Puts the grid on the GPU (Create waits until it is there), its levels around the origin until FollowViewer;
         * false (and logged) on failure. The device must outlive it.
         */
        [[nodiscard]] bool Create(const Device& device);

        /** Frees the grid; safe to call more than once or after a failed Create. */
        void Destroy();

        /** Moves the levels around the viewer (only x and z count), for the draws that follow. */
        void FollowViewer(const glm::vec3& viewer);

        /** Where the levels lie this frame, finest first. */
        [[nodiscard]] const ClipmapPlacement& GetPlacement() const noexcept { return m_placement; }

        /**
         * Records the levels from the finest up to (not including) end with the bound pipeline: viewProjection pushed
         * once, each level's place after it (ClipmapPushConstants), then its part of the grid. stages: those of the
         * layout's push constant range, as Vulkan wants every stage of a range named when part of it is pushed.
         */
        void Draw(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stages,
                  const glm::mat4& viewProjection, uint32_t end = ClipmapLevelCount) const;

    private:
        Mesh                                    m_grid;
        std::array<MeshRange, ClipmapPartCount> m_parts{};
        ClipmapPlacement                        m_placement{};
    };
} // namespace Wotah
