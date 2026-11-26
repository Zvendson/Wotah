/**
 * @file hints_window.hpp
 * A small window over the scene that lists the controls: which keys and mouse buttons fly the camera, reset the scene
 * and open the settings. Shown in every UI frame, between UserInterface::BeginFrame and EndFrame.
 *
 *     Wotah::ShowHintsWindow();
 */
#pragma once

namespace Wotah
{
    /** The hints window's header, which is also its name in ImGui. */
    inline constexpr const char* HintsWindowTitle = "Controls";

    /**
     * Puts the hints window into the UI frame: a card (ui/card.hpp) fixed at the top right, a row per control, each
     * key drawn as a small key cap beside what it does.
     */
    void ShowHintsWindow();
} // namespace Wotah
