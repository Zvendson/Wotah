/**
 * @file ui_renderer.cpp
 * Dear ImGui's Vulkan backend (1.93.0, backends/imgui_impl_vulkan.h) on the program's device, with dynamic rendering.
 */
#include "render/ui_renderer.hpp"

#include "core/logger.hpp"
#include "render/device.hpp"
#include "render/vulkan_utils.hpp"

#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <vulkan/vulkan.h>

#include <atomic>
#include <cstdint>

namespace
{
    /**
     * How many sets of vertex and index buffers the backend takes turns with, one per frame that can be in flight. The
     * Renderer has one frame in flight, and 2 is the least the backend accepts (imgui_impl_vulkan.cpp,
     * ImGui_ImplVulkan_Init).
     */
    constexpr uint32_t BufferSetCount = 2;

    /** How many textures the backend's own descriptor pool holds: the least it accepts, room for the font pictures. */
    constexpr uint32_t TextureSlotCount = IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE;

    /** Set when the backend reported a failed Vulkan call since it was last cleared; it has no other way to tell. */
    std::atomic<bool> s_hasVulkanFailed = false;

    /** The backend reports the result of every Vulkan call it makes here; failures are logged and noted. */
    void NoteVulkanResult(VkResult result)
    {
        if (!Wotah::Succeeded(result, "run Dear ImGui's Vulkan backend"))
        {
            s_hasVulkanFailed = true;
        }
    }
} // namespace

Wotah::UiRenderer::~UiRenderer()
{
    Destroy();
}

bool Wotah::UiRenderer::Create(const Device& device, VkFormat colorFormat, VkFormat depthFormat)
{
    Destroy();
    if (ImGui::GetCurrentContext() == nullptr)
    {
        Logger::Error("The UI renderer needs the UI to run first");
        return false;
    }
    if (ImGui::GetIO().BackendRendererUserData != nullptr)
    {
        Logger::Error("The UI already has a renderer; there can only be one at a time");
        return false;
    }

    // The UI neither tests nor writes depth, but a pipeline names the depth format of the frames it draws in, which
    // have a depth image (Vulkan spec, vkCmdDraw: the formats must match unless dynamicRenderingUnusedAttachments is
    // on).
    VkPipelineRenderingCreateInfo rendering =
        MakeVulkanInfo<VkPipelineRenderingCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
    rendering.colorAttachmentCount    = 1;
    rendering.pColorAttachmentFormats = &colorFormat; // the backend keeps a copy
    rendering.depthAttachmentFormat   = depthFormat;

    // Zeroed first, as the backend asks (imgui_impl_vulkan.h, ImGui_ImplVulkan_InitInfo).
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion                                   = VK_API_VERSION_1_3; // the instance's (render/device.cpp)
    info.Instance                                     = device.GetInstance();
    info.PhysicalDevice                               = device.GetGpu();
    info.Device                                       = device.GetHandle();
    info.QueueFamily                                  = device.GetQueueFamily();
    info.Queue                                        = device.GetQueue();
    info.DescriptorPoolSize                           = TextureSlotCount;
    info.MinImageCount                                = BufferSetCount;
    info.ImageCount                                   = BufferSetCount;
    info.UseDynamicRendering                          = true;
    info.PipelineInfoMain.MSAASamples                 = VK_SAMPLE_COUNT_1_BIT;
    info.PipelineInfoMain.PipelineRenderingCreateInfo = rendering;
    info.CheckVkResultFn                              = NoteVulkanResult;

    s_hasVulkanFailed = false;
    m_isCreated       = ImGui_ImplVulkan_Init(&info);
    if (!m_isCreated || s_hasVulkanFailed)
    {
        Logger::Error("Could not start the UI renderer");
        Destroy();
        return false;
    }
    return true;
}

void Wotah::UiRenderer::Destroy()
{
    if (!m_isCreated)
    {
        return;
    }
    m_isCreated = false;

    // UserInterface refuses to stop while this runs, so the context is still there; checked all the same, as the
    // backend lives inside it.
    if (ImGui::GetCurrentContext() == nullptr)
    {
        Logger::Error("The UI stopped before its renderer; the renderer's GPU objects are lost");
        return;
    }
    ImGui_ImplVulkan_Shutdown();
}

bool Wotah::UiRenderer::Draw(VkCommandBuffer commandBuffer)
{
    if (!m_isCreated)
    {
        Logger::Error("The UI renderer draws only after Create");
        return false;
    }

    // ImGui_ImplVulkan_NewFrame only checks that Init ran (imgui_impl_vulkan.cpp, 1.93.0), as m_isCreated does here,
    // so it is not called.
    ImDrawData* drawData = ImGui::GetDrawData();
    if (drawData == nullptr)
    {
        return true; // no UI frame ended since the last one began
    }
    s_hasVulkanFailed = false;
    ImGui_ImplVulkan_RenderDrawData(drawData, commandBuffer);
    return !s_hasVulkanFailed;
}
