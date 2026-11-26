/**
 * @file settings_window.hpp
 * A window of sliders to change the scene while it runs: where the sun stands and how bright it is, the sky's light,
 * shadows, gravity, how fast time runs, how bouncy the balls are, how fast the cube turns, and the camera's field of
 * view. Hidden at the start; Tab shows or hides it (input/scene_shortcuts.hpp).
 *
 *     if (isSettingsOpen && Wotah::ShowSettingsWindow(settings, defaults))
 *     {
 *         // put settings into the scene
 *     }
 */
#pragma once

namespace Wotah
{
    /**
     * The scene's settings, in the units the window shows. The defaults here are placeholders only; the program fills
     * in its scene's own values before the window shows them.
     */
    struct SceneSettings
    {
        /** Where the sun stands: compass degrees (0 north, 90 east, 180 south, 270 west). */
        float SunAzimuthDegrees = 0.0f;

        /** How high the sun stands, in degrees above the horizon (90 straight overhead). */
        float SunElevationDegrees = 90.0f;

        /** How bright the sun is; multiplies its color. */
        float SunBrightness = 1.0f;

        /** How much the sky lights every surface from all around, as a share of the scene's own sky light. */
        float SkyLight = 1.0f;

        /** Whether the lights cast shadows. */
        bool HasShadows = true;

        /** Gravity's pull straight down, in metres per second squared. */
        float Gravity = 0.0f;

        /** How fast the world runs: 1 is real time, 0.5 half as fast. */
        float TimeSpeed = 1.0f;

        /** How much the balls bounce back: 0 not at all, 1 as high as they fell. */
        float BallBounciness = 0.0f;

        /** How fast the cube turns, in degrees per second; positive turns counterclockwise, seen from above. */
        float CubeTurnDegreesPerSecond = 0.0f;

        /** The camera's vertical field of view, in degrees. */
        float FieldOfViewDegrees = 45.0f;
    };

    /** The settings window's header, which is also its name in ImGui. */
    inline constexpr const char* SettingsWindowTitle = "Settings";

    /**
     * Puts the settings window into the UI frame: a card (ui/card.hpp) at the bottom left with the settings in groups,
     * a slider or a check box each, and a button that puts every setting back to defaults. True when a setting
     * changed this frame, so the caller puts them into the scene.
     */
    [[nodiscard]] bool ShowSettingsWindow(SceneSettings& settings, const SceneSettings& defaults);
} // namespace Wotah
