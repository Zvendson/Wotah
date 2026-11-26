/**
 * @file scene_shortcuts.hpp
 * Keys that act on the scene rather than the camera: R resets the scene. Added after the UI, so an R typed into a UI
 * text field never resets anything.
 *
 *     Wotah::SceneShortcuts shortcuts;
 *     dispatcher.AddHandler(shortcuts);
 *     // every frame, after the input events were dispatched:
 *     if (shortcuts.TakeResetRequest())
 *     {
 *         // put the scene back as it started
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
        /** Uses R; holding it asks once, as the system's repeats are not new presses. Other input goes on. */
        bool OnInput(const InputEvent& event) override;

        /** Whether R was pressed since the last call; the caller resets the scene. */
        [[nodiscard]] bool TakeResetRequest() noexcept;

    private:
        /** Whether R was pressed and TakeResetRequest has not answered it yet. */
        bool m_isResetRequested = false;
    };
} // namespace Wotah
