/**
 * @file terrain_renderer.cpp
 * The land's uniform buffer, clipmap grid and pipelines, and the draws that use them.
 */
#include "render/terrain_renderer.hpp"

#include "core/logger.hpp"
#include "render/device.hpp"
#include "render/lighting.hpp"
#include "render/mesh_data.hpp"
#include "render/shadow_maps.hpp"
#include "terrain/terrain_shape.hpp"

#include <glm/vec4.hpp>
#include <vulkan/vulkan.h>

#include <span>

namespace
{
    /** The land's shaders (shaders/terrain.vert, terrain.frag, terrain_shadow.vert), compiled by the build. */
    constexpr uint32_t TerrainVertexShader[] = {
#include "terrain.vert.inc"
    };
    constexpr uint32_t TerrainFragmentShader[] = {
#include "terrain.frag.inc"
    };
    constexpr uint32_t TerrainShadowVertexShader[] = {
#include "terrain_shadow.vert.inc"
    };

    /** What shaders/terrain_shape.glsl's TerrainShapeBlock holds: a TerrainShape's numbers in vec4s (std140). */
    struct GpuTerrainShape
    {
        /** x: Seed. */
        glm::uvec4 Seed{0u};
        /** x: SeaLevel, y: BeachSlope, z: BeachTop, w: SeaDepth. */
        glm::vec4 Sea{0.0f};
        /** x: CoastWander, y: CoastWavelength, z: BendStrength, w: BendWavelength. */
        glm::vec4 Coast{0.0f};
        /** x: DuneStart, y: DuneRamp, z: DuneHeight; w unused. */
        glm::vec4 Dunes{0.0f};
        /** x: DuneLength, y: DuneWidth; zw unused. */
        glm::vec4 DuneSize{0.0f};
        /** x: HillStart, y: HillRamp, z: HillHeight, w: HillWavelength. */
        glm::vec4 Hills{0.0f};
        /** x: BumpHeight, y: BumpWavelength, z: FinestWavelength; w unused. */
        glm::vec4 Bumps{0.0f};
    };

    // std140 places vec4s on 16-byte steps with no gaps (GLSL spec, 7.6.2.2), which this struct of vec4s reproduces.
    static_assert(sizeof(GpuTerrainShape) == 7 * sizeof(glm::vec4));

    /** A shape's numbers as the shaders read them. */
    GpuTerrainShape Pack(const Wotah::TerrainShape& shape)
    {
        GpuTerrainShape packed;
        packed.Seed     = {shape.Seed, 0u, 0u, 0u};
        packed.Sea      = {shape.SeaLevel, shape.BeachSlope, shape.BeachTop, shape.SeaDepth};
        packed.Coast    = {shape.CoastWander, shape.CoastWavelength, shape.BendStrength, shape.BendWavelength};
        packed.Dunes    = {shape.DuneStart, shape.DuneRamp, shape.DuneHeight, 0.0f};
        packed.DuneSize = {shape.DuneLength, shape.DuneWidth, 0.0f, 0.0f};
        packed.Hills    = {shape.HillStart, shape.HillRamp, shape.HillHeight, shape.HillWavelength};
        packed.Bumps    = {shape.BumpHeight, shape.BumpWavelength, shape.FinestWavelength, 0.0f};
        return packed;
    }

    /** What terrain.vert and terrain_shadow.vert get per level; matches their PushConstants block. */
    struct TerrainPushConstants
    {
        /** World space to clip space: the camera, or a light's shadow map. */
        glm::mat4 ViewProjection{1.0f};
        /** xy: the level's middle; z: its cells' size; w: half its width in cells (clipmap.glsl). */
        glm::vec4 Level{0.0f};
    };

    // Every GPU offers at least 128 bytes of push constants (Vulkan spec, "Required Limits", maxPushConstantsSize).
    static_assert(sizeof(TerrainPushConstants) <= 128, "the push constants must fit the 128 bytes every GPU offers");

    constexpr std::array<VkPushConstantRange, 1> TerrainPushConstantRanges{
        {{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(TerrainPushConstants)}}};

    /**
     * The shape's set: its uniform buffer, read by the vertex shaders (heights) and the fragment shader (the sea
     * level).
     */
    constexpr std::array<Wotah::DescriptorBinding, 1> ShapeBindings{
        {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT}}};
    constexpr uint32_t ShapeBinding = 0;

    /**
     * How far the land's depths in the shadow maps are pushed away from the light, so lit ground does not shadow
     * itself: as for the shapes (app.cpp), 2 of the map's smallest depth steps and twice the triangle's slope.
     */
    constexpr float ShadowDepthBiasConstant = 2.0f;
    constexpr float ShadowDepthBiasSlope    = 2.0f;

    /** How far in from its middle a level's own triangles start: the finer level fills half its width (clipmap.hpp). */
    constexpr float HoleShare = 0.5f;
} // namespace

Wotah::TerrainRenderer::~TerrainRenderer()
{
    Destroy();
}

