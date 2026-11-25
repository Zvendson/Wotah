/**
 * @file window.hpp
 * The program's window, built on GLFW: opens it, shows it, collects its input events and gives Vulkan a surface to draw
 * on.
 *
 *     Wotah::Window window;
 *     if (window.Create(size, "Title"))
 *     {
 *         window.Show();
 *         while (!window.ShouldClose())
 *         {
 *             window.PollEvents();
 *             for (const Wotah::InputEvent& event : window.GetInputEvents())
 *             {
 *                 dispatcher.Dispatch(event);
 *             }
 *             // draw a frame
 *         }
 *     }
 *     window.Destroy();
 *
 * GLFW stays inside window.cpp; this header only forward-declares the handle types it passes around.
 */
#pragma once

#include "input/input_event.hpp"
#include "render/vulkan_handles.hpp"

#include <glm/vec2.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

struct GLFWwindow;

namespace Wotah
{
    /** A size in pixels. */
    struct WindowSize
    {
        uint32_t Width  = 0;
        uint32_t Height = 0;
    };

    /**
     * The one window of the program. It starts GLFW in Create and stops it in Destroy, so there is only ever one.
     * The destructor runs Destroy, so nothing leaks when Create fails part way.
     */
    class Window final
    {
    public:
        Window() = default;
        ~Window();

        Window(const Window&)            = delete;
        Window& operator=(const Window&) = delete;

        /**
         * Starts GLFW and opens a resizable window without an OpenGL context, still hidden; false (and logged)
         * when that fails.
         */
        [[nodiscard]] bool Create(const WindowSize& size, const std::string& title);

        /** Closes the window and stops GLFW; safe to call more than once or after a failed Create. */
        void Destroy();

        /** Shows the window. */
        void Show();

        /** Whether the user asked to close the window. */
        [[nodiscard]] bool ShouldClose() const;

        /**
         * Handles the waiting window events and collects the input among them for GetInputEvents, replacing the last
         * frame's. While the window is minimized or has no drawable area it sleeps until that changes or the window is
         * closed, so no frames are drawn for nothing.
         */
        void PollEvents();

        /** The input events of the last PollEvents, in the order they happened. */
        [[nodiscard]] std::span<const InputEvent> GetInputEvents() const noexcept { return m_inputEvents; }

        /**
         * Hides the cursor and locks it to the window, so the mouse can move without limits (e.g. to turn a camera),
         * or gives it back. Mouse movement then reports the raw motion where the system offers it.
         */
        void SetCursorCaptured(bool isCaptured);

        /**
         * Whether the drawable area changed size since the last call (the user resized the window, or SetSize did):
         * the swapchain must be made again. Answers each change once.
         */
        [[nodiscard]] bool TakeResize() noexcept;

        /** Resizes the window (its inside, in screen units); logged and ignored for a size of 0. */
        void SetSize(const WindowSize& size);

        /** The size of the drawable area in pixels; it can differ from the window size on high-DPI screens. */
        [[nodiscard]] WindowSize GetFramebufferSize() const;

        /**
         * The Vulkan instance extensions this window needs for a surface; empty (and logged) when Vulkan is not
         * available.
         */
        [[nodiscard]] std::span<const char* const> GetRequiredInstanceExtensions() const;

        /**
         * Creates a surface for the window; the caller owns it and frees it with vkDestroySurfaceKHR. A null
         * handle (and a logged reason) when that fails.
         */
        [[nodiscard]] VkSurfaceKHR CreateSurface(VkInstance instance) const;

    private:
        /** GLFW's input callbacks; defined in window.cpp, they add to m_inputEvents. */
        struct Callbacks;
        friend struct Callbacks;

        /** The GLFW window; null before Create and after Destroy. */
        GLFWwindow* m_window = nullptr;

        /** The input of the last PollEvents; kept between frames so it does not allocate again. */
        std::vector<InputEvent> m_inputEvents;

        /** The cursor at the last mouse movement, for the next movement's Delta; unknown until the first one. */
        glm::vec2 m_lastCursorPosition{0.0f};
        bool      m_hasLastCursorPosition = false;

        /** Whether SetCursorCaptured locked the cursor. */
        bool m_isCursorCaptured = false;

        /** Whether the drawable area changed size since TakeResize last answered. */
        bool m_wasResized = false;

        /** Whether the drawable area is larger than nothing in both directions. */
        [[nodiscard]] bool HasDrawableArea() const;
    };
} // namespace Wotah
