/**
 * @file water_renderer.cpp
 * The sea's clipmap grid and pipeline, and the draw that uses them.
 */
#include "render/water_renderer.hpp"

#include "render/device.hpp"
#include "render/lighting.hpp"
#include "render/mesh_data.hpp"
#include "render/scene_depth.hpp"
#include "render/water.hpp"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <vulkan/vulkan.h>

namespace
{
    /** The water's shaders (shaders/water.vert, water.frag), compiled to SPIR-V by the build. */
    constexpr uint32_t WaterVertexShader[] = {
#include "water.vert.inc"
    };
    constexpr uint32_t WaterFragmentShader[] = {
#include "water.frag.inc"
    };

    /** What water.vert gets per level; matches its PushConstants block. */
    struct WaterPushConstants
    {
        /** World space to clip space: the camera. */
        glm::mat4 ViewProjection{1.0f};
        /** xy: the level's middle; z: its cells' size; w: half its width in cells (clipmap.glsl). */
        glm::vec4 Level{0.0f};
        /** x: the surface's height; yzw unused. */
        glm::vec4 Surface{0.0f};
    };

    // Every GPU offers at least 128 bytes of push constants (Vulkan spec, "Required Limits", maxPushConstantsSize).
    static_assert(sizeof(WaterPushConstants) <= 128, "the push constants must fit the 128 bytes every GPU offers");

    constexpr std::array<VkPushConstantRange, 1> WaterPushConstantRanges{
        {{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(WaterPushConstants)}}};
} // namespace

Wotah::WaterRenderer::~WaterRenderer()
{
    Destroy();
}

bool Wotah::WaterRenderer::Create(const Device& device, VkFormat colorFormat, VkFormat depthFormat,
                                  const Lighting& lighting, const SceneDepth& sceneDepth)
{
    Destroy(); // a second Create replaces the first instead of leaking it
    const ClipmapGrid grid = MakeClipmapGrid();
    m_parts                = grid.Parts;
    m_placement            = PlaceClipmap(glm::vec2(0.0f)); // around the origin until FollowViewer
    if (!m_grid.Create(device, grid.Mesh))
    {
        return false;
    }

    // Over what is already drawn (BlendMode::Filtered: the shader says, per color, how much of it shows through), seen
    // from both sides (from under the water too), depth tested against the solid scene but never written: the depth
    // image is only read now (Renderer::BeginSeeThrough).
    const std::array<VkDescriptorSetLayout, 2> setLayouts{lighting.GetSetLayout(), sceneDepth.GetSetLayout()};
    PipelineSettings                           settings;
    settings.VertexShader         = WaterVertexShader;
    settings.FragmentShader       = WaterFragmentShader;
    settings.ColorFormat          = colorFormat;
    settings.DepthFormat          = depthFormat;
    settings.VertexBindings       = MeshVertexBindings;
    settings.VertexAttributes     = MeshVertexAttributes;
    settings.CullMode             = VK_CULL_MODE_NONE;
    settings.DepthTest            = true;
    settings.DepthWrite           = false;
    settings.Blend                = BlendMode::Filtered;
    settings.PushConstants        = WaterPushConstantRanges;
    settings.DescriptorSetLayouts = setLayouts;
    return m_pipeline.Create(device, settings);
}

void Wotah::WaterRenderer::Destroy()
{
    m_pipeline.Destroy();
    m_grid.Destroy();
    m_placement = {};
}

void Wotah::WaterRenderer::FollowViewer(const glm::vec3& viewer)
{
    m_placement = PlaceClipmap({viewer.x, viewer.z});
}

void Wotah::WaterRenderer::Draw(VkCommandBuffer commandBuffer, const glm::mat4& viewProjection,
                                const Lighting& lighting, const SceneDepth& sceneDepth, const Sea& sea) const
{
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.GetHandle());
    lighting.Bind(commandBuffer, m_pipeline.GetLayout());
    sceneDepth.Bind(commandBuffer, m_pipeline.GetLayout(), WaterSceneDepthSet);

    WaterPushConstants pushConstants;
    pushConstants.ViewProjection = viewProjection;
    pushConstants.Surface        = glm::vec4(sea.Level, 0.0f, 0.0f, 0.0f);
    for (const ClipmapLevel& level : m_placement)
    {
        pushConstants.Level = glm::vec4(level.Center, level.CellSize, static_cast<float>(ClipmapHalfCells));
        vkCmdPushConstants(commandBuffer, m_pipeline.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushConstants),
                           &pushConstants);
        m_grid.Draw(commandBuffer, m_parts[level.Part]);
    }
}
