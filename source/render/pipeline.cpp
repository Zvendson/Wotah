/**
 * @file pipeline.cpp
 * Builds a graphics pipeline for dynamic rendering from PipelineSettings, and frees it.
 */
#include "render/pipeline.hpp"

#include "core/logger.hpp"
#include "render/device.hpp"
#include "render/vulkan_utils.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace
{
    /** The shaders' entry point; every shader of the project names its entry point main. */
    constexpr const char* ShaderEntryPoint = "main";

    /** Write all four color channels. */
    constexpr VkColorComponentFlags AllColorChannels =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    /** Viewport and scissor are set per frame, so a window size change needs no new pipeline. */
    constexpr std::array<VkDynamicState, 2> DynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    /** A shader module that frees itself; it is only needed while the pipeline is built. */
    class ShaderModule final
    {
    public:
        /** Creates the module from SPIR-V words; Get is VK_NULL_HANDLE (and the reason logged) on failure. */
        ShaderModule(VkDevice device, std::span<const uint32_t> code, const char* name)
            : m_device(device)
        {
            VkShaderModuleCreateInfo createInfo =
                Wotah::MakeVulkanInfo<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
            createInfo.codeSize = code.size_bytes();
            createInfo.pCode    = code.data();
            if (vkCreateShaderModule(m_device, &createInfo, nullptr, &m_module) != VK_SUCCESS)
            {
                Wotah::Logger::Error("Could not create the {} shader module", name);
                m_module = VK_NULL_HANDLE;
            }
        }

        ~ShaderModule() { vkDestroyShaderModule(m_device, m_module, nullptr); }

        ShaderModule(const ShaderModule&)            = delete;
        ShaderModule& operator=(const ShaderModule&) = delete;

        /** The module; VK_NULL_HANDLE when creating it failed. */
        VkShaderModule Get() const noexcept { return m_module; }

    private:
        VkDevice       m_device = VK_NULL_HANDLE;
        VkShaderModule m_module = VK_NULL_HANDLE;
    };

    /** Whether a list is short enough for Vulkan's uint32_t counts. */
    template <typename T>
    bool FitsCount(std::span<const T> list)
    {
        return list.size() <= std::numeric_limits<uint32_t>::max();
    }

    /** A list's length as Vulkan's uint32_t count; only after FitsCount said yes. */
    template <typename T>
    uint32_t CountOf(std::span<const T> list)
    {
        return static_cast<uint32_t>(list.size());
    }

    /** Whether the settings fit together; every problem is logged. */
    bool AreSettingsUsable(const Wotah::PipelineSettings& settings)
    {
        bool usable = true;
        if (settings.VertexShader.empty())
        {
            Wotah::Logger::Error("A pipeline needs a vertex shader");
            usable = false;
        }
        // A fragment shader writes color, so it comes with a color format; without both the pipeline writes depth only.
        const bool hasFragmentShader = !settings.FragmentShader.empty();
        const bool hasColorFormat    = settings.ColorFormat != VK_FORMAT_UNDEFINED;
        if (hasFragmentShader != hasColorFormat)
        {
            Wotah::Logger::Error("A pipeline needs a fragment shader and a color format together, or neither");
            usable = false;
        }
        if (!hasColorFormat && settings.DepthFormat == VK_FORMAT_UNDEFINED)
        {
            Wotah::Logger::Error("A pipeline needs a color or a depth format to draw into");
            usable = false;
        }
        if ((settings.DepthTest || settings.DepthWrite) && settings.DepthFormat == VK_FORMAT_UNDEFINED)
        {
            Wotah::Logger::Error("Depth testing or writing needs a depth format");
            usable = false;
        }
        if (!FitsCount(settings.VertexBindings) || !FitsCount(settings.VertexAttributes) ||
            !FitsCount(settings.PushConstants) || !FitsCount(settings.DescriptorSetLayouts))
        {
            Wotah::Logger::Error("A pipeline setting list is longer than Vulkan can count");
            usable = false;
        }
        return usable;
    }

    /** The blend state for one color image. */
    VkPipelineColorBlendAttachmentState MakeBlendState(Wotah::BlendMode mode)
    {
        VkPipelineColorBlendAttachmentState state{};
        state.colorWriteMask = AllColorChannels;
        if (mode == Wotah::BlendMode::Opaque)
        {
            return state;
        }

        // Color: output * alpha + color * (1 - alpha) for Alpha, output * alpha + color for Additive.
        state.blendEnable         = VK_TRUE;
        state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        state.dstColorBlendFactor =
            mode == Wotah::BlendMode::Alpha ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA : VK_BLEND_FACTOR_ONE;
        state.colorBlendOp = VK_BLEND_OP_ADD;

        // Alpha: coverage adds up like layered glass, alpha + image alpha * (1 - alpha) (Porter-Duff "over").
        state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        state.alphaBlendOp        = VK_BLEND_OP_ADD;
        return state;
    }
} // namespace

Wotah::Pipeline::~Pipeline()
{
    Destroy();
}

