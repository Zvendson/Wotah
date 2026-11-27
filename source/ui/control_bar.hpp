/**
 * @file control_bar.hpp
 * A bar of buttons at the bottom middle of the window: Add ball, Reset scene, Pause (Resume while paused) and Reset
 * camera. It only tells what was clicked; the program does it. Shown in every UI frame, between
 * UserInterface::BeginFrame and EndFrame.
 *
 *     const Wotah::ControlBarClicks clicks = Wotah::ShowControlBar(isPaused, canAddBall);
 *     if (clicks.TogglePause)
 *     {
 *         isPaused = !isPaused;
 *     }
 */
#pragma once

namespace Wotah
{
    /** The control bar's name in ImGui; a card shows no title. */
    inline constexpr const char* ControlBarName = "Control bar";

    /** What was clicked in the control bar this frame. */
    struct ControlBarClicks
    {
        /** Drop one more ball where the middle of the view points. */
        bool AddBall = false;

        /** Put the scene back as it started: one ball, where it started. */
        bool ResetScene = false;

        /** Stop the world, or let it run on again (the button says Resume while paused). */
        bool TogglePause = false;

        /** Put the camera back to its focus view, as the F key does. */
        bool ResetCamera = false;
    };

    /**
     * Puts the control bar into the UI frame: a card (ui/card.hpp) at the bottom middle with its buttons in a row.
     * isPaused picks the pause button's label, and shows it in the accent while paused; the button keeps its width
     * either way, so the bar does not jump. Without canAddBall (the scene is full) Add ball is greyed out and does
     * nothing.
     */
    [[nodiscard]] ControlBarClicks ShowControlBar(bool isPaused, bool canAddBall);
} // namespace Wotah
