/**
 * @file app.hpp
 * The application: owns the window and the Vulkan objects and drives their life.
 *
 *     Wotah::App app;
 *     const bool ranCleanly = app.Create() && app.Run();
 *     app.Destroy();
 */
#pragma once

#include "input/camera_controller.hpp"
#include "input/input_dispatcher.hpp"
#include "platform/window.hpp"
#include "render/camera.hpp"
#include "render/device.hpp"
#include "render/lighting.hpp"

#include <cstddef>
#include <memory>
#include <vector>

namespace Wotah
{
    /**
     * The window and everything Vulkan needs to draw into it.
     * The steps run in order: Create, Run (which calls Start and Stop), Destroy. The destructor runs Stop and Destroy,
     * so nothing leaks when a step fails part way.
     */
    class App final
    {
    public:
        App();
        ~App();

        App(const App&)            = delete;
        App& operator=(const App&) = delete;

        /** Starts the log, opens the window (still hidden) and creates every Vulkan object; false when one failed. */
        [[nodiscard]] bool Create();

        /** Starts, draws frames until the window is closed, and stops; false (and logged) when Vulkan failed. */
        [[nodiscard]] bool Run();

        /**
         * Frees every Vulkan object and the window, then closes the log; safe to call more than once or after a failed
         * Create.
         */
        void Destroy();

        /**
         * Adds a camera and returns its index for SelectCamera; the active camera stays the same. The camera as given
         * becomes its focus view, which FocusActiveCamera (the F key) goes back to.
         */
        std::size_t AddCamera(const Camera& camera);

        /** Draws through the camera at index from the next frame on; false (and logged) for an unknown index. */
        bool SelectCamera(std::size_t index);

        /** The camera frames are drawn through; change it at any time. There is always one. */
        [[nodiscard]] Camera& GetActiveCamera() noexcept { return m_cameras[m_activeCamera]; }

        /** Puts the active camera back to its focus view: how it was when it was added. */
        void FocusActiveCamera();

        /** The scene's lights (ambient, sun, moon, ...); change them at any time, the next frame shows them. */
        [[nodiscard]] SceneLights& GetLights() noexcept { return m_lights; }

    private:
        /** Shows the window; the first thing Run does. */
        void Start();

        /**
         * Waits until the GPU has finished the last frame, so nothing is destroyed while in use; the last thing Run
         * does.
         */
        void Stop();

        /** The Vulkan handles; defined in app.cpp so this header stays free of Vulkan. */
        struct State;

        // Order matters: members are destroyed in reverse, so the device goes before the window its surface is on.

        /** The window everything is drawn into. */
        Window m_window;

        /** The Vulkan instance, surface, GPU, device and queue. */
        Device m_device;

        std::unique_ptr<State> m_state;

        /** Every camera; never empty, so there is always an active one. Create sets up the first. */
        std::vector<Camera> m_cameras{Camera{}};

        /** Each camera's focus view, at the same index as in m_cameras. */
        std::vector<Camera> m_focusViews{Camera{}};

        /** The index of the camera frames are drawn through; always inside m_cameras. */
        std::size_t m_activeCamera = 0;

        /** What lights the scene; Create puts a sun and a moon in. */
        SceneLights m_lights;

        /** Hands the window's input to the handlers, the UI first once there is one. */
        InputDispatcher m_input;

        /** Flies the active camera: W A S D, Space, Shift, the right mouse button to turn, the wheel, and F. */
        CameraController m_cameraController;
    };
} // namespace Wotah
