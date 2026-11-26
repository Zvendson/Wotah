/**
 * @file sky_clock.hpp
 * The sky's clock: where the sun and the moon stand at a time of day, and how the day runs on. The sun crosses the sky
 * as on an equinox day: it rises in the east at 6, stands highest in the south at 12, sets in the west at 18 and is
 * lowest in the north at midnight. How high it gets at noon is the place's latitude in disguise (90 degrees minus the
 * latitude). The moon is full: it stands where the sun stands 12 hours away, so it rises as the sun sets. Also how
 * much sunset and night the sun's height brings, and how much of a setting sun or moon still shows, as shaders/sky.frag
 * draws them, so the scene's light follows the sky.
 *
 *     hours                     = Wotah::AdvanceTimeOfDay(hours, frameSeconds, dayLengthMinutes);
 *     const glm::vec3 sunlight  = Wotah::MakeSunDirection(hours, noonElevation);
 *     const glm::vec3 moonlight = Wotah::MakeMoonDirection(hours, noonElevation);
 *     const float     night     = Wotah::GetNightShare(Wotah::GetElevation(sunlight));
 *
 * The compass is render/lighting.hpp's: y up, north -z, east +x.
 */
#pragma once

#include <glm/vec3.hpp>

#include <numbers>

namespace Wotah
{
    /** Hours in a day: the clock runs from 0 to this, then starts again. */
    inline constexpr float HoursPerDay = 24.0f;

    /**
     * The radius of the sun's and the moon's disk in the sky, in radians: 1 degree, about four times the real ones'
     * 0.27, so they show well (shaders/sky.frag, SunRadius and MoonRadius, which must match).
     */
    inline constexpr float SkyDiskRadius = std::numbers::pi_v<float> / 180.0f;

    /**
     * Where a setting sun or moon starts to fade and where it is gone (GetDiskShowing), in radians below the horizon:
     * 2 and 6 degrees (shaders/sky.frag, SetStart and SetEnd, which must match). By eye: at a 10-minute day the fade
     * takes about 8 seconds.
     */
    inline constexpr float SkySetStart = 2.0f * std::numbers::pi_v<float> / 180.0f;
    inline constexpr float SkySetEnd   = 6.0f * std::numbers::pi_v<float> / 180.0f;

    /**
     * The direction the sun's light travels at a time of day (hours; any value, it wraps around the day) on a day the
     * sun stands noonElevation radians above the horizon at noon (0 to pi/2), as MakeLightDirection gives it: length 1,
     * pointing up while the sun is below the horizon.
     */
    [[nodiscard]] glm::vec3 MakeSunDirection(float hours, float noonElevation);

    /** The direction the full moon's light travels at a time of day: the sun's path, 12 hours away. */
    [[nodiscard]] glm::vec3 MakeMoonDirection(float hours, float noonElevation);

    /**
     * The time of day seconds later, when a whole day lasts dayLengthMinutes: from 0 up to HoursPerDay, wrapping past
     * midnight. A day length of 0 or less, or a time or length that is not a number, stops the clock.
     */
    [[nodiscard]] float AdvanceTimeOfDay(float hours, float seconds, float dayLengthMinutes);

    /**
     * How high a light stands, in radians above the horizon (below it negative), from the direction its light travels
     * (any length above 0); 0 for a direction without length.
     */
    [[nodiscard]] float GetElevation(const glm::vec3& lightDirection);

    /**
     * How much of a sun or moon at elevation (radians) shows, 0 to 1: all of it until its middle is SkySetStart below
     * the horizon, then fading out smoothly until SkySetEnd below. The ground's far edge lies about 2 to 3 degrees
     * below the horizon seen from the start camera, so a setting disk sinks behind the ground whole and bright, and
     * only then fades, instead of vanishing in the open sky. The disk stays round; nothing in the sky cuts it.
     */
    [[nodiscard]] float GetDiskShowing(float elevation);

    /**
     * How much of a sun's or moon's disk at elevation (radians) stands above the horizon, 0 to 1: all of it a disk's
     * radius up, half with its middle on the horizon, none a radius down (smoothed, standing in for the circle's
     * area). Only that part lights the scene: the earth hides the rest, whether the ground's edge still hides the disk
     * from the camera or not. A light from below the horizon would otherwise shine on upright faces from under the
     * ground, which shadow maps cannot block.
     */
    [[nodiscard]] float GetHorizonShare(float elevation);

    /**
     * How much of a sunset the sky shows for the sun at elevation (radians), 0 to 1: none with the sun 20 degrees up or
     * higher, all of it from the horizon down.
     */
    [[nodiscard]] float GetSunsetShare(float elevation);

    /**
     * How much of the night has come for the sun at elevation (radians), 0 to 1: none with the sun on or above the
     * horizon, all of it from 12 degrees below it on, the end of nautical twilight, when the sky is dark.
     */
    [[nodiscard]] float GetNightShare(float elevation);
} // namespace Wotah
