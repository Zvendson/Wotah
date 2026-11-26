/**
 * @file lighting.hpp
 * The scene's light: an ambient color that reaches every surface, and directional lights such as the sun and the moon,
 * each with its own direction, color and intensity, and each casting its own shadows. Lighting hands them to the
 * shaders (shaders/lighting.glsl) through a uniform buffer, updated every frame, so lights can move and change color (a
 * day and night cycle), together with every light's shadow maps (render/shadow_maps.hpp), which follow the camera.
 *
 *     Wotah::SceneLights lights;
 *     lights.AmbientColor = {0.1f, 0.1f, 0.15f};
 *     Wotah::DirectionalLight sun;
 *     sun.Direction = {-0.4f, -1.0f, -0.25f}; // shining down
 *     sun.Color     = {1.0f, 0.95f, 0.85f};
 *     lights.DirectionalLights.push_back(sun);
 *     lights.Shadows = {{0.0f, 0.0f, 0.0f}, 50.0f}; // where shadow casters can stand
 *
 *     lighting.Update(lights, camera);  // every frame, after Renderer::BeginFrame
 *     // draw each light's cascades: lighting.GetShadowMaps() with FindShadowViewProjection(index, cascade)
 *     lighting.Bind(commands, layout);  // with each pipeline that reads the lights
 *
 * The world's compass: y is up, north is -z, east is +x, south is +z, west is -x.
 */
#pragma once

#include "render/buffer.hpp"
#include "render/shadow_maps.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <vulkan/vulkan_core.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace Wotah
{
    class Camera;
    class Device;

    /**
     * The most directional lights a scene has at once. Set once in CMakeLists.txt (WOTAH_MAX_DIRECTIONAL_LIGHTS), which
     * hands the same number to the shaders, so their light array always has this size.
     */
    inline constexpr uint32_t MaxDirectionalLights = WOTAH_MAX_DIRECTIONAL_LIGHTS;

    /**
     * Light from so far away that it falls in one direction everywhere: the sun, the moon. It casts shadows when the
     * scene has a shadow area (SceneLights::Shadows) and the light is not dark.
     */
    struct DirectionalLight
    {
        /** The way the light travels, from the light into the scene; any length above 0. */
        glm::vec3 Direction{0.0f, -1.0f, 0.0f};
        /** Its color, 1 per channel being full brightness. */
        glm::vec3 Color{1.0f};
        /** How strong it is; multiplies the color, 0 switches it off. */
        float Intensity = 1.0f;
    };

    /**
     * The direction a light in the sky shines in, from where it stands: azimuth is the compass angle in radians from
     * north towards east (0 north, pi/2 east, pi south), elevation the angle above the horizon (0 on the horizon, pi/2
     * straight overhead). A sun in the east at 45 degrees shines down and towards the west.
     */
    [[nodiscard]] glm::vec3 MakeLightDirection(float azimuth, float elevation);

    /**
     * How much of the sun's light gets through the air, per color (red, green, blue), compared with a sun straight
     * overhead, for a sun elevation in radians above the horizon. The lower the sun, the more air its light crosses,
     * and the air takes blue more than red: a low sun is dim and reddish, as at sunset. 1 overhead (elevations past it
     * count as overhead), nearly 0 at the horizon, 0 below it or for an elevation that is not a number.
     */
    [[nodiscard]] glm::vec3 MakeSunlightThroughAir(float elevation);

    /** Everything that lights the scene. */
    struct SceneLights
    {
        /** The light that reaches every surface from all around, even those turned away from every light. */
        glm::vec3 AmbientColor{0.25f};
        /**
         * Light the ground throws back up, on top of AmbientColor, reaching a surface by how much it faces down: all of
         * it facing straight down, half on upright sides, none facing up. 0 by default, leaving AmbientColor alone.
         */
        glm::vec3 GroundBounce{0.0f};
        /** The directional lights, at most MaxDirectionalLights. */
        std::vector<DirectionalLight> DirectionalLights;
        /**
         * Where shadow casters can stand, so every one of them reaches the shadow maps; the default radius of 0 means
         * no shadows at all.
         */
        ShadowArea Shadows;
        /** How far from the camera shadows reach, in world units; they fade out over the last stretch. */
        float ShadowReach = 50.0f;
    };

    /**
     * The scene's lights on the GPU: a uniform buffer the CPU rewrites every frame, every light's shadow maps, and the
     * descriptor set that shows them to the shaders at set 0 (binding 0 the buffer, binding 1 the shadow maps). The
     * destructor runs Destroy, so nothing leaks when Create fails part way.
     */
    class Lighting final
    {
    public:
        Lighting() = default;
        ~Lighting();

        Lighting(const Lighting&)            = delete;
        Lighting& operator=(const Lighting&) = delete;

        /**
         * Creates the buffer, the shadow maps, the descriptor set layout, pool and set; false (and logged) when Vulkan
         * refuses. Starts with the default ambient light, no directional lights and no shadows. The device must
         * outlive it.
         */
        [[nodiscard]] bool Create(const Device& device);

        /** Frees everything Create made; safe to call more than once or after a failed Create. */
        void Destroy();

        /**
         * Writes the lights for the next frame, with each shadow-casting light's view of the cascades the camera needs;
         * call it after Renderer::BeginFrame, when the GPU no longer reads the last frame's. Makes room for more shadow
         * maps when more lights cast shadows than before, which waits for the GPU once. False (and logged, the old
         * lights kept) for more than MaxDirectionalLights lights or a light without a direction.
         */
        [[nodiscard]] bool Update(const SceneLights& lights, const Camera& camera);

        /**
         * World space to a cascade's shadow map of the directional light at index, as of the last Update; empty when
         * that light casts no shadow (no shadow area, a dark light, no such light). Every cascade with one must have
         * its map drawn (GetShadowMaps, layer GetShadowLayer) each frame before the scene, or the shaders read an old
         * map.
         */
        [[nodiscard]] std::optional<glm::mat4> FindShadowViewProjection(uint32_t index, uint32_t cascade) const;

        /** The shadow maps, ShadowCascadeCount layers per light, to draw into before the scene. */
        [[nodiscard]] const ShadowMaps& GetShadowMaps() const noexcept { return m_shadowMaps; }

        /** Binds the lights at set 0 for a pipeline whose layout includes GetSetLayout first. */
        void Bind(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout) const;

        /** The descriptor set layout the pipelines list (PipelineSettings::DescriptorSetLayouts) to read the lights. */
        [[nodiscard]] VkDescriptorSetLayout GetSetLayout() const noexcept { return m_setLayout; }

    private:
        /** Points binding 1 of the set at the current shadow maps. */
        void WriteShadowMapsDescriptor();

        /** The device it was made on; not owned. The object too, to make more shadow maps when needed. */
        VkDevice      m_device       = VK_NULL_HANDLE;
        const Device* m_deviceObject = nullptr;

        Buffer                m_buffer;
        ShadowMaps            m_shadowMaps;
        VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
        VkDescriptorPool      m_pool      = VK_NULL_HANDLE;
        VkDescriptorSet       m_set       = VK_NULL_HANDLE; // freed with the pool

        /**
         * Each light's view of each cascade from the last Update, at GetShadowLayer(light, cascade); empty where a
         * light casts no shadow.
         */
        std::array<std::optional<glm::mat4>, MaxDirectionalLights * ShadowCascadeCount> m_shadowViewProjections{};
    };
} // namespace Wotah
