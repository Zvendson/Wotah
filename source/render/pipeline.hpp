/**
 * @file pipeline.hpp
 * A graphics pipeline for dynamic rendering, built from a settings struct: the shaders, the vertex layout, how
 * triangles are filled and culled, depth testing, blending, and what data the shaders get (push constants and
 * descriptor sets).
 *
 *     Wotah::PipelineSettings settings;
 *     settings.VertexShader   = MyVertexShader;   // compiled-in SPIR-V
 *     settings.FragmentShader = MyFragmentShader;
 *     settings.ColorFormat    = swapchain.GetFormat();
 *     settings.Blend          = Wotah::BlendMode::Alpha;
 *
 *     Wotah::Pipeline pipeline;
 *     if (pipeline.Create(device, settings))
 *     {
 *         vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.GetHandle());
 *     }
 *     pipeline.Destroy();
 *
 * Viewport and scissor are always dynamic: set them with vkCmdSetViewport and vkCmdSetScissor before drawing.
 */
#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <span>

namespace Wotah
{
    class Device;

    /** How a pipeline mixes its output with the color already in the image. */
    enum class BlendMode
    {
        /** The output replaces the color. */
        Opaque,
        /** Standard transparency: output * alpha + color * (1 - alpha). */
        Alpha,
        /** Light that adds up: output * alpha + color. */
        Additive,
    };

    /**
     * Everything a pipeline is built from. Only the shaders and the color format have to be set; every other field has
     * a default that draws filled, unculled triangles without vertex buffers, depth or blending. A depth-only pipeline
     * (shadow maps) leaves out the fragment shader and the color format and sets a DepthFormat.
     * The spans only need to stay valid during Pipeline::Create, which copies what it needs.
     */
    struct PipelineSettings
    {
        /** The vertex shader's SPIR-V words; its entry point is "main". */
        std::span<const uint32_t> VertexShader;
        /** The fragment shader's SPIR-V words; its entry point is "main". Empty for a depth-only pipeline. */
        std::span<const uint32_t> FragmentShader;

        /** The format of the image the pipeline draws into, e.g. Swapchain::GetFormat; undefined for depth only. */
        VkFormat ColorFormat = VK_FORMAT_UNDEFINED;
        /** The format of the depth image; VK_FORMAT_UNDEFINED draws without one. */
        VkFormat DepthFormat = VK_FORMAT_UNDEFINED;

        /** The vertex buffers' strides and rates; empty when the shader makes its own vertices. */
        std::span<const VkVertexInputBindingDescription> VertexBindings;
        /** Where each vertex shader input is found in those buffers. */
        std::span<const VkVertexInputAttributeDescription> VertexAttributes;

        /** How vertices form shapes. */
        VkPrimitiveTopology Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        /** Filled triangles, or only their edges (VK_POLYGON_MODE_LINE needs the fillModeNonSolid GPU feature). */
        VkPolygonMode PolygonMode = VK_POLYGON_MODE_FILL;
        /** Which side of a triangle is skipped. */
        VkCullModeFlags CullMode = VK_CULL_MODE_NONE;
        /** Which winding counts as the front side. */
        VkFrontFace FrontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        /** Whether fragments behind what is already drawn are dropped; needs a DepthFormat. */
        bool DepthTest = false;
        /** Whether drawn fragments write their depth; needs a DepthFormat. */
        bool DepthWrite = false;
        /** Which fragments pass the depth test: by default those closer than what is drawn. */
        VkCompareOp DepthCompare = VK_COMPARE_OP_LESS;
        /**
         * How far written depths are pushed away: a constant part in the depth format's smallest steps, and a part
         * that grows with how steeply the triangle faces away. Both 0 (the default) push nothing; shadow maps push a
         * little, so lit surfaces do not shadow themselves.
         */
        float DepthBiasConstant = 0.0f;
        float DepthBiasSlope    = 0.0f;

        /** How the output mixes with the image's color. */
        BlendMode Blend = BlendMode::Opaque;

        /** The small values the shaders get straight from the command buffer (vkCmdPushConstants), e.g. a time. */
        std::span<const VkPushConstantRange> PushConstants;
        /** The layouts of the descriptor sets the shaders read (uniform buffers, textures); owned by the caller. */
        std::span<const VkDescriptorSetLayout> DescriptorSetLayouts;
    };

    /**
     * A graphics pipeline and its layout. The destructor runs Destroy, so nothing leaks when Create fails part way.
     */
    class Pipeline final
    {
    public:
        Pipeline() = default;
        ~Pipeline();

        Pipeline(const Pipeline&)            = delete;
        Pipeline& operator=(const Pipeline&) = delete;

        /**
         * Builds the layout and the pipeline from the settings; false (and logged) when the settings do not fit
         * together or Vulkan refuses them. The device must outlive the pipeline.
         */
        [[nodiscard]] bool Create(const Device& device, const PipelineSettings& settings);

        /** Frees the pipeline and its layout; safe to call more than once or after a failed Create. */
        void Destroy();

        /** The pipeline, for vkCmdBindPipeline. */
        [[nodiscard]] VkPipeline GetHandle() const noexcept { return m_pipeline; }

        /** The layout, for vkCmdPushConstants and vkCmdBindDescriptorSets. */
        [[nodiscard]] VkPipelineLayout GetLayout() const noexcept { return m_layout; }

    private:
        /** The device the pipeline belongs to; not owned, kept for Destroy. */
        VkDevice m_device = VK_NULL_HANDLE;

        VkPipelineLayout m_layout   = VK_NULL_HANDLE;
        VkPipeline       m_pipeline = VK_NULL_HANDLE;
    };
} // namespace Wotah
