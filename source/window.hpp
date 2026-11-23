/**
 * @file window.hpp
 * The program's window, built on GLFW: opens it, shows it, handles its events and gives Vulkan a surface to draw on.
 *
 *     Wotah::Window window;
 *     if (window.Create(size, "Title"))
 *     {
 *         window.Show();
 *         while (!window.ShouldClose())
 *         {
 *             window.PollEvents();
 *             // draw a frame
 *         }
 *     }
 *     window.Destroy();
 *
 * GLFW stays inside window.cpp; this header only forward-declares the handle types it passes around.
 */
#pragma once

#include "vulkan_handles.hpp"

#include <cstdint>
#include <span>
#include <string>

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
         * Starts GLFW and opens a fixed-size window without an OpenGL context, still hidden; false (and logged)
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
         * Handles the waiting window events. While the window is minimized it sleeps until the window comes back or
         * is closed, so no frames are drawn for nothing.
         */
        void PollEvents();

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
        /** The GLFW window; null before Create and after Destroy. */
        GLFWwindow* m_window = nullptr;
    };
} // namespace Wotah
