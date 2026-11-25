/**
 * @file control_bar.hpp
 * A bar of buttons at the bottom middle of the window: Reset scene, Pause (Resume while paused) and Reset camera. It
 * only tells what was clicked; the program does it. Shown in every UI frame, between UserInterface::BeginFrame and
 * EndFrame.
 *
 *     const Wotah::ControlBarClicks clicks = Wotah::ShowControlBar(isPaused);
 *     if (clicks.TogglePause)
 *     {
 *         isPaused = !isPaused;
 *     }
 */
#pragma once

namespace Wotah
{
    /** The control bar's name in ImGui; a card shows no title. */
    inline constexpr const char* ControlBarName = "Controls";

    /** What was clicked in the control bar this frame. */
    struct ControlBarClicks
    {
        /** Put the scene back as it started: the cube's turn, the balls' places. */
        bool ResetScene = false;

        /** Stop the world, or let it run on again (the button says Resume while paused). */
        bool TogglePause = false;

        /** Put the camera back to its focus view, as the F key does. */
        bool ResetCamera = false;
    };

    /**
     * Puts the control bar into the UI frame: a card (ui/card.hpp) at the bottom middle with its buttons in a row.
     * isPaused picks the middle button's label, and shows it in the accent while paused; the button keeps its width
     * either way, so the bar does not jump.
     */
    [[nodiscard]] ControlBarClicks ShowControlBar(bool isPaused);
} // namespace Wotah
