/**
 * @file scene_renderer.cpp
 * The scene's parts on the GPU, and a frame of them in the renderer's order.
 */
#include "render/scene_renderer.hpp"

#include "render/camera.hpp"
#include "render/mirror_view.hpp"
#include "render/renderer.hpp"
#include "render/shadow_maps.hpp"
#include "terrain/terrain_shape.hpp"
#include "water/sea.hpp"

#include <glm/mat4x4.hpp>

#include <cstdint>
#include <optional>

bool Wotah::SceneRenderer::Create(const Device& device, VkFormat colorFormat, const Renderer& renderer,
                                  const TerrainShape& landShape)
{
    Destroy(); // a second Create replaces the first instead of leaking it

    // The lights and the sea first: land and water read them through their descriptor set layouts. The sea's floor is
    // the land's shape, so it is pointed there once the land has it.
    const VkFormat depthFormat = renderer.GetDepthFormat();
    if (!m_lighting.Create(device) || !m_seaState.Create(device) || !m_sky.Create(device, colorFormat, depthFormat) ||
        !m_shadowPipeline.Create(device, MakeMeshShadowSettings(m_lighting.GetShadowMaps().GetFormat())) ||
        !m_land.Create(device, colorFormat, depthFormat, m_lighting, m_seaState) || !m_land.SetShape(landShape))
    {
        return false;
    }
    m_seaState.SetFloor(m_land.GetShapeBuffer());
    return m_water.Create(device, colorFormat, depthFormat, m_lighting, renderer.GetSceneBehind(), renderer.GetMirror(),
                          m_seaState);
}

void Wotah::SceneRenderer::Destroy()
{
    // Reverse of Create: the parts that read the lights before the lights.
    m_water.Destroy();
    m_land.Destroy();
    m_shadowPipeline.Destroy();
    m_sky.Destroy();
    m_seaState.Destroy();
    m_lighting.Destroy();
}

void Wotah::SceneRenderer::Draw(Renderer& renderer, const Camera& camera, const SceneLights& lights, const Sea& sea,
                                std::span<const MeshObject> objects)
{
    // Land and sea follow the camera to the horizon. The GPU is done with the last frame (BeginFrame waited for it),
    // so the lights and the waves can be rewritten; bad lights are logged by Update and the last good ones stay, so
    // the frame still draws (and so does a sea whose buffer failed, logged, with the last waves).
    const glm::vec3 viewer = camera.GetPosition();
    m_land.FollowViewer(viewer);
    m_water.FollowViewer(viewer);
    (void)m_lighting.Update(lights, camera);
    (void)m_seaState.Update(sea);

    // Every light's shadow map first, as the scene's shaders read them all; then what the sea mirrors (nothing while
    // the camera is under the surface or looks away from it); then the scene itself.
    const VkCommandBuffer commandBuffer = renderer.GetCommandBuffer();
    RecordShadows(commandBuffer, objects, lights.Shadows.Radius);
    renderer.BeginMirror();
    if (const std::optional<glm::mat4> mirrored = MakeMirrorViewProjection(camera, sea.Level))
    {
        RecordMirror(commandBuffer, *mirrored, lights, objects);
    }
    renderer.BeginScene();

    // The sky first: it tests no depth, so everything after it is drawn in front. Sun and moon are its own disks
    // (they sink behind the ground whole), not their light on the scene. Then the land and the meshes; the depth test
    // sorts out which is in front, so their order does not matter.
    const glm::mat4& viewProjection = camera.GetViewProjection();
    m_sky.Draw(commandBuffer, viewProjection, lights.SkySun, lights.SkyMoon, lights.SkySeconds);
    m_land.Draw(commandBuffer, viewProjection, m_lighting, m_seaState);
    RecordMeshes(commandBuffer, viewProjection, m_lighting, objects, false);

    // The sea last of the scene: what lies under it shows through it, bent and as deep as the solid scene's depth
    // says, and it mirrors what the mirror's picture shows.
    renderer.BeginSeeThrough();
    m_water.Draw(commandBuffer, viewProjection, m_lighting, renderer.GetSceneBehind(), renderer.GetMirror(), m_seaState,
                 sea);
}

void Wotah::SceneRenderer::RecordShadows(VkCommandBuffer commandBuffer, std::span<const MeshObject> objects,
                                         float landReach) const
{
    const ShadowMaps& maps = m_lighting.GetShadowMaps();
    for (uint32_t layer = 0; layer < MaxDirectionalLights * ShadowCascadeCount; ++layer)
    {
        const std::optional<glm::mat4> lightView =
            m_lighting.FindShadowViewProjection(layer / ShadowCascadeCount, layer % ShadowCascadeCount);
        if (!lightView || !maps.BeginLayer(commandBuffer, layer))
        {
            continue; // casts no shadow (or no such map, logged)
        }
        RecordMeshShadows(commandBuffer, m_shadowPipeline, *lightView, objects);
        m_land.DrawShadow(commandBuffer, *lightView, landReach);
        maps.EndLayer(commandBuffer, layer);
    }
}

void Wotah::SceneRenderer::RecordMirror(VkCommandBuffer commandBuffer, const glm::mat4& mirrored,
                                        const SceneLights& lights, std::span<const MeshObject> objects) const
{
    DirectionalLight sunless = lights.SkySun;
    sunless.Intensity        = 0.0f;
    m_sky.Draw(commandBuffer, mirrored, sunless, lights.SkyMoon, lights.SkySeconds);
    m_land.DrawMirrored(commandBuffer, mirrored, m_lighting, m_seaState);
    RecordMeshes(commandBuffer, mirrored, m_lighting, objects, true);
}
