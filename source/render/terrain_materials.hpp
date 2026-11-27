/**
 * @file terrain_materials.hpp
 * The ground's textures: for each of its layers (shaders/ground.glsl: wet sand, dry sand, grass, rock) a look (color
 * and height) and a surface (slope and roughness), each a GroundTextureSize tile that repeats, with every mip level
 * down to 1 x 1. They are made on the GPU at start from noise (shaders/terrain_material.comp), each level from the
 * pattern itself, not from the level before. The land's shader reads them through one descriptor set: repeating,
 * blended between texels and mip levels, and sharp at a slant (anisotropic filtering).
 *
 *     Wotah::TerrainMaterials materials;
 *     materials.Create(device);                          // makes the textures; takes a moment
 *     materials.Bind(commands, landPipeline.GetLayout(), 2);
 *
 * shortcut: every layer is made from noise. A painted or photographed texture would go into a layer's place (an
 * upload into that layer and its mip levels); the land's shader reads it the same way.
 */
#pragma once

#include "render/descriptor_set.hpp"
#include "render/image.hpp"

#include <vulkan/vulkan_core.h>

#include <cstdint>

namespace Wotah
{
    class Device;

    /** The ground's layers; shaders/ground.glsl holds the same count and order. */
    inline constexpr uint32_t GroundLayerCount = 4;

    /** Texels to a side of each layer's tile at its finest level. */
    inline constexpr uint32_t GroundTextureSize = 1024;

    /** The bindings of the set: the looks, then the surfaces (both 2D arrays, a layer each). */
    inline constexpr uint32_t GroundLookBinding    = 0;
    inline constexpr uint32_t GroundSurfaceBinding = 1;

    /**
     * The ground's textures, their sampler and the set that shows them to fragment shaders. The destructor runs
     * Destroy, so nothing leaks when Create fails part way.
     */
    class TerrainMaterials final
    {
    public:
        TerrainMaterials() = default;
        ~TerrainMaterials();

        TerrainMaterials(const TerrainMaterials&)            = delete;
        TerrainMaterials& operator=(const TerrainMaterials&) = delete;

        /**
         * Makes the textures (waiting until the GPU has filled every level), the sampler and the set; replaces what an
         * earlier Create made. False (and logged) when the GPU cannot write and filter the textures' format or Vulkan
         * refuses. The device must outlive it.
         */
        [[nodiscard]] bool Create(const Device& device);

        /** Frees everything Create made; safe to call more than once or after a failed Create. */
        void Destroy();

        /** Binds the set at set index setIndex of a graphics pipeline's layout. */
        void Bind(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, uint32_t setIndex) const;

        /** The set's layout, for pipelines that read the ground's textures; VK_NULL_HANDLE before Create. */
        [[nodiscard]] VkDescriptorSetLayout GetSetLayout() const noexcept { return m_set.GetLayout(); }

    private:
        /** Fills every mip level of both textures with the compute shader, one level at a time; false when it fails. */
        [[nodiscard]] bool Fill(const Device& device);

        VkDevice      m_device = VK_NULL_HANDLE;
        Image         m_look;
        Image         m_surface;
        VkSampler     m_sampler = VK_NULL_HANDLE;
        DescriptorSet m_set;
    };
} // namespace Wotah
