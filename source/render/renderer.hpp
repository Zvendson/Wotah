/**
 * @file renderer.hpp
 * Runs a frame: waits for the GPU, takes a swapchain image, records into a command buffer that draws into it (with a
 * depth image, so nearer surfaces hide farther ones) with dynamic rendering, submits the work and shows the image. What
 * is drawn stays with the caller. Between BeginFrame and BeginScene the frame can record its own passes first, such as
 * the shadow maps, and the picture a flat mirror shows (BeginMirror, render/mirror_view.hpp). After the solid scene,
 * BeginSeeThrough lets what lies over it (water) read the solid scene behind each pixel: how far, and its colors
 * (render/scene_behind.hpp).
 *
 *     Wotah::Renderer renderer;
 *     if (renderer.Create(device, swapchain))
 *     {
 *         const Wotah::FrameStart start = renderer.BeginFrame();
 *         if (start == Wotah::FrameStart::Ready)
 *         {
 *             // passes before the scene, e.g. the shadow maps
 *             renderer.BeginMirror();      // optional: what the water mirrors, read through renderer.GetMirror()
 *             renderer.BeginScene();
 *             vkCmdBindPipeline(renderer.GetCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.GetHandle());
 *             vkCmdDraw(renderer.GetCommandBuffer(), 3, 1, 0, 0);
 *             renderer.BeginSeeThrough();  // optional: water and the like, reading renderer.GetSceneBehind()
 *             renderer.EndFrame();
 *         }
 *     }
 *     renderer.Destroy();
 */
#pragma once

#include "render/image.hpp"
#include "render/mirror_view.hpp"
#include "render/scene_behind.hpp"

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
        /** The command buffer is recording the next frame: record passes, BeginScene, draw, then EndFrame. */
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
         * Creates the depth image, the solid scene's copy and the mirror for shaders to read (GetSceneBehind,
         * GetMirror), the command pool and buffer, the semaphores and the frame fence for the swapchain; false (and
         * logged) when one fails. The device and the swapchain must outlive the renderer.
         */
        [[nodiscard]] bool Create(const Device& device, Swapchain& swapchain);

        /** Frees everything Create made; safe to call more than once or after a failed Create. */
        void Destroy();

        /**
         * Makes the swapchain again at the window's current size (after Window::TakeResize or Swapchain::IsOutOfDate),
         * with a depth image, scene copy, mirror and semaphores to match. Waits until the GPU is idle first. The
         * pipelines stay, as viewport and scissor are set every frame; false (and logged) when Vulkan fails or the
         * image format changed.
         */
        [[nodiscard]] bool RecreateSwapchain(const Device& device, const Window& window);

        /** The depth image's format; pipelines that draw in this renderer's frames need it (DepthFormat). */
        [[nodiscard]] VkFormat GetDepthFormat() const noexcept { return m_depth.GetFormat(); }

        /** The color every frame starts with. */
        void SetClearColor(const VkClearColorValue& color) noexcept { m_clearColor = color; }

        /**
         * Waits until the GPU has finished the previous frame, takes the next swapchain image and starts recording. On
         * Ready, record passes that come before the scene (shadow maps), then BeginScene, draw, and EndFrame.
         */
        [[nodiscard]] FrameStart BeginFrame();

        /**
         * Starts drawing the mirror's picture (render/mirror_view.hpp): cleared, viewport and scissor at its size. Draw
         * what a flat mirror shows, through MakeMirrorViewProjection, with pipelines that cull clockwise front faces.
         * At most once per frame, after BeginFrame said Ready and before BeginScene; refused (logged) otherwise.
         */
        void BeginMirror();

        /**
         * Starts drawing into the frame's image: the image and the depth are cleared, and viewport and scissor cover
         * all of it. The mirror's picture is done first (only cleared if BeginMirror did not run). Once per frame,
         * after BeginFrame said Ready.
         */
        void BeginScene();

        /**
         * Ends the solid part of the scene and starts what lies over it (water, glass): the depth drawn so far stays
         * the depth test but is no longer written, and fragment shaders can read it and a copy of the colors drawn so
         * far through GetSceneBehind. Pipelines drawn after it must not write depth. At most once per frame, after
         * BeginScene; refused (logged) otherwise.
         */
        void BeginSeeThrough();

        /** The mirror's picture for shaders drawn after BeginScene; they bind it themselves. */
        [[nodiscard]] const MirrorView& GetMirror() const noexcept { return m_mirror; }

        /** The solid scene's depth and colors for shaders drawn after BeginSeeThrough; they bind it themselves. */
        [[nodiscard]] const SceneBehind& GetSceneBehind() const noexcept { return m_sceneBehind; }

        /** The command buffer of the frame between BeginFrame and EndFrame. */
        [[nodiscard]] VkCommandBuffer GetCommandBuffer() const noexcept { return m_commandBuffer; }

        /**
         * Stops recording, submits the frame and shows the image; false (and logged) when Vulkan failed. Runs
         * BeginScene first if the frame never did, so the image is still cleared.
         */
        [[nodiscard]] bool EndFrame();

    private:
        /**
         * Makes the images as large as the swapchain's: the depth image, the solid scene's copy and the mirror; false
         * (and logged) when one fails. Replaces earlier ones, which the GPU must be done with.
         */
        [[nodiscard]] bool CreateFrameImages(const Device& device, VkFormat depthFormat);

        /** Makes one "drawn" semaphore per swapchain image; false (and logged) when one fails. */
        [[nodiscard]] bool CreateDrawnSemaphores();

        /** Frees the "drawn" semaphores. */
        void DestroyDrawnSemaphores() noexcept;

        /**
         * Begins a rendering into the frame's image and the depth image, with viewport and scissor covering all of
         * it: the scene's first (both cleared, depth written) or the see-through one (both kept, depth only read).
         */
        void BeginRendering(bool isSeeThrough);

        /** The device and swapchain the renderer draws with; not owned. */
        VkDevice   m_device    = VK_NULL_HANDLE;
        VkQueue    m_queue     = VK_NULL_HANDLE;
        Swapchain* m_swapchain = nullptr;

        VkClearColorValue m_clearColor{};

        /** The depth of every pixel drawn this frame; one image, as only one frame is in flight. */
        Image m_depth;

        /** The depth image and a copy of the colors as fragment shaders read them after BeginSeeThrough. */
        SceneBehind m_sceneBehind;

        /** What a flat mirror (the sea) shows, drawn after BeginMirror, read after BeginScene. */
        MirrorView m_mirror;

        VkCommandPool            m_commandPool    = VK_NULL_HANDLE;
        VkCommandBuffer          m_commandBuffer  = VK_NULL_HANDLE;
        VkSemaphore              m_imageAvailable = VK_NULL_HANDLE; // signalled when the taken image can be written
        std::vector<VkSemaphore> m_renderFinished;                  // one per swapchain image: its frame is drawn
        VkFence                  m_frameFence = VK_NULL_HANDLE;     // signalled when the GPU finished the frame

        /** The swapchain image of the frame being recorded. */
        uint32_t m_imageIndex = 0;

        /** Whether BeginMirror, BeginScene and BeginSeeThrough ran in the frame being recorded. */
        bool m_isMirrorBegun     = false;
        bool m_isSceneBegun      = false;
        bool m_isSeeThroughBegun = false;
    };
} // namespace Wotah
