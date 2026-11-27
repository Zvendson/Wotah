/**
 * @file mesh_pipelines.hpp
 * The pipelines that draw meshes (render/mesh.hpp) with shaders/mesh.vert: the settings they share, what each draw
 * pushes, a mesh's pair of pipelines (one for the scene, one for the sea's mirror, whose front faces wind clockwise),
 * the depth-only pipeline that draws meshes into the lights' shadow maps, and recording meshes' draws.
 *
 *     Wotah::MeshPipelines ballLook;
 *     ballLook.Create(device, BallFragmentShader, colorFormat, depthFormat, lighting);
 *     const std::array<Wotah::MeshObject, 1> objects{{{&ballLook, &ballMesh, ballModel}}};
 *     Wotah::RecordMeshes(commands, camera.GetViewProjection(), lighting, objects, false);
 */
#pragma once

#include "render/pipeline.hpp"

#include <glm/mat4x4.hpp>
#include <vulkan/vulkan_core.h>

#include <array>
#include <cstdint>
#include <span>

namespace Wotah
{
    class Device;
    class Lighting;
    class Mesh;

    /** What shaders/mesh.vert (and shadow.vert) get per draw; matches their PushConstants blocks. */
    struct MeshPushConstants
    {
        /** World space to clip space: the camera, or a light's shadow map. */
        glm::mat4 ViewProjection{1.0f};
        /** The mesh's own space to world space: where the object is (no non-uniform scaling). */
        glm::mat4 Model{1.0f};
    };

    // Every GPU offers at least 128 bytes of push constants (Vulkan spec, "Required Limits", maxPushConstantsSize).
    static_assert(sizeof(MeshPushConstants) <= 128, "the push constants must fit the 128 bytes every GPU offers");

    /** The one push constant range of every mesh pipeline: MeshPushConstants, read by the vertex shader. */
    inline constexpr std::array<VkPushConstantRange, 1> MeshPushConstantRanges{
        {{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants)}}};

    /**
     * The settings every mesh pipeline shares: mesh.vert with this fragment shader, mesh vertices, back faces culled,
     * depth tested and written (nearer surfaces hide farther ones whatever order they are drawn in), these descriptor
     * sets (the lights at set 0). The spans must stay valid until the pipeline is created.
     */
    [[nodiscard]] PipelineSettings MakeMeshSettings(std::span<const uint32_t> fragmentShader, VkFormat colorFormat,
                                                    VkFormat                               depthFormat,
                                                    std::span<const VkDescriptorSetLayout> setLayouts);

    /**
     * The depth-only pipeline that draws meshes into a light's shadow map (shaders/shadow.vert): both sides of every
     * triangle (so open or thin shapes cast too), depths pushed away from the light a little (so lit faces do not
     * shadow themselves).
     */
    [[nodiscard]] PipelineSettings MakeMeshShadowSettings(VkFormat shadowFormat);

    /** Records one mesh's draw: its pipeline, the lights, the camera and its place, then the mesh. */
    void RecordMeshDraw(VkCommandBuffer commandBuffer, const Pipeline& pipeline, const Lighting& lighting,
                        const Mesh& mesh, const MeshPushConstants& pushConstants);

    /**
     * A look for meshes (a fragment shader) as two pipelines: one for the scene, one for the sea's mirror
     * (render/mirror_view.hpp: front faces wind clockwise there). The destructor frees both.
     */
    class MeshPipelines final
    {
    public:
        /**
         * Builds both pipelines for frames of these formats, reading the lights (set 0); false (and logged) when one
         * fails. The device must outlive them.
         */
        [[nodiscard]] bool Create(const Device& device, std::span<const uint32_t> fragmentShader, VkFormat colorFormat,
                                  VkFormat depthFormat, const Lighting& lighting);

        /** Frees both pipelines; safe to call more than once or after a failed Create. */
        void Destroy();

        /** The pipeline for the scene. */
        [[nodiscard]] const Pipeline& GetScene() const noexcept { return m_scene; }

        /** The pipeline for the sea's mirror. */
        [[nodiscard]] const Pipeline& GetMirror() const noexcept { return m_mirror; }

    private:
        Pipeline m_scene;
        Pipeline m_mirror;
    };

    /** One mesh standing in the scene this frame: how it looks, its shape, and where it is. */
    struct MeshObject
    {
        /** Not owned: the pipelines that draw it. */
        const MeshPipelines* Pipelines = nullptr;
        /** Not owned: its shape on the GPU. */
        const Mesh* Shape = nullptr;
        /** Its own space to world space. */
        glm::mat4 Model{1.0f};
    };

    /**
     * Records every object's draw through viewProjection, in order: with its scene pipeline, or its mirror pipeline
     * when isMirrored (inside MirrorView's drawing).
     */
    void RecordMeshes(VkCommandBuffer commandBuffer, const glm::mat4& viewProjection, const Lighting& lighting,
                      std::span<const MeshObject> objects, bool isMirrored);

    /**
     * Records every object into the shadow map layer being drawn (between ShadowMaps::BeginLayer and EndLayer), as the
     * light sees it through lightView, with shadowPipeline (made from MakeMeshShadowSettings).
     */
    void RecordMeshShadows(VkCommandBuffer commandBuffer, const Pipeline& shadowPipeline, const glm::mat4& lightView,
                           std::span<const MeshObject> objects);
} // namespace Wotah
