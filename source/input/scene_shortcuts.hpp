/**
 * @file scene_shortcuts.hpp
 * Keys that act on the scene rather than the camera: R resets the scene, Tab shows or hides its settings. Added after
 * the UI, so these keys typed into a UI text field never do anything else.
 *
 *     Wotah::SceneShortcuts shortcuts;
 *     dispatcher.AddHandler(shortcuts);
 *     // every frame, after the input events were dispatched:
 *     if (shortcuts.TakeResetRequest())
 *     {
 *         // put the scene back as it started
 *     }
 *     if (shortcuts.TakeSettingsRequest())
 *     {
 *         // show the settings window, or hide it
 *     }
 */
#pragma once

#include "input/input_handler.hpp"

namespace Wotah
{
    /** Turns the scene's keys into requests the program takes once per frame. */
    class SceneShortcuts final : public InputHandler
    {
    public:
        /**
         * Uses R and Tab; holding one asks once, as the system's repeats are not new presses. Other input goes on to
         * the handlers after it.
         */
        bool OnInput(const InputEvent& event) override;

        /** Whether R was pressed since the last call; the caller resets the scene. */
        [[nodiscard]] bool TakeResetRequest() noexcept;

        /** Whether Tab was pressed since the last call; the caller shows the settings window, or hides it. */
        [[nodiscard]] bool TakeSettingsRequest() noexcept;

    private:
        /** Whether R was pressed and TakeResetRequest has not answered it yet. */
        bool m_isResetRequested = false;

        /** Whether Tab was pressed and TakeSettingsRequest has not answered it yet. */
        bool m_isSettingsRequested = false;
    };
} // namespace Wotah