bool Wotah::TerrainRenderer::Create(const Device& device, VkFormat colorFormat, VkFormat depthFormat,
                                    const Lighting& lighting)
{
    Destroy(); // a second Create replaces the first instead of leaking it

    // The shape's buffer and set, flat land to start with, and the grid every level is drawn from.
    const ClipmapGrid grid = MakeClipmapGrid();
    m_parts                = grid.Parts;
    if (!m_shapeBuffer.Create(device, sizeof(GpuTerrainShape), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                              BufferMemory::CpuToGpu) ||
        !m_shapeSet.Create(device, ShapeBindings) || !m_grid.Create(device, grid.Mesh))
    {
        return false;
    }
    m_shapeSet.WriteBuffer(ShapeBinding, m_shapeBuffer.GetHandle(), sizeof(GpuTerrainShape));
    if (!SetShape(MakeFlatTerrainShape(0.0f)))
    {
        return false;
    }
    m_placement = PlaceClipmap(glm::vec2(0.0f)); // around the origin until FollowViewer

    // Both pipelines see the lights at set 0 and the shape at set 1 (only the scene's fragment shader reads the
    // lights). The land is seen from above, so back faces are skipped; into shadow maps both sides go, as for shapes.
    const std::array<VkDescriptorSetLayout, 2> setLayouts{lighting.GetSetLayout(), m_shapeSet.GetLayout()};
    PipelineSettings                           scene;
    scene.VertexShader         = TerrainVertexShader;
    scene.FragmentShader       = TerrainFragmentShader;
    scene.ColorFormat          = colorFormat;
    scene.DepthFormat          = depthFormat;
    scene.VertexBindings       = MeshVertexBindings;
    scene.VertexAttributes     = MeshVertexAttributes;
    scene.CullMode             = VK_CULL_MODE_BACK_BIT;
    scene.FrontFace            = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    scene.DepthTest            = true;
    scene.DepthWrite           = true;
    scene.PushConstants        = TerrainPushConstantRanges;
    scene.DescriptorSetLayouts = setLayouts;

    PipelineSettings shadow;
    shadow.VertexShader         = TerrainShadowVertexShader;
    shadow.DepthFormat          = lighting.GetShadowMaps().GetFormat();
    shadow.VertexBindings       = MeshVertexBindings;
    shadow.VertexAttributes     = MeshVertexAttributes;
    shadow.CullMode             = VK_CULL_MODE_NONE;
    shadow.DepthTest            = true;
    shadow.DepthWrite           = true;
    shadow.DepthBiasConstant    = ShadowDepthBiasConstant;
    shadow.DepthBiasSlope       = ShadowDepthBiasSlope;
    shadow.PushConstants        = TerrainPushConstantRanges;
    shadow.DescriptorSetLayouts = setLayouts;
    return m_pipeline.Create(device, scene) && m_shadowPipeline.Create(device, shadow);
}

void Wotah::TerrainRenderer::Destroy()
{
    m_shadowPipeline.Destroy();
    m_pipeline.Destroy();
    m_grid.Destroy();
    m_shapeSet.Destroy();
    m_shapeBuffer.Destroy();
    m_placement = {};
}

bool Wotah::TerrainRenderer::SetShape(const TerrainShape& shape)
{
    if (!IsUsable(shape))
    {
        Logger::Error("The land's shape needs finite numbers, wavelengths, ramps and a beach slope above 0");
        return false;
    }
    const GpuTerrainShape packed = Pack(shape);
    return m_shapeBuffer.Write(std::as_bytes(std::span(&packed, 1)));
}

void Wotah::TerrainRenderer::FollowViewer(const glm::vec3& viewer)
{
    m_placement = PlaceClipmap({viewer.x, viewer.z});
}

void Wotah::TerrainRenderer::Draw(VkCommandBuffer commandBuffer, const glm::mat4& viewProjection,
                                  const Lighting& lighting) const
{
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.GetHandle());
    lighting.Bind(commandBuffer, m_pipeline.GetLayout());
    m_shapeSet.Bind(commandBuffer, m_pipeline.GetLayout(), TerrainShapeSet);
    DrawLevels(commandBuffer, m_pipeline, viewProjection, ClipmapLevelCount);
}

void Wotah::TerrainRenderer::DrawShadow(VkCommandBuffer commandBuffer, const glm::mat4& lightViewProjection,
                                        float reach) const
{
    // The levels whose ring starts within reach of the viewer: level 0 always, then every level whose hole (the finer
    // level, half its width) is narrower than reach.
    uint32_t end = 1;
    while (end < ClipmapLevelCount &&
           HoleShare * static_cast<float>(ClipmapHalfCells) * m_placement[end].CellSize < reach)
    {
        ++end;
    }
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline.GetHandle());
    m_shapeSet.Bind(commandBuffer, m_shadowPipeline.GetLayout(), TerrainShapeSet);
    DrawLevels(commandBuffer, m_shadowPipeline, lightViewProjection, end);
}

void Wotah::TerrainRenderer::BindShape(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout) const
{
    m_shapeSet.Bind(commandBuffer, pipelineLayout, TerrainShapeSet);
}

void Wotah::TerrainRenderer::DrawLevels(VkCommandBuffer commandBuffer, const Pipeline& pipeline,
                                        const glm::mat4& viewProjection, uint32_t end) const
{
    TerrainPushConstants pushConstants;
    pushConstants.ViewProjection = viewProjection;
    for (uint32_t index = 0; index < end; ++index)
    {
        const ClipmapLevel& level = m_placement[index];
        pushConstants.Level       = glm::vec4(level.Center, level.CellSize, static_cast<float>(ClipmapHalfCells));
        vkCmdPushConstants(commandBuffer, pipeline.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushConstants),
                           &pushConstants);
        m_grid.Draw(commandBuffer, m_parts[level.Part]);
    }
}
