/**
 * @file descriptor_set.hpp
 * One descriptor set with its own layout and pool: what a group of shaders reads besides vertices and push constants
 * (uniform buffers, textures), at one set index of a pipeline layout. Binding i of the set is bindings[i].
 *
 *     const std::array<Wotah::DescriptorBinding, 1> bindings{
 *         {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT}}};
 *     Wotah::DescriptorSet shape;
 *     if (shape.Create(device, bindings))
 *     {
 *         shape.WriteBuffer(0, buffer.GetHandle(), sizeof(GpuShape));
 *         // pipelines get shape.GetLayout() at set 1, then while recording:
 *         shape.Bind(commands, pipeline.GetLayout(), 1);
 *     }
 */
#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <span>
#include <vector>

namespace Wotah
{
    class Device;

    /** One binding of a set: what kind of resource it is, and which shader stages read it. */
    struct DescriptorBinding
    {
        VkDescriptorType   Type   = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        VkShaderStageFlags Stages = VK_SHADER_STAGE_FRAGMENT_BIT;
    };

    /**
     * A descriptor set layout, a pool with room for exactly one set, and that set. The destructor runs Destroy, so
     * nothing leaks when Create fails part way. Rewriting a binding while the GPU reads it is not allowed: write
     * between frames (one frame is in flight; Renderer::BeginFrame waits for the last one).
     */
    class DescriptorSet final
    {
    public:
        DescriptorSet() = default;
        ~DescriptorSet();

        DescriptorSet(const DescriptorSet&)            = delete;
        DescriptorSet& operator=(const DescriptorSet&) = delete;

        /**
         * Creates the layout, the pool and the set for these bindings, replacing a set created before; false (and
         * logged) for no bindings or when Vulkan refuses. The device must outlive the set.
         */
        [[nodiscard]] bool Create(const Device& device, std::span<const DescriptorBinding> bindings);

        /** Frees the pool, the set with it, and the layout; safe more than once or after a failed Create. */
        void Destroy();

        /** Points a uniform buffer binding at the first size bytes of buffer; refused (logged) for another kind. */
        void WriteBuffer(uint32_t binding, VkBuffer buffer, VkDeviceSize size);

        /**
         * Points an image-with-sampler binding at an image view, read through sampler while in layout; refused
         * (logged) for another kind.
         */
        void WriteImage(uint32_t binding, VkImageView view, VkSampler sampler, VkImageLayout layout);

        /**
         * Points a storage image binding (read and written by shaders, in VK_IMAGE_LAYOUT_GENERAL) at an image view;
         * refused (logged) for another kind.
         */
        void WriteStorageImage(uint32_t binding, VkImageView view);

        /** Binds the set at set index setIndex of a pipeline's layout: a graphics one, or as bindPoint says. */
        void Bind(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, uint32_t setIndex,
                  VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS) const;

        /** The layout, for pipelines that read this set; VK_NULL_HANDLE before Create. */
        [[nodiscard]] VkDescriptorSetLayout GetLayout() const noexcept { return m_layout; }

    private:
        /** Whether binding exists and is of this kind; logged when not. */
        [[nodiscard]] bool IsBindingOf(uint32_t binding, VkDescriptorType type) const;

        VkDevice                      m_device = VK_NULL_HANDLE;
        VkDescriptorSetLayout         m_layout = VK_NULL_HANDLE;
        VkDescriptorPool              m_pool   = VK_NULL_HANDLE;
        VkDescriptorSet               m_set    = VK_NULL_HANDLE;
        std::vector<VkDescriptorType> m_types; // each binding's kind, by binding
    };
} // namespace Wotah
