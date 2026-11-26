/**
 * @file sky_lights.hpp
 * The lights the sky gives at a time of day: the sun's and the full moon's light on the scene, their disks as the sky
 * draws them, the sky's own light from all around, the light the ground throws back up, and the haze far ground fades
 * into. The scene's colors are tuned for the sun 45 degrees up; a lower sun dims and reddens, and the night turns to a
 * faint blue moonlight. The disks the sky draws stay whole while they sink behind the ground, then fade out slowly
 * (render/sky_clock.hpp, GetDiskShowing); their light on the scene fades as they cross the horizon (GetHorizonShare),
 * never all at once.
 *
 *     Wotah::SkyLightSettings settings;
 *     settings.TimeOfDayHours  = 18.5f;
 *     const Wotah::SkyLights sky = Wotah::MakeSkyLights(settings);
 *     lights.DirectionalLights   = {sky.Sun, sky.Moon};
 *     // and sky.SunDisk, sky.MoonDisk to Sky::Draw
 */
#pragma once

#include "render/lighting.hpp"

#include <glm/vec3.hpp>

#include <numbers>

namespace Wotah
{
    /** What the sky's lights are made from. */
    struct SkyLightSettings
    {
        /** The time of day in hours (render/sky_clock.hpp); any value, it wraps around the day. */
        float TimeOfDayHours = 12.0f;

        /** How high the sun stands at noon, in radians above the horizon (0 to pi/2). */
        float NoonElevation = std::numbers::pi_v<float> / 2.0f;

        /** How bright the sun is; multiplies its light. */
        float SunBrightness = 1.0f;

        /** How bright the moon is: multiplies its light and its disk's brightness; 0 hides it. */
        float MoonBrightness = 1.0f;

        /** How much the sky lights every surface from all around, as a share of its own light. */
        float SkyLight = 1.0f;
    };

    /** The lights the sky gives at one time of day. */
    struct SkyLights
    {
        /**
         * The sun's light on the scene: dimmer and redder the lower it stands, fading as its disk crosses the
         * horizon, none from below it.
         */
        DirectionalLight Sun;

        /**
         * The sun as the sky draws it (Sky::Draw): the same light, but whole until the disk has sunk behind the ground,
         * then fading out slowly; Intensity 0 hides it.
         */
        DirectionalLight SunDisk;

        /**
         * The full moon's light on the scene: a faint blue, only as night comes (a day-adapted eye sees none), and
         * like the sun's only from above the horizon.
         */
        DirectionalLight Moon;

        /**
         * The moon as the sky draws it (Sky::Draw): where it stands, its disk's tint (orange-red low over the horizon,
         * white high up) and brightness. Shown whenever it is up, by day too, pale over the blue; Intensity 0 hides it.
         */
        DirectionalLight MoonDisk;

        /** The sky's light reaching every surface from all around. */
        glm::vec3 AmbientColor{0.0f};

        /** The light the ground throws back up (SceneLights::GroundBounce). */
        glm::vec3 GroundBounce{0.0f};

        /** The color far ground fades into (SceneLights::HazeColor): the sky's color at the horizon. */
        glm::vec3 HazeColor{0.0f};
    };

    /** The sky's lights for these settings. */
    [[nodiscard]] SkyLights MakeSkyLights(const SkyLightSettings& settings);
} // namespace Wotah
