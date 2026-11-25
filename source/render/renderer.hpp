/**
 * @file renderer.hpp
 * Runs a frame: waits for the GPU, takes a swapchain image, records into a command buffer that draws into it (with a
 * depth image, so nearer surfaces hide farther ones) with dynamic rendering, submits the work and shows the image. What
 * is drawn stays with the caller.
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

#include "render/image.hpp"

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <vector>

namespace Wotah
{
    class Device;
    class Swapchain;
    class Window;

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
         * Creates the depth image, the command pool and buffer, the semaphores and the frame fence for the swapchain;
         * false (and logged) when one fails. The device and the swapchain must outlive the renderer.
         */
        [[nodiscard]] bool Create(const Device& device, Swapchain& swapchain);

        /** Frees everything Create made; safe to call more than once or after a failed Create. */
        void Destroy();

        /**
         * Makes the swapchain again at the window's current size (after Window::TakeResize or Swapchain::IsOutOfDate),
         * with a depth image and semaphores to match. Waits until the GPU is idle first. The pipelines stay, as
         * viewport and scissor are set every frame; false (and logged) when Vulkan fails or the image format changed.
         */
        [[nodiscard]] bool RecreateSwapchain(const Device& device, const Window& window);

        /** The depth image's format; pipelines that draw in this renderer's frames need it (DepthFormat). */
        [[nodiscard]] VkFormat GetDepthFormat() const noexcept { return m_depth.GetFormat(); }

        /** The color every frame starts with. */
        void SetClearColor(const VkClearColorValue& color) noexcept { m_clearColor = color; }

        /**
         * Waits until the GPU has finished the previous frame, takes the next swapchain image and starts recording:
         * the image and the depth are cleared, and viewport and scissor cover all of it. On Ready, draw into
         * GetCommandBuffer and call EndFrame.
         */
        [[nodiscard]] FrameStart BeginFrame();

        /** The command buffer of the frame between BeginFrame and EndFrame. */
        [[nodiscard]] VkCommandBuffer GetCommandBuffer() const noexcept { return m_commandBuffer; }

        /** Stops recording, submits the frame and shows the image; false (and logged) when Vulkan failed. */
        [[nodiscard]] bool EndFrame();

    private:
        /** Makes one "drawn" semaphore per swapchain image; false (and logged) when one fails. */
        [[nodiscard]] bool CreateDrawnSemaphores();

        /** Frees the "drawn" semaphores. */
        void DestroyDrawnSemaphores() noexcept;

        /** The device and swapchain the renderer draws with; not owned. */
        VkDevice   m_device    = VK_NULL_HANDLE;
        VkQueue    m_queue     = VK_NULL_HANDLE;
        Swapchain* m_swapchain = nullptr;

        VkClearColorValue m_clearColor{};

        /** The depth of every pixel drawn this frame; one image, as only one frame is in flight. */
        Image m_depth;

        VkCommandPool            m_commandPool    = VK_NULL_HANDLE;
        VkCommandBuffer          m_commandBuffer  = VK_NULL_HANDLE;
        VkSemaphore              m_imageAvailable = VK_NULL_HANDLE; // signalled when the taken image can be written
        std::vector<VkSemaphore> m_renderFinished;                  // one per swapchain image: its frame is drawn
        VkFence                  m_frameFence = VK_NULL_HANDLE;     // signalled when the GPU finished the frame

        /** The swapchain image of the frame being recorded. */
        uint32_t m_imageIndex = 0;
    };
} // namespace Wotah
