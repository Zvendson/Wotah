/**
 * @file ui_renderer.hpp
 * Draws the UI (ui/user_interface.hpp) into the frame with Dear ImGui's Vulkan backend: the windows' shapes and text,
 * and the font pictures ImGui makes as it needs them. Drawn last in the scene, so the UI lies in front of everything.
 *
 *     Wotah::UiRenderer uiRenderer;
 *     if (uiRenderer.Create(device, swapchain.GetFormat(), renderer.GetDepthFormat())) // after UserInterface::Create
 *     {
 *         // every frame, after UserInterface::EndFrame, between Renderer::BeginScene and EndFrame:
 *         uiRenderer.Draw(renderer.GetCommandBuffer());
 *     }
 *     uiRenderer.Destroy(); // once the GPU is idle, before UserInterface::Destroy
 *
 * ImGui keeps its backend inside its context, so there is one UiRenderer at a time, made after the UserInterface and
 * destroyed before it.
 */
#pragma once

#include <vulkan/vulkan_core.h>

namespace Wotah
{
    class Device;

    /**
     * ImGui's Vulkan backend: its pipeline, font pictures and vertex buffers. The destructor runs Destroy, so nothing
     * leaks when Create fails part way.
     */
    class UiRenderer final
    {
    public:
        UiRenderer() = default;
        ~UiRenderer();

        UiRenderer(const UiRenderer&)            = delete;
        UiRenderer& operator=(const UiRenderer&) = delete;

        /**
         * Builds ImGui's pipeline for frames with these color and depth formats (the Renderer's); false (and logged)
         * when the UI is not running yet, another UiRenderer runs, or Vulkan refuses. The device must outlive it.
         */
        [[nodiscard]] bool Create(const Device& device, VkFormat colorFormat, VkFormat depthFormat);

        /**
         * Frees what Create and Draw made; safe to call more than once or after a failed Create. The GPU must be done
         * with the frames that drew the UI, and the UI must still be running.
         */
        void Destroy();

        /**
         * Records the UI of the last ended UI frame (UserInterface::EndFrame); with none since the last BeginFrame it
         * records nothing. New font pictures go to the GPU first, in a submit of their own that it waits for, so no
         * other thread may use the device's queue meanwhile. Leaves its own viewport and scissor set. False (and
         * logged) when Vulkan failed or Create did not run.
         */
        [[nodiscard]] bool Draw(VkCommandBuffer commandBuffer);

    private:
        /** Whether Create attached ImGui's backend, so Destroy must take it away. */
        bool m_isCreated = false;
    };
} // namespace Wotah
