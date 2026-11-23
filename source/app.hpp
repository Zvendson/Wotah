/**
 * @file app.hpp
 * The application: owns the window and the Vulkan objects and drives their life.
 *
 *     Wotah::App app;
 *     if (app.Create())
 *     {
 *         app.Run();
 *     }
 *     app.Destroy();
 */
#pragma once

#include <memory>

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

        App(const App&) = delete;
        App& operator=(const App&) = delete;

        /** Starts the log, opens the window (still hidden) and creates every Vulkan object; false when one failed. */
        [[nodiscard]] bool Create();

        /** Starts, draws frames until the window is closed, and stops. */
        void Run();

        /** Frees every Vulkan object and the window, then closes the log; safe to call more than once or after a failed
         *  Create. */
        void Destroy();

    private:
        /** Shows the window; the first thing Run does. */
        void Start();

        /** Waits until the GPU has finished the last frame, so nothing is destroyed while in use; the last thing Run
         *  does. */
        void Stop();

        /** The window and Vulkan handles; defined in app.cpp so this header stays free of GLFW and Vulkan. */
        struct State;

        std::unique_ptr<State> m_state;
    };
} // namespace Wotah
