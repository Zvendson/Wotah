/**
 * @file water_renderer.cpp
 * The sea's clipmap grid, its waves' buffer and its pipelines, and the draw that uses them.
 */
#include "render/water_renderer.hpp"

#include "render/device.hpp"
#include "render/lighting.hpp"
#include "render/mesh_data.hpp"
#include "render/mirror_view.hpp"
#include "render/scene_behind.hpp"
#include "render/sea_state.hpp"
#include "water/sea.hpp"

#include <glm/vec4.hpp>
#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace
{
    /** The water's shaders (shaders/water.vert, water.frag), compiled to SPIR-V by the build. */
    constexpr uint32_t WaterVertexShader[] = {
#include "water.vert.inc"
    };
    constexpr uint32_t WaterFragmentShader[] = {
#include "water.frag.inc"
    };

    /** The view from under the surface (shaders/underwater.frag), over the whole picture. */
    constexpr uint32_t UnderwaterFragmentShader[] = {
#include "underwater.frag.inc"
    };

    /** What underwater.frag gets; matches its PushConstants block. */
    struct UnderwaterPushConstants
    {
        /** x: the surface's height over the camera; yzw unused. */
        glm::vec4 Surface{0.0f};
    };

    constexpr std::array<VkPushConstantRange, 1> UnderwaterPushConstantRanges{
        {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(UnderwaterPushConstants)}}};

    /**
     * What water.vert and water.frag get: the clipmap's push constants (ClipmapLevels::Draw pushes them per level),
     * then the surface; matches their PushConstants blocks.
     */
    struct WaterPushConstants
    {
        Wotah::ClipmapPushConstants Clipmap;
        /** x: the still surface's height; y: the surface's height over the camera; zw unused. */
        glm::vec4 Surface{0.0f};
    };

    // Every GPU offers at least 128 bytes of push constants (Vulkan spec, "Required Limits", maxPushConstantsSize).
    static_assert(sizeof(WaterPushConstants) <= 128, "the push constants must fit the 128 bytes every GPU offers");

    /** Both stages read them: the fragment shader sees through the camera to find where the bent view lands. */
    constexpr VkShaderStageFlags WaterPushStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    /** Where the surface lies in the push constants, in bytes. */
    constexpr uint32_t SurfaceOffset = offsetof(WaterPushConstants, Surface);

    constexpr std::array<VkPushConstantRange, 1> WaterPushConstantRanges{
        {{WaterPushStages, 0, sizeof(WaterPushConstants)}}};
} // namespace

Wotah::WaterRenderer::~WaterRenderer()
{
    Destroy();
}

bool Wotah::WaterRenderer::Create(const Device& device, VkFormat colorFormat, VkFormat depthFormat,
                                  const Lighting& lighting, const SceneBehind& behind, const MirrorView& mirror,
                                  const SeaState& sea)
{
    Destroy(); // a second Create replaces the first instead of leaking it
    if (!m_levels.Create(device))
    {
        return false;
    }

    // Over what is already drawn, which the shader reads from the copy and covers (what lies under the water shows
    // through the shader, bent), depth tested against the solid scene and written, so the crest of a wave hides the
    // water behind it (the shader reads the depth's copy). From above only the surface's top side is drawn (the grid's
    // front faces, as the land's), from below only its underside: far away, where the depth can no longer tell a crest
    // from the back of the wave behind it, the back would show through as dark specks.
    const std::array<VkDescriptorSetLayout, 4> setLayouts{lighting.GetSetLayout(), behind.GetSetLayout(),
                                                          mirror.GetSetLayout(), sea.GetSetLayout()};
    PipelineSettings                           settings;
    settings.VertexShader         = WaterVertexShader;
    settings.FragmentShader       = WaterFragmentShader;
    settings.ColorFormat          = colorFormat;
    settings.DepthFormat          = depthFormat;
    settings.VertexBindings       = MeshVertexBindings;
    settings.VertexAttributes     = MeshVertexAttributes;
    settings.CullMode             = VK_CULL_MODE_BACK_BIT;
    settings.FrontFace            = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    settings.DepthTest            = true;
    settings.DepthWrite           = true;
    settings.Blend                = BlendMode::Opaque;
    settings.PushConstants        = WaterPushConstantRanges;
    settings.DescriptorSetLayouts = setLayouts;

    // Under the surface: every pixel of the solid scene tinted, from its copy, before the surface is drawn over it; no
    // depth test (it covers the whole picture), no culling (one triangle).
    const std::array<VkDescriptorSetLayout, 2> underwaterSetLayouts{lighting.GetSetLayout(), behind.GetSetLayout()};
    PipelineSettings underwater     = MakeFullscreenSettings(UnderwaterFragmentShader, colorFormat, depthFormat);
    underwater.PushConstants        = UnderwaterPushConstantRanges;
    underwater.DescriptorSetLayouts = underwaterSetLayouts;
    PipelineSettings fromBelow      = settings;
    fromBelow.CullMode              = VK_CULL_MODE_FRONT_BIT;
    return m_pipeline.Create(device, settings) && m_belowPipeline.Create(device, fromBelow) &&
           m_underwaterPipeline.Create(device, underwater);
}

void Wotah::WaterRenderer::Destroy()
{
    m_underwaterPipeline.Destroy();
    m_belowPipeline.Destroy();
    m_pipeline.Destroy();
    m_levels.Destroy();
}

void Wotah::WaterRenderer::FollowViewer(const glm::vec3& viewer)
{
    m_levels.FollowViewer(viewer);
    m_viewer = viewer;
}

void Wotah::WaterRenderer::Draw(VkCommandBuffer commandBuffer, const glm::mat4& viewProjection,
                                const Lighting& lighting, const SceneBehind& behind, const MirrorView& mirror,
                                const SeaState& seaState, const Sea& sea) const
{
    // The surface over the viewer, waves and all: under it, everything the viewer sees lies in the water, tinted first,
    // the surface drawn over it.
    const float overViewer = FindWaterHeight(sea, m_viewer.x, m_viewer.z).value_or(sea.Level);
    const bool  isUnder    = m_viewer.y < overViewer;
    if (isUnder)
    {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_underwaterPipeline.GetHandle());
        lighting.Bind(commandBuffer, m_underwaterPipeline.GetLayout());
        behind.Bind(commandBuffer, m_underwaterPipeline.GetLayout(), WaterSceneBehindSet);
        const UnderwaterPushConstants underwater{glm::vec4(overViewer, 0.0f, 0.0f, 0.0f)};
        vkCmdPushConstants(commandBuffer, m_underwaterPipeline.GetLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(underwater), &underwater);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0); // fullscreen.vert's one triangle
    }

    const Pipeline& surfacePipeline = isUnder ? m_belowPipeline : m_pipeline;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, surfacePipeline.GetHandle());
    lighting.Bind(commandBuffer, surfacePipeline.GetLayout());
    behind.Bind(commandBuffer, surfacePipeline.GetLayout(), WaterSceneBehindSet);
    mirror.Bind(commandBuffer, surfacePipeline.GetLayout(), WaterMirrorSet);
    seaState.Bind(commandBuffer, surfacePipeline.GetLayout());

    // The surface's heights once, after the clipmap's part of the push constants; the levels push their part.
    const glm::vec4 surface(sea.Level, overViewer, 0.0f, 0.0f);
    vkCmdPushConstants(commandBuffer, surfacePipeline.GetLayout(), WaterPushStages, SurfaceOffset, sizeof(surface),
                       &surface);
    m_levels.Draw(commandBuffer, surfacePipeline.GetLayout(), WaterPushStages, viewProjection);
}
