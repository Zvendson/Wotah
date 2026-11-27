/**
 * @file mesh_pipelines.cpp
 * The mesh pipelines' settings, the scene's and mirror's pair, and a mesh's draw.
 */
#include "render/mesh_pipelines.hpp"

#include "render/lighting.hpp"
#include "render/mesh.hpp"
#include "render/mesh_data.hpp"
#include "render/shadow_maps.hpp"

#include <vulkan/vulkan.h>

namespace
{
    /** The vertex shader of every mesh (shaders/mesh.vert), compiled to SPIR-V by the build. */
    constexpr uint32_t MeshVertexShader[] = {
#include "mesh.vert.inc"
    };

    /** The vertex shader that draws meshes into a light's shadow map (shaders/shadow.vert). */
    constexpr uint32_t ShadowVertexShader[] = {
#include "shadow.vert.inc"
    };
} // namespace

Wotah::PipelineSettings Wotah::MakeMeshSettings(std::span<const uint32_t> fragmentShader, VkFormat colorFormat,
                                                VkFormat depthFormat, std::span<const VkDescriptorSetLayout> setLayouts)
{
    PipelineSettings settings;
    settings.VertexShader         = MeshVertexShader;
    settings.FragmentShader       = fragmentShader;
    settings.ColorFormat          = colorFormat;
    settings.DepthFormat          = depthFormat;
    settings.VertexBindings       = MeshVertexBindings;
    settings.VertexAttributes     = MeshVertexAttributes;
    settings.CullMode             = VK_CULL_MODE_BACK_BIT;
    settings.FrontFace            = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    settings.DepthTest            = true;
    settings.DepthWrite           = true;
    settings.PushConstants        = MeshPushConstantRanges;
    settings.DescriptorSetLayouts = setLayouts;
    return settings;
}

Wotah::PipelineSettings Wotah::MakeMeshShadowSettings(VkFormat shadowFormat)
{
    PipelineSettings settings;
    settings.VertexShader      = ShadowVertexShader;
    settings.DepthFormat       = shadowFormat;
    settings.VertexBindings    = MeshVertexBindings;
    settings.VertexAttributes  = MeshVertexAttributes;
    settings.CullMode          = VK_CULL_MODE_NONE;
    settings.DepthTest         = true;
    settings.DepthWrite        = true;
    settings.DepthBiasConstant = ShadowDepthBiasConstant;
    settings.DepthBiasSlope    = ShadowDepthBiasSlope;
    settings.PushConstants     = MeshPushConstantRanges;
    return settings;
}

void Wotah::RecordMeshDraw(VkCommandBuffer commandBuffer, const Pipeline& pipeline, const Lighting& lighting,
                           const Mesh& mesh, const MeshPushConstants& pushConstants)
{
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.GetHandle());
    lighting.Bind(commandBuffer, pipeline.GetLayout());
    vkCmdPushConstants(commandBuffer, pipeline.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushConstants),
                       &pushConstants);
    mesh.Draw(commandBuffer);
}

void Wotah::RecordMeshes(VkCommandBuffer commandBuffer, const glm::mat4& viewProjection, const Lighting& lighting,
                         std::span<const MeshObject> objects, bool isMirrored)
{
    MeshPushConstants pushConstants;
    pushConstants.ViewProjection = viewProjection;
    for (const MeshObject& object : objects)
    {
        pushConstants.Model            = object.Model;
        const MeshPipelines& pipelines = *object.Pipelines;
        RecordMeshDraw(commandBuffer, isMirrored ? pipelines.GetMirror() : pipelines.GetScene(), lighting,
                       *object.Shape, pushConstants);
    }
}

void Wotah::RecordMeshShadows(VkCommandBuffer commandBuffer, const Pipeline& shadowPipeline, const glm::mat4& lightView,
                              std::span<const MeshObject> objects)
{
    // One pipeline for every caster: bound once, only the place changes per object.
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline.GetHandle());
    MeshPushConstants pushConstants;
    pushConstants.ViewProjection = lightView;
    for (const MeshObject& object : objects)
    {
        pushConstants.Model = object.Model;
        vkCmdPushConstants(commandBuffer, shadowPipeline.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(pushConstants), &pushConstants);
        object.Shape->Draw(commandBuffer);
    }
}

bool Wotah::MeshPipelines::Create(const Device& device, std::span<const uint32_t> fragmentShader, VkFormat colorFormat,
                                  VkFormat depthFormat, const Lighting& lighting)
{
    Destroy(); // a second Create replaces the first instead of leaking it
    const std::array<VkDescriptorSetLayout, 1> setLayouts{lighting.GetSetLayout()};
    const PipelineSettings scene  = MakeMeshSettings(fragmentShader, colorFormat, depthFormat, setLayouts);
    PipelineSettings       mirror = scene;
    mirror.FrontFace              = VK_FRONT_FACE_CLOCKWISE; // the mirror turns the picture's handedness
    return m_scene.Create(device, scene) && m_mirror.Create(device, mirror);
}

void Wotah::MeshPipelines::Destroy()
{
    m_mirror.Destroy();
    m_scene.Destroy();
}
