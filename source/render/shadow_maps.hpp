/**
 * @file shadow_maps.hpp
 * Shadows of directional lights (the sun, the moon): for every light, depth pictures of the scene as that light sees
 * it, its shadow maps. A surface lies in a light's shadow where the light's map holds something nearer to the light.
 *
 * Each light has ShadowCascadeCount maps (cascades), each covering a stretch of the camera's view: the first only the
 * nearest few units, so close shadows get small map pixels and sharp edges; the later ones farther and farther
 * stretches with larger pixels, where the camera sees less detail anyway. The maps follow the camera in whole map
 * pixels, so the shadows stay still while it moves.
 *
 *     // every frame, before the scene is drawn, for every light and cascade with a view (Lighting):
 *     const uint32_t layer = Wotah::GetShadowLayer(lightIndex, cascade);
 *     if (shadowMaps.BeginLayer(commands, layer))
 *     {
 *         // draw everything that casts shadows with a depth-only pipeline (DepthFormat = GetFormat) through the
 *         // cascade's view
 *         shadowMaps.EndLayer(commands, layer);
 *     }
 *     // the scene's shaders then read every map through GetArrayView and GetSampler (shaders/lighting.glsl)
 */
#pragma once

#include "render/image.hpp"

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
     * How many shadow maps each light has, nearest stretch of the view first. Set once in CMakeLists.txt
     * (WOTAH_SHADOW_CASCADES), which hands the same number to the shaders.
     */
    inline constexpr uint32_t ShadowCascadeCount = WOTAH_SHADOW_CASCADES;

    /**
     * Each shadow map's width and height in pixels. The nearest cascade then has a map pixel every few millimetres; at
     * 16-bit depth one map takes 8 MiB, so a light with all its cascades takes 32 MiB.
     */
    inline constexpr uint32_t ShadowMapSize = 2048;

    /** A sphere in the world: the stretch of view a shadow map shows, or where shadow casters can be. */
    struct ShadowArea
    {
        /** The sphere's middle in the world. */
        glm::vec3 Center{0.0f};
        /** Its radius in world units; 0 (the default) is no area: for SceneLights::Shadows, no shadows. */
        float Radius = 0.0f;
    };

    /**
     * How far casters' depths in the shadow maps are pushed away from the light, so a lit surface does not shadow
     * itself (PipelineSettings::DepthBiasConstant and DepthBiasSlope of every shadow pipeline): 2 of the map's smallest
     * depth steps, and twice the triangle's slope. Found by looking at the pictures; with shaders/lighting.glsl lifting
     * the looked-up point, this leaves the open ground clean without the shadows letting go of the shapes.
     */
    inline constexpr float ShadowDepthBiasConstant = 2.0f;
    inline constexpr float ShadowDepthBiasSlope    = 2.0f;

    /** Where each cascade ends, as distances along the camera's view direction; the last one is reach. */
    using CascadeEnds = std::array<float, ShadowCascadeCount>;

    /**
     * Where the cascades end between nearest and reach: close together near the camera, far apart away from it (a mix
     * of even and growing steps, "practical split scheme", Zhang et al. 2006, "Parallel-split shadow maps"). Empty
     * unless 0 < nearest < reach.
     */
    [[nodiscard]] std::optional<CascadeEnds> MakeCascadeEnds(float nearest, float reach);

    /**
     * The smallest sphere around the part of the camera's view between the distances from and to (along its view
     * direction). Its radius depends only on the camera's lens (field of view, aspect ratio), never on where the camera
     * looks, and is rounded up a little, so the map's pixel size does not change while the camera turns. Empty unless
     * 0 <= from < to.
     */
    [[nodiscard]] std::optional<ShadowArea> MakeCascadeArea(const Camera& camera, float from, float to);

    /**
     * World space to a directional light's shadow map: an orthographic view along the light, x and y from -1 to 1
     * across the shown sphere, depth from 0 where the light enters the casters' sphere (or the shown one, whichever
     * starts first) to 1 where it leaves. The view moves only in whole map pixels, so a map that follows the camera
     * does not shimmer. direction is the way the light travels (any length). Empty without a direction or with a sphere
     * that has no radius.
     */
    [[nodiscard]] std::optional<glm::mat4> MakeShadowViewProjection(const glm::vec3& direction, const ShadowArea& shown,
                                                                    const ShadowArea& casters);

    /** How many world units one shadow map pixel covers when the map shows area. */
    [[nodiscard]] float GetShadowTexelSize(const ShadowArea& area) noexcept;

    /** The layer of the shadow maps that holds a light's cascade. */
    [[nodiscard]] constexpr uint32_t GetShadowLayer(uint32_t lightIndex, uint32_t cascade) noexcept
    {
        return lightIndex * ShadowCascadeCount + cascade;
    }

    /**
     * The shadow maps: one depth image with ShadowCascadeCount layers per light, a view of each layer to draw into, a
     * view of all of them for the shaders, and a sampler that compares depths. Until a layer is drawn it is fully lit.
     * The destructor runs Destroy, so nothing leaks when Create fails part way.
     */
    class ShadowMaps final
    {
    public:
        ShadowMaps() = default;
        ~ShadowMaps();

        ShadowMaps(const ShadowMaps&)            = delete;
        ShadowMaps& operator=(const ShadowMaps&) = delete;

        /**
         * Creates maps for lightCount lights (at least 1), their views and the sampler, and fills every layer with
         * "nothing casts a shadow"; false (and logged) when Vulkan refuses. Replaces maps created before; the GPU must
         * not be using them. Waits for the GPU once. The device must outlive the maps.
         */
        [[nodiscard]] bool Create(const Device& device, uint32_t lightCount);

        /** Frees everything Create made; safe to call more than once or after a failed Create. */
        void Destroy();

        /** For how many lights there are maps. */
        [[nodiscard]] uint32_t GetLightCapacity() const noexcept;

        /**
         * Starts drawing into a layer (GetShadowLayer): clears it and sets viewport and scissor to all of it. Call it
         * outside other drawing, and EndLayer after drawing the shadow casters. False (and logged, nothing recorded)
         * for a layer the maps do not have.
         */
        [[nodiscard]] bool BeginLayer(VkCommandBuffer commandBuffer, uint32_t layer) const;

        /** Ends drawing the layer and makes it readable for the shaders that draw the scene after it. */
        void EndLayer(VkCommandBuffer commandBuffer, uint32_t layer) const;

        /** The maps' depth format; the shadow pipeline draws with it (PipelineSettings::DepthFormat). */
        [[nodiscard]] VkFormat GetFormat() const noexcept { return m_image.GetFormat(); }

        /** All maps as one array view, for the shaders' sampler2DArrayShadow. */
        [[nodiscard]] VkImageView GetArrayView() const noexcept { return m_image.GetView(); }

        /** The sampler that compares a depth with the map's (lit = 1, in shadow = 0), smoothed between pixels. */
        [[nodiscard]] VkSampler GetSampler() const noexcept { return m_sampler; }

    private:
        /** The device the maps belong to; not owned. */
        VkDevice m_device = VK_NULL_HANDLE;

        /** The maps: ShadowCascadeCount layers per light. */
        Image m_image;

        /** One view per layer, to draw into. */
        std::vector<VkImageView> m_layerViews;

        VkSampler m_sampler = VK_NULL_HANDLE;
    };
} // namespace Wotah
