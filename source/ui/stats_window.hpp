/**
 * @file stats_window.hpp
 * A small window over the scene that shows how the program runs: the frame rate and frame time, the picture's size and
 * where the camera stands. Shown in every UI frame, between UserInterface::BeginFrame and EndFrame.
 *
 *     Wotah::FrameStats stats;
 *     stats.PictureWidth   = extent.width;
 *     stats.PictureHeight  = extent.height;
 *     stats.CameraPosition = camera.GetPosition();
 *     Wotah::ShowStatsWindow(stats);
 */
#pragma once

#include "ui/card.hpp"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>

namespace Wotah
{
    /** What the stats window shows besides the frame rate, which ImGui measures (an average of the last 60 frames). */
    struct FrameStats
    {
        /** The size of the picture drawn, in pixels. */
        uint32_t PictureWidth  = 0;
        uint32_t PictureHeight = 0;

        /** Where the active camera stands, in world units (metres). */
        glm::vec3 CameraPosition{0.0f};
    };

    /** The stats window's header, which is also its name in ImGui. */
    inline constexpr const char* StatsWindowTitle = "Stats";

    /** Where the stats window stays: its top-left corner, in the cursor's units, a card's margin off the corner. */
    inline constexpr glm::vec2 StatsWindowPosition{CardMargin, CardMargin};

    /**
     * Puts the stats window into the UI frame: a card (ui/card.hpp) fixed at the top left, always as large as its text,
     * with its header inside. It has no title bar and cannot be folded, moved or resized, and it never takes the
     * keyboard from another window.
     */
    void ShowStatsWindow(const FrameStats& stats);
} // namespace Wotah
