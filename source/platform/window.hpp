/**
 * @file window.hpp
 * The program's window, built on GLFW: opens it, shows it, collects its input events and gives Vulkan a surface to draw
 * on. It is made for two threads: the main thread handles the window's events, another thread draws and takes the
 * input, the size and the minimized state from it.
 *
 *     Wotah::Window window;
 *     if (window.Create(size, "Title"))
 *     {
 *         window.Show();
 *         // drawing thread, every frame:
 *         //     if (!window.WaitForDrawableArea(stop)) { stop drawing }
 *         //     window.TakeInputEvents(events);  hand them to the dispatcher
 *         //     window.RequestCursorCaptured(controller.IsTurning());
 *         //     if (window.TakeResize()) { make the swapchain again }
 *         //     draw a frame
 *         while (!window.ShouldClose())
 *         {
 *             window.WaitEvents(); // main thread: sleeps until something happens
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

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <span>
#include <stop_token>
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
     *
     * Threads: Create, Destroy, Show, PollEvents, WaitEvents, SetSize, GetRequiredInstanceExtensions and CreateSurface
     * belong to the main thread (GLFW's rule). Everything else may be called from any thread, so a drawing thread keeps
     * going while the main thread is held up, e.g. by Windows while the user moves or resizes the window.
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

        /**
         * Closes the window and stops GLFW; safe to call more than once or after a failed Create. No other thread may
         * use the window any more.
         */
        void Destroy();

        /** Shows the window. */
        void Show();

        /** Whether the user asked to close the window. Any thread. */
        [[nodiscard]] bool ShouldClose() const;

        /** Handles the waiting window events without waiting for more, then applies RequestCursorCaptured. */
        void PollEvents();

        /**
         * Sleeps until the window has events (or Wake is called), handles them, then applies RequestCursorCaptured.
         * While the user moves or resizes the window, Windows keeps the call inside until the mouse is let go.
         */
        void WaitEvents();

        /** Makes a WaitEvents that is sleeping return. Any thread. */
        void Wake();

        /**
         * Replaces events with the input collected since the last call, in the order it happened. Any thread; the
         * vectors trade places, so no memory is allocated once both have room.
         */
        void TakeInputEvents(std::vector<InputEvent>& events);

        /**
         * Hides the cursor and locks it to the window, so the mouse can move without limits (e.g. to turn a camera),
         * or gives it back. Mouse movement then reports the raw motion where the system offers it. Any thread: the
         * main thread applies it in its next PollEvents or WaitEvents, which this wakes.
         */
        void RequestCursorCaptured(bool isCaptured);

        /**
         * Whether the drawable area changed size since the last call (the user resized the window, or SetSize did):
         * the swapchain must be made again. Answers each change once. Any thread.
         */
        [[nodiscard]] bool TakeResize();

        /** Resizes the window (its inside, in screen units); logged and ignored for a size of 0. */
        void SetSize(const WindowSize& size);

        /**
         * The size of the drawable area in pixels, as of the last handled event; it can differ from the window size on
         * high-DPI screens. Any thread.
         */
        [[nodiscard]] WindowSize GetFramebufferSize() const;

        /**
         * Sleeps while the window has nothing to draw into (minimized, or resized down to nothing), so no frames are
         * drawn for nothing. True once it can be drawn into; false when stop was requested first. Any thread but the
         * main one, which must keep handling events to end the wait.
         */
        [[nodiscard]] bool WaitForDrawableArea(const std::stop_token& stop);

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
        /** GLFW's callbacks; defined in window.cpp, they add input and record size changes. */
        struct Callbacks;
        friend struct Callbacks;

        /** The GLFW window; null before Create and after Destroy. */
        GLFWwindow* m_window = nullptr;

        // ---- Main thread only ----

        /** The cursor at the last mouse movement, for the next movement's Delta; unknown until the first one. */
        glm::vec2 m_lastCursorPosition{0.0f};
        bool      m_hasLastCursorPosition = false;

        /** Whether the cursor is locked right now. */
        bool m_isCursorCaptured = false;

        // ---- Shared between the threads ----

        /** What RequestCursorCaptured last asked for. */
        std::atomic<bool> m_wantsCursorCaptured = false;

        /** Guards everything below. */
        mutable std::mutex m_mutex;

        /** Signalled when the drawable area or the minimized state changes, for WaitForDrawableArea. */
        std::condition_variable_any m_drawableChanged;

        /** The input collected since TakeInputEvents last took it. */
        std::vector<InputEvent> m_inputEvents;

        /** The drawable area's size, kept by the callbacks: GLFW only answers on the main thread. */
        WindowSize m_framebufferSize;

        /** Whether the window is minimized. */
        bool m_isMinimized = false;

        /** Whether the drawable area changed size since TakeResize last answered. */
        bool m_wasResized = false;

        /** Adds one input event; the callbacks call it. */
        void AddInputEvent(const InputEvent& event);

        /** Locks or frees the cursor as RequestCursorCaptured asked; main thread. */
        void ApplyCursorRequest();

        /** Whether there is something to draw into; m_mutex must be held. */
        [[nodiscard]] bool IsDrawable() const noexcept;
    };
} // namespace Wotah
