/**
 * @file renderer.hpp
 * Runs a frame: waits for the GPU, takes a swapchain image, records into a command buffer that draws into it with
 * dynamic rendering, submits the work and shows the image. What is drawn stays with the caller.
 *
 *     Wotah::Renderer renderer;
 *     if (renderer.Create(device, swapchain))
 *     {
 *         const Wotah::FrameStart start = renderer.BeginFrame();
 *         if (start == Wotah::FrameStart::Ready)
 *         {
 *             vkCmdBindPipeline(renderer.GetCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.GetHandle());
 *             vkCmdDraw(renderer.GetCommandBuffer(), 3, 1, 0, 0);
 *             renderer.EndFrame();
 *         }
 *     }
 *     renderer.Destroy();
 */
#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <vector>

namespace Wotah
{
    class Device;
    class Swapchain;

    /** What BeginFrame did. */
    enum class FrameStart
    {
        /** The command buffer is recording into the next image: draw, then call EndFrame. */
        Ready,
        /** No image could be taken this time; skip the frame and try again. */
        Skip,
        /** Vulkan failed (logged); stop drawing. */
        Failed,
    };

    /**
     * One command buffer with its semaphores and fence: one frame in flight. The destructor runs Destroy, so nothing
     * leaks when Create fails part way.
     */
    class Renderer final
    {
    public:
        Renderer() = default;
        ~Renderer();

        Renderer(const Renderer&)            = delete;
        Renderer& operator=(const Renderer&) = delete;

        /**
         * Creates the command pool and buffer, the semaphores and the frame fence for the swapchain; false (and logged)
         * when one fails. The device and the swapchain must outlive the renderer.
         */
        [[nodiscard]] bool Create(const Device& device, Swapchain& swapchain);

        /** Frees everything Create made; safe to call more than once or after a failed Create. */
        void Destroy();

        /** The color every frame starts with. */
        void SetClearColor(const VkClearColorValue& color) noexcept { m_clearColor = color; }

        /**
         * Waits until the GPU has finished the previous frame, takes the next swapchain image and starts recording:
         * the image is cleared, and viewport and scissor cover all of it. On Ready, draw into GetCommandBuffer and
         * call EndFrame.
         */
        [[nodiscard]] FrameStart BeginFrame();

        /** The command buffer of the frame between BeginFrame and EndFrame. */
        [[nodiscard]] VkCommandBuffer GetCommandBuffer() const noexcept { return m_commandBuffer; }

        /** Stops recording, submits the frame and shows the image; false (and logged) when Vulkan failed. */
        [[nodiscard]] bool EndFrame();

    private:
        /** The device and swapchain the renderer draws with; not owned. */
        VkDevice   m_device    = VK_NULL_HANDLE;
        VkQueue    m_queue     = VK_NULL_HANDLE;
        Swapchain* m_swapchain = nullptr;

        VkClearColorValue m_clearColor{};

        VkCommandPool            m_commandPool    = VK_NULL_HANDLE;
        VkCommandBuffer          m_commandBuffer  = VK_NULL_HANDLE;
        VkSemaphore              m_imageAvailable = VK_NULL_HANDLE; // signalled when the taken image can be written
        std::vector<VkSemaphore> m_renderFinished;                  // one per swapchain image: its frame is drawn
        VkFence                  m_frameFence = VK_NULL_HANDLE;     // signalled when the GPU finished the frame

        /** The swapchain image of the frame being recorded. */
        uint32_t m_imageIndex = 0;
    };
} // namespace Wotah