bool Wotah::Pipeline::Create(const Device& device, const PipelineSettings& settings)
{
    if (!AreSettingsUsable(settings))
    {
        return false;
    }
    m_device = device.GetHandle();

    // ---- Layout: what data the shaders get -----------------------------------
    VkPipelineLayoutCreateInfo layoutInfo =
        Wotah::MakeVulkanInfo<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    layoutInfo.setLayoutCount         = CountOf(settings.DescriptorSetLayouts);
    layoutInfo.pSetLayouts            = settings.DescriptorSetLayouts.data();
    layoutInfo.pushConstantRangeCount = CountOf(settings.PushConstants);
    layoutInfo.pPushConstantRanges    = settings.PushConstants.data();
    if (!Succeeded(vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_layout), "create the pipeline layout"))
    {
        return false;
    }

    // ---- Shaders ---------------------------------------------------------------
    // A depth-only pipeline has no fragment shader; the module is only made when there is one.
    const bool                  isDepthOnly = settings.FragmentShader.empty();
    const ShaderModule          vertexShader(m_device, settings.VertexShader, "vertex");
    std::optional<ShaderModule> fragmentShader;
    if (!isDepthOnly)
    {
        fragmentShader.emplace(m_device, settings.FragmentShader, "fragment");
    }
    if (vertexShader.Get() == VK_NULL_HANDLE || (fragmentShader && fragmentShader->Get() == VK_NULL_HANDLE))
    {
        return false;
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexShader.Get();
    stages[0].pName  = ShaderEntryPoint;
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentShader ? fragmentShader->Get() : VK_NULL_HANDLE;
    stages[1].pName  = ShaderEntryPoint;

    // ---- Fixed steps -------------------------------------------------------------
    VkPipelineVertexInputStateCreateInfo vertexInput = Wotah::MakeVulkanInfo<VkPipelineVertexInputStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
    vertexInput.vertexBindingDescriptionCount   = CountOf(settings.VertexBindings);
    vertexInput.pVertexBindingDescriptions      = settings.VertexBindings.data();
    vertexInput.vertexAttributeDescriptionCount = CountOf(settings.VertexAttributes);
    vertexInput.pVertexAttributeDescriptions    = settings.VertexAttributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly =
        Wotah::MakeVulkanInfo<VkPipelineInputAssemblyStateCreateInfo>(
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
    inputAssembly.topology = settings.Topology;

    VkPipelineViewportStateCreateInfo viewport =
        Wotah::MakeVulkanInfo<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
    viewport.viewportCount = 1;
    viewport.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rasterization =
        Wotah::MakeVulkanInfo<VkPipelineRasterizationStateCreateInfo>(
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
    rasterization.polygonMode = settings.PolygonMode;
    rasterization.cullMode    = settings.CullMode;
    rasterization.frontFace   = settings.FrontFace;
    rasterization.lineWidth   = 1.0f; // wider lines need the wideLines GPU feature
    if (settings.DepthBiasConstant != 0.0f || settings.DepthBiasSlope != 0.0f)
    {
        rasterization.depthBiasEnable         = VK_TRUE;
        rasterization.depthBiasConstantFactor = settings.DepthBiasConstant;
        rasterization.depthBiasSlopeFactor    = settings.DepthBiasSlope;
    }

    // shortcut: one sample per pixel; add a sample count here together with a multisampled color image.
    VkPipelineMultisampleStateCreateInfo multisample = Wotah::MakeVulkanInfo<VkPipelineMultisampleStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth = Wotah::MakeVulkanInfo<VkPipelineDepthStencilStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
    depth.depthTestEnable  = settings.DepthTest ? VK_TRUE : VK_FALSE;
    depth.depthWriteEnable = settings.DepthWrite ? VK_TRUE : VK_FALSE;
    depth.depthCompareOp   = settings.DepthCompare;

    const VkPipelineColorBlendAttachmentState blendState = MakeBlendState(settings.Blend);
    VkPipelineColorBlendStateCreateInfo       blend      = Wotah::MakeVulkanInfo<VkPipelineColorBlendStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
    blend.attachmentCount = isDepthOnly ? 0 : 1;
    blend.pAttachments    = &blendState;

    VkPipelineDynamicStateCreateInfo dynamic =
        Wotah::MakeVulkanInfo<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
    dynamic.dynamicStateCount = static_cast<uint32_t>(DynamicStates.size());
    dynamic.pDynamicStates    = DynamicStates.data();

    // Dynamic rendering: the pipeline is told the image formats instead of a render pass.
    // shortcut: no stencil image; add a stencil format once something draws with a stencil.
    VkPipelineRenderingCreateInfo rendering =
        Wotah::MakeVulkanInfo<VkPipelineRenderingCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
    rendering.colorAttachmentCount    = isDepthOnly ? 0 : 1;
    rendering.pColorAttachmentFormats = isDepthOnly ? nullptr : &settings.ColorFormat;
    rendering.depthAttachmentFormat   = settings.DepthFormat;

    // ---- The pipeline ------------------------------------------------------------
    VkGraphicsPipelineCreateInfo createInfo =
        Wotah::MakeVulkanInfo<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    createInfo.pNext               = &rendering;
    createInfo.stageCount          = isDepthOnly ? 1 : static_cast<uint32_t>(stages.size());
    createInfo.pStages             = stages.data();
    createInfo.pVertexInputState   = &vertexInput;
    createInfo.pInputAssemblyState = &inputAssembly;
    createInfo.pViewportState      = &viewport;
    createInfo.pRasterizationState = &rasterization;
    createInfo.pMultisampleState   = &multisample;
    createInfo.pDepthStencilState  = &depth;
    createInfo.pColorBlendState    = &blend;
    createInfo.pDynamicState       = &dynamic;
    createInfo.layout              = m_layout;
    return Succeeded(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &createInfo, nullptr, &m_pipeline),
                     "create the graphics pipeline");
}

void Wotah::Pipeline::Destroy()
{
    if (m_device != VK_NULL_HANDLE)
    {
        // The pipeline before the layout it was built with; both accept VK_NULL_HANDLE (Vulkan spec).
        vkDestroyPipeline(m_device, m_pipeline, nullptr);
        vkDestroyPipelineLayout(m_device, m_layout, nullptr);
    }
    m_device   = VK_NULL_HANDLE;
    m_layout   = VK_NULL_HANDLE;
    m_pipeline = VK_NULL_HANDLE;
}
