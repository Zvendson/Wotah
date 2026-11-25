/**
 * @file user_interface.hpp
 * The windows and widgets drawn over the scene, made with Dear ImGui: owns ImGui's context with the program's theme,
 * hands it the window's input, and runs its frames. As the first input handler it keeps the clicks, wheel turns and
 * typing that land on a UI window, so they never also move the camera. Drawing the result is render/ui_renderer's job.
 *
 *     Wotah::UserInterface ui;
 *     if (ui.Create())
 *     {
 *         dispatcher.AddHandler(ui); // before the camera controller
 *         // drawing thread, every frame, after the input was dispatched:
 *         ui.BeginFrame(window.GetSize(), pictureSize, frameSeconds);
 *         Wotah::ShowStatsWindow(stats); // the UI's windows, between BeginFrame and EndFrame
 *         ui.EndFrame();                 // then UiRenderer::Draw records it into the frame
 *     }
 *     ui.Destroy(); // after the UiRenderer
 *
 * ImGui has one context per program here, so there is one UserInterface at a time. ImGui stays inside the .cpp files;
 * this header only names its context type.
 */
#pragma once

#include "input/input_handler.hpp"

#include <cstdint>
#include <memory>

struct ImGuiContext;

namespace Wotah
{
    struct WindowSize;
    enum class KeyCode : int32_t;

    /**
     * ImGui's context and the bridge from the window's input to it. Create, then every frame BeginFrame, the windows,
     * EndFrame; Destroy at the end. The destructor runs Destroy. Like the input it handles, everything but Create and
     * Destroy runs on the drawing thread.
     */
    class UserInterface final : public InputHandler
    {
    public:
        UserInterface() = default;
        ~UserInterface() override;

        UserInterface(const UserInterface&)            = delete;
        UserInterface& operator=(const UserInterface&) = delete;

        /**
         * Starts ImGui with the theme (ui/theme.hpp) and its scalable font, saving nothing to disk (no imgui.ini, no
         * log file); false (and logged) when ImGui is already running.
         */
        [[nodiscard]] bool Create();

        /**
         * Stops ImGui; safe to call more than once or after a failed Create. The UiRenderer must be destroyed first, as
         * ImGui keeps it inside its context: while it runs, ImGui keeps running too (logged).
         */
        void Destroy();

        /**
         * Hands the event to ImGui. True when the UI keeps it: mouse events while the mouse is over a UI window (or a
         * drag started on one), key and text events while a UI field has the keyboard. As ImGui knows that only at the
         * end of a frame, the answer comes from the last frame. False before Create.
         */
        bool OnInput(const InputEvent& event) override;

        /**
         * Starts a UI frame that lasts seconds. windowSize is the window's inside in the cursor's units
         * (Window::GetSize), pictureSize the image drawn into in pixels; on scaled screens they differ, and the UI
         * is drawn at the picture's resolution. Before Create it does nothing.
         */
        void BeginFrame(const WindowSize& windowSize, const WindowSize& pictureSize, float seconds);

        /** Ends the UI frame and makes the shapes UiRenderer draws. Before Create it does nothing. */
        void EndFrame();

    private:
        /** Frees an ImGui context; defined in user_interface.cpp, where ImGui is known. */
        struct ContextDeleter
        {
            void operator()(ImGuiContext* context) const noexcept;
        };

        /** The modifier keys held right now; ImGui wants each modifier as one key, either side. */
        struct HeldModifiers
        {
            bool LeftControl  = false;
            bool RightControl = false;
            bool LeftShift    = false;
            bool RightShift   = false;
            bool LeftAlt      = false;
            bool RightAlt     = false;
            bool LeftSuper    = false;
            bool RightSuper   = false;
        };

        /** Hands a key going down or up to ImGui, with the modifiers held at that moment. */
        void SendKey(const InputEvent& event);

        /** Notes a modifier key going down or up; other keys are ignored. */
        void SetModifierHeld(KeyCode key, bool isHeld) noexcept;

        /** ImGui's context; null before Create and after Destroy. */
        std::unique_ptr<ImGuiContext, ContextDeleter> m_context;

        HeldModifiers m_modifiers;
    };
} // namespace Wotah
