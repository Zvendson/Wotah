/**
 * @file clipmap_levels.cpp
 * The clipmap's grid on the GPU, its levels around the viewer, and their draws.
 */
#include "render/clipmap_levels.hpp"

#include "render/mesh_data.hpp"

#include <glm/vec2.hpp>
#include <vulkan/vulkan.h>

#include <cstddef>

namespace
{
    // Every GPU offers at least 128 bytes of push constants (Vulkan spec, "Required Limits", maxPushConstantsSize).
    static_assert(sizeof(Wotah::ClipmapPushConstants) <= 128,
                  "the push constants must fit the 128 bytes every GPU offers");

    /** Where the parts of ClipmapPushConstants lie, in bytes; the matrix first. */
    constexpr uint32_t ViewProjectionOffset = offsetof(Wotah::ClipmapPushConstants, ViewProjection);
    constexpr uint32_t LevelOffset          = offsetof(Wotah::ClipmapPushConstants, Level);
} // namespace

bool Wotah::ClipmapLevels::Create(const Device& device)
{
    Destroy(); // a second Create replaces the first instead of leaking it
    const ClipmapGrid grid = MakeClipmapGrid();
    m_parts                = grid.Parts;
    m_placement            = PlaceClipmap(glm::vec2(0.0f)); // around the origin until FollowViewer
    return m_grid.Create(device, grid.Mesh);
}

void Wotah::ClipmapLevels::Destroy()
{
    m_grid.Destroy();
    m_placement = {};
}

void Wotah::ClipmapLevels::FollowViewer(const glm::vec3& viewer)
{
    m_placement = PlaceClipmap({viewer.x, viewer.z});
}

void Wotah::ClipmapLevels::Draw(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stages,
                                const glm::mat4& viewProjection, uint32_t end) const
{
    // The matrix once; per level only its 16 bytes of place.
    vkCmdPushConstants(commandBuffer, layout, stages, ViewProjectionOffset, sizeof(viewProjection), &viewProjection);
    for (uint32_t index = 0; index < end && index < ClipmapLevelCount; ++index)
    {
        const ClipmapLevel& level = m_placement[index];
        const glm::vec4     place(level.Center, level.CellSize, static_cast<float>(ClipmapHalfCells));
        vkCmdPushConstants(commandBuffer, layout, stages, LevelOffset, sizeof(place), &place);
        m_grid.Draw(commandBuffer, m_parts[level.Part]);
    }
}
