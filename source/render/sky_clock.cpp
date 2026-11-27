/**
 * @file sky_clock.cpp
 * Where sun and moon stand, from the hour angle (how far the earth has turned since noon) on an equinox day, and the
 * sky's shares of sunset and night (shaders/sky.frag computes the same from the sun's height).
 */
#include "render/sky_clock.hpp"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace
{
    /** The hour of noon, when the sun crosses the south. */
    constexpr float Noon = 12.0f;

    /** How far the full moon stands from the sun on the clock: half a day. */
    constexpr float MoonDelay = Wotah::HoursPerDay / 2.0f;

    /** Seconds in a minute. */
    constexpr float SecondsPerMinute = 60.0f;

    /** Below this squared length a direction counts as none. */
    constexpr float NearlyZeroSquared = 1.0e-12f;

    /**
     * The sun's heights the sky's sunset and night are measured by, as the sine of its elevation (the y of the
     * direction towards it), as shaders/sky.frag has them: SunsetHeight sin(20 degrees), NightDepth sin(12 degrees).
     */
    constexpr float SunsetHeight = 0.34f;
    constexpr float NightDepth   = 0.21f;

    /** The time of day wrapped into 0 up to HoursPerDay. */
    float WrapHours(float hours)
    {
        const float wrapped = std::fmod(hours, Wotah::HoursPerDay);
        return wrapped < 0.0f ? wrapped + Wotah::HoursPerDay : wrapped;
    }
} // namespace

glm::vec3 Wotah::MakeSunDirection(float hours, float noonElevation)
{
    // The hour angle: how far the sky has turned since noon, a full turn a day (negative in the morning).
    const float hourAngle = (hours - Noon) / Noon * std::numbers::pi_v<float>;

    // On an equinox day the sun runs along the sky's equator, which meets the horizon due east and due west and stands
    // noonElevation high in the south: towards the sun = cos(H) * (equator's highest point) - sin(H) * east, with the
    // highest point (0, sin e, cos e) for south +z and up +y (any spherical astronomy text, "hour angle").
    const glm::vec3 towardsSun{-std::sin(hourAngle), std::cos(hourAngle) * std::sin(noonElevation),
                               std::cos(hourAngle) * std::cos(noonElevation)};
    return -towardsSun;
}

glm::vec3 Wotah::MakeMoonDirection(float hours, float noonElevation)
{
    return MakeSunDirection(hours + MoonDelay, noonElevation);
}

float Wotah::GetElevation(const glm::vec3& lightDirection)
{
    if (!(glm::dot(lightDirection, lightDirection) >= NearlyZeroSquared))
    {
        return 0.0f;
    }
    const float towardsUp = -glm::normalize(lightDirection).y; // the light travels down from a light above
    return std::asin(std::clamp(towardsUp, -1.0f, 1.0f));
}

float Wotah::GetDiskShowing(float elevation)
{
    return glm::smoothstep(-SkySetEnd, -SkySetStart, elevation);
}

float Wotah::GetHorizonShare(float elevation)
{
    return glm::smoothstep(-SkyDiskRadius, SkyDiskRadius, elevation);
}

float Wotah::GetSunsetShare(float elevation)
{
    return 1.0f - glm::smoothstep(0.0f, SunsetHeight, std::sin(elevation));
}

float Wotah::GetNightShare(float elevation)
{
    return glm::smoothstep(0.0f, NightDepth, -std::sin(elevation));
}

float Wotah::AdvanceTimeOfDay(float hours, float seconds, float dayLengthMinutes)
{
    if (!std::isfinite(hours))
    {
        return 0.0f; // a time that is not a number starts the day again rather than spreading
    }
    if (!(dayLengthMinutes > 0.0f) || !std::isfinite(dayLengthMinutes) || !std::isfinite(seconds))
    {
        return WrapHours(hours);
    }
    // A whole day of HoursPerDay passes in dayLengthMinutes: that many clock hours per real second.
    const float hoursPerSecond = HoursPerDay / (dayLengthMinutes * SecondsPerMinute);
    return WrapHours(hours + seconds * hoursPerSecond);
}
