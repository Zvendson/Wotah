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
#include "ui/user_interface.hpp"

#include <atomic>
#include <cstddef>
#include <memory>
#include <stop_token>
#include <vector>

namespace Wotah
{
    struct ControlBarClicks;
    struct MeshData;

    /**
     * The window and everything Vulkan needs to draw into it.
     * The steps run in order: Create, Run (which calls Start and Stop), Destroy. The destructor runs Stop and Destroy,
     * so nothing leaks when a step fails part way.
     *
     * Two threads: Run handles the window's events on the main thread, and a drawing thread takes the input, moves the
     * camera and draws, so moving or resizing the window never stops the picture.
     */
    class App final
    {
    public:
        App();
        ~App();

        App(const App&)            = delete;
        App& operator=(const App&) = delete;

        /**
         * Starts the log, opens the window (still hidden), starts the UI and creates every Vulkan object; false when
         * one failed.
         */
        [[nodiscard]] bool Create();

        /**
         * Starts, draws frames on a drawing thread until the window is closed, and stops; false (and logged) when
         * Vulkan failed. Call it from the main thread.
         */
        [[nodiscard]] bool Run();

        /**
         * Frees every Vulkan object, the UI and the window, then closes the log; safe to call more than once or after a
         * failed Create.
         */
        void Destroy();

        /**
         * Adds a camera and returns its index for SelectCamera; the active camera stays the same. The camera as given
         * becomes its focus view, which FocusActiveCamera (the F key) goes back to. Like everything below: before Run,
         * or on the drawing thread while it runs.
         */
        std::size_t AddCamera(const Camera& camera);

        /** Draws through the camera at index from the next frame on; false (and logged) for an unknown index. */
        bool SelectCamera(std::size_t index);

        /** The camera frames are drawn through; change it at any time on the drawing thread. There is always one. */
        [[nodiscard]] Camera& GetActiveCamera() noexcept { return m_cameras[m_activeCamera]; }

        /** Puts the active camera back to its focus view: how it was when it was added. */
        void FocusActiveCamera();

        /** The scene's lights (ambient, sun, moon, ...); change them on the drawing thread, shown the next frame. */
        [[nodiscard]] SceneLights& GetLights() noexcept { return m_lights; }

    private:
        /**
         * Starts the physics world: the ground from groundMesh, the turning cube, the ball on the ground and the one in
         * the air; false (and logged) when that fails.
         */
        [[nodiscard]] bool CreatePhysics(const MeshData& groundMesh);

        /** Shows the window; the first thing Run does. */
        void Start();

        /**
         * The drawing thread: draws frames until stop is requested or Vulkan fails, then clears m_isDrawing and wakes
         * the main thread.
         */
        void DrawFrames(const std::stop_token& stop);

        /**
         * One frame: hands out the input, moves the camera by frameSeconds, makes the swapchain again when needed, and
         * draws the scene with the UI over it; false (and logged) when Vulkan failed.
         */
        [[nodiscard]] bool DrawFrame(float frameSeconds);

        /**
         * Puts this frame's UI together (the stats window and the control bar), for a frame of frameSeconds, at the
         * swapchain's size, and does what was clicked in it.
         */
        void ShowUi(float frameSeconds);

        /** Does what was clicked in the control bar: pauses or resumes the world, resets the camera or the scene. */
        void ApplyClicks(const ControlBarClicks& clicks);

        /**
         * Puts the scene back as it started: a new physics world with the ground, the cube at its first turn and both
         * balls where they began. A failure is logged; the scene then lacks what could not be made.
         */
        void ResetScene();

        /**
         * Waits until the GPU has finished the last frame, so nothing is destroyed while in use; the last thing Run
         * does.
         */
        void Stop();

        /** The Vulkan handles; defined in app.cpp so this header stays free of Vulkan. */
        struct State;

        // Order matters: members are destroyed in reverse, so the device goes before the window its surface is on, and
        // the UI's renderer (in m_state) before the UI it is attached to.

        /** The window everything is drawn into. */
        Window m_window;

        /** The Vulkan instance, surface, GPU, device and queue. */
        Device m_device;

        /** The windows drawn over the scene (Dear ImGui); the first to get the input. */
        UserInterface m_ui;

        std::unique_ptr<State> m_state;

        /** Every camera; never empty, so there is always an active one. Create sets up the first. */
        std::vector<Camera> m_cameras{Camera{}};

        /** Each camera's focus view, at the same index as in m_cameras. */
        std::vector<Camera> m_focusViews{Camera{}};

        /** The index of the camera frames are drawn through; always inside m_cameras. */
        std::size_t m_activeCamera = 0;

        /** What lights the scene; Create puts a sun and a moon in. */
        SceneLights m_lights;

        /** Hands the window's input to the handlers: the UI first, then the camera controller. */
        InputDispatcher m_input;

        /** Flies the active camera: W A S D, Space, Shift, the right mouse button to turn, the wheel, and F. */
        CameraController m_cameraController;

        /** The input the drawing thread took from the window this frame; kept, so it does not allocate again. */
        std::vector<InputEvent> m_frameInput;

        /** Whether the world stands still (the control bar's Pause); the camera still flies. Drawing thread only. */
        bool m_isPaused = false;

        /** Whether the drawing thread still draws; it clears this when it ends, for whatever reason. */
        std::atomic<bool> m_isDrawing = false;

        /** Whether the drawing thread stopped because Vulkan failed; read by Run once the thread has ended. */
        bool m_hasDrawingFailed = false;
    };
} // namespace Wotah
