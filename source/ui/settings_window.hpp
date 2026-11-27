/**
 * @file settings_window.hpp
 * A window of sliders to change the scene while it runs, in four tabs: the sky (the time of day, whether and how fast
 * the day runs, how high the sun gets, how bright sun, moon and sky are, shadows), the sea (how high its waves are),
 * the physics (gravity, how fast time runs, how bouncy the ball is) and the camera (its field of view). Hidden at the
 * start; Tab shows or hides it (input/scene_shortcuts.hpp).
 *
 *     if (isSettingsOpen && Wotah::ShowSettingsWindow(settings, defaults))
 *     {
 *         // put settings into the scene
 *     }
 */
#pragma once

#include <array>

namespace Wotah
{
    /**
     * The scene's settings, in the units the window shows. The defaults here are placeholders only; the program fills
     * in its scene's own values before the window shows them.
     */
    struct SceneSettings
    {
        /** The time of day in hours, 0 up to 24 (render/sky_clock.hpp): sunrise at 6, noon at 12, sunset at 18. */
        float TimeOfDayHours = 12.0f;

        /** Whether the day runs on by itself; otherwise the sky stands still at TimeOfDayHours. */
        bool IsDayRunning = false;

        /** How long a whole day and night lasts while it runs, in minutes. */
        float DayLengthMinutes = 10.0f;

        /** How high the sun stands at noon, in degrees above the horizon (90 straight overhead). */
        float SunNoonHeightDegrees = 90.0f;

        /** How bright the sun is; multiplies its color. */
        float SunBrightness = 1.0f;

        /** How bright the moon is at night, as a share of the scene's own moonlight. */
        float MoonBrightness = 1.0f;

        /** How much the sky lights every surface from all around, as a share of the scene's own sky light. */
        float SkyLight = 1.0f;

        /** Whether the lights cast shadows. */
        bool HasShadows = true;

        /** Gravity's pull straight down, in metres per second squared. */
        float Gravity = 0.0f;

        /** How fast the world runs, the day included: 1 is real time, 0.5 half as fast. */
        float TimeSpeed = 1.0f;

        /** How much the ball bounces back: 0 not at all, 1 as high as it fell. */
        float BallBounciness = 0.0f;
        /** How high the sea's waves are, trough to crest, in metres (their significant height); 0 is still water. */
        float WaveHeight = 0.0f;

        /** The camera's vertical field of view, in degrees. */
        float FieldOfViewDegrees = 45.0f;
    };

    /** The settings window's header, which is also its name in ImGui. */
    inline constexpr const char* SettingsWindowTitle = "Settings";

    /** Room for a time of day as the window writes it: "hh:mm" and the closing zero. */
    using ClockText = std::array<char, 6>;

    /**
     * A time of day in hours as the window shows it, on a 24-hour clock: "06:30" for 6.5. It wraps past midnight (24
     * is "00:00", -1 "23:00"); a time that is not a number shows as "00:00".
     */
    [[nodiscard]] ClockText MakeClockText(float hours);

    /**
     * Puts the settings window into the UI frame: a card (ui/card.hpp) at the bottom left with the settings in tabs, a
     * slider or a check box each, and a button that puts every setting back to defaults. True when a setting changed
     * this frame, so the caller puts them into the scene.
     */
    [[nodiscard]] bool ShowSettingsWindow(SceneSettings& settings, const SceneSettings& defaults);
} // namespace Wotah
