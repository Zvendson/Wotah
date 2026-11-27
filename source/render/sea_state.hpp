/**
 * @file sea_state.hpp
 * The sea on the GPU: its wind waves and swell as the shaders read them (shaders/sea_waves.glsl), with the sea floor's
 * shape beside them, in one descriptor set that the water's and the land's pipelines read at SeaStateSet: the water to
 * move and shade itself, the land to show where the swash wet the sand. Update rewrites it every frame.
 *
 *     Wotah::SeaState sea;
 *     sea.Create(device);                     // still water until Update
 *     // pipelines that read it get sea.GetSetLayout() at SeaStateSet
 *     sea.SetFloor(land.GetShapeBuffer());    // before the first draw that reads it
 *     // every frame, after Renderer::BeginFrame:
 *     sea.Update(seaNow);
 *     sea.Bind(commands, pipeline.GetLayout());
 */
#pragma once

#include "render/buffer.hpp"
#include "render/descriptor_set.hpp"

#include <vulkan/vulkan_core.h>

#include <cstdint>

namespace Wotah
{
    class Device;
    struct Sea;

    /** The set index the water's and the land's shaders read the sea from (sea_waves.glsl: set 3). */
    inline constexpr uint32_t SeaStateSet = 3;

    /** The sea's uniform buffer and its set. The destructor frees both. */
    class SeaState final
    {
    public:
        /**
         * Makes the buffer and the set, still water in it; false (and logged) on failure. The device must outlive it.
         */
        [[nodiscard]] bool Create(const Device& device);

        /** Frees everything; safe to call more than once or after a failed Create. */
        void Destroy();

        /**
         * Points the set's floor at shape, the land's shape buffer (terrain_shape.glsl's block,
         * TerrainRenderer::GetShapeBuffer), which must outlive the set; the sea's Floor must be that shape. Not while
         * the GPU reads the set.
         */
        void SetFloor(const Buffer& shape);

        /**
         * Puts the sea's waves where they stand at its clock into the buffer; only while the GPU reads none of it
         * (after Renderer::BeginFrame). False (and logged) when the buffer cannot be written.
         */
        bool Update(const Sea& sea);

        /** Binds the set at SeaStateSet of a pipeline's layout. */
        void Bind(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout) const;

        /** The set's layout, for the pipelines that read the sea at SeaStateSet. */
        [[nodiscard]] VkDescriptorSetLayout GetSetLayout() const noexcept { return m_set.GetLayout(); }

    private:
        Buffer        m_buffer;
        DescriptorSet m_set;
    };
} // namespace Wotah
