/**
 * @file lighting.hpp
 * The scene's light: an ambient color that reaches every surface, and directional lights such as the sun and the moon,
 * each with its own direction, color and intensity. Lighting hands them to the shaders (shaders/lighting.glsl) through
 * a uniform buffer, updated every frame, so lights can move and change color (a day and night cycle).
 *
 *     Wotah::SceneLights lights;
 *     lights.AmbientColor = {0.1f, 0.1f, 0.15f};
 *     Wotah::DirectionalLight sun;
 *     sun.Direction = {-0.4f, -1.0f, -0.25f}; // shining down
 *     sun.Color     = {1.0f, 0.95f, 0.85f};
 *     lights.DirectionalLights.push_back(sun);
 *
 *     lighting.Update(lights);          // every frame, after Renderer::BeginFrame
 *     lighting.Bind(commands, layout);  // with each pipeline that reads the lights
 *
 * The world's compass: y is up, north is -z, east is +x, south is +z, west is -x.
 */
#pragma once

#include "render/buffer.hpp"

#include <glm/vec3.hpp>
#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <vector>

namespace Wotah
{
    class Device;

    /**
     * The most directional lights a scene has at once. Set once in CMakeLists.txt (WOTAH_MAX_DIRECTIONAL_LIGHTS), which
     * hands the same number to the shaders, so their light array always has this size.
     */
    inline constexpr uint32_t MaxDirectionalLights = WOTAH_MAX_DIRECTIONAL_LIGHTS;

    /** Light from so far away that it falls in one direction everywhere: the sun, the moon. */
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

    /** Everything that lights the scene. */
    struct SceneLights
    {
        /** The light that reaches every surface from all around, even those turned away from every light. */
        glm::vec3 AmbientColor{0.25f};
        /** The directional lights, at most MaxDirectionalLights. */
        std::vector<DirectionalLight> DirectionalLights;
    };

    /**
     * The scene's lights on the GPU: a uniform buffer the CPU rewrites every frame, and the descriptor set that shows
     * it to the shaders at set 0, binding 0. The destructor runs Destroy, so nothing leaks when Create fails part way.
     */
    class Lighting final
    {
    public:
        Lighting() = default;
        ~Lighting();

        Lighting(const Lighting&)            = delete;
        Lighting& operator=(const Lighting&) = delete;

        /**
         * Creates the buffer, the descriptor set layout, pool and set; false (and logged) when Vulkan refuses. Starts
         * with the default ambient light and no directional lights. The device must outlive it.
         */
        [[nodiscard]] bool Create(const Device& device);

        /** Frees everything Create made; safe to call more than once or after a failed Create. */
        void Destroy();

        /**
         * Writes the lights for the next frame; call it after Renderer::BeginFrame, when the GPU no longer reads the
         * last frame's. False (and logged, the old lights kept) for more than MaxDirectionalLights lights or a light
         * without a direction.
         */
        [[nodiscard]] bool Update(const SceneLights& lights);

        /** Binds the lights at set 0 for a pipeline whose layout includes GetSetLayout first. */
        void Bind(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout) const;

        /** The descriptor set layout the pipelines list (PipelineSettings::DescriptorSetLayouts) to read the lights. */
        [[nodiscard]] VkDescriptorSetLayout GetSetLayout() const noexcept { return m_setLayout; }

    private:
        /** The device it was made on; not owned. */
        VkDevice m_device = VK_NULL_HANDLE;

        Buffer                m_buffer;
        VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
        VkDescriptorPool      m_pool      = VK_NULL_HANDLE;
        VkDescriptorSet       m_set       = VK_NULL_HANDLE; // freed with the pool
    };
} // namespace Wotah
