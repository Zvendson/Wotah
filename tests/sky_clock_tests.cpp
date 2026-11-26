/**
 * @file sky_clock_tests.cpp
 * The sky's clock: where sun and moon stand through an equinox day, how the time of day runs on and wraps, and the
 * sky's shares of a setting disk, sunset and night.
 */
#include "render/lighting.hpp"
#include "render/sky_clock.hpp"

#include <doctest/doctest.h>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

#include <cmath>
#include <limits>

namespace
{
    /** How close two numbers must be to count as equal here. */
    constexpr float Tolerance = 1.0e-4f;

    bool IsNear(float first, float second)
    {
        return std::abs(first - second) <= Tolerance;
    }

    bool IsNear(const glm::vec3& first, const glm::vec3& second)
    {
        return glm::length(first - second) <= Tolerance;
    }

    /** A day whose sun stands 60 degrees up at noon, as the program's. */
    constexpr float NoonElevation = glm::radians(60.0f);
} // namespace

TEST_SUITE("sky clock")
{
    TEST_CASE("the sun on an equinox day: rises due east at 6, highest due south at noon, sets due west at 18")
    {
        // The direction its light travels: from the east it shines west, from the south north, and so on.
        CHECK(IsNear(Wotah::MakeSunDirection(6.0f, NoonElevation), {-1.0f, 0.0f, 0.0f}));
        CHECK(IsNear(Wotah::MakeSunDirection(12.0f, NoonElevation),
                     Wotah::MakeLightDirection(glm::radians(180.0f), NoonElevation)));
        CHECK(IsNear(Wotah::GetElevation(Wotah::MakeSunDirection(12.0f, NoonElevation)), NoonElevation));
        CHECK(IsNear(Wotah::MakeSunDirection(18.0f, NoonElevation), {1.0f, 0.0f, 0.0f}));

        // At midnight it is as far below the horizon, in the north; its light comes up from below.
        const glm::vec3 midnight = Wotah::MakeSunDirection(0.0f, NoonElevation);
        CHECK(IsNear(Wotah::GetElevation(midnight), -NoonElevation));
        CHECK(midnight.z > 0.0f); // shining from the north (-z) towards the south

        // Morning in the east, afternoon in the west, always length 1; the day wraps (24 + 9 is 9).
        CHECK(Wotah::MakeSunDirection(9.0f, NoonElevation).x < 0.0f);
        CHECK(Wotah::MakeSunDirection(15.0f, NoonElevation).x > 0.0f);
        CHECK(IsNear(glm::length(Wotah::MakeSunDirection(9.7f, NoonElevation)), 1.0f));
        CHECK(IsNear(Wotah::MakeSunDirection(33.0f, NoonElevation), Wotah::MakeSunDirection(9.0f, NoonElevation)));
    }

    TEST_CASE("the full moon stands where the sun stands 12 hours later: it rises in the east as the sun sets")
    {
        CHECK(IsNear(Wotah::MakeMoonDirection(18.0f, NoonElevation), {-1.0f, 0.0f, 0.0f}));
        CHECK(IsNear(Wotah::GetElevation(Wotah::MakeMoonDirection(0.0f, NoonElevation)), NoonElevation));
        CHECK(IsNear(Wotah::MakeMoonDirection(7.5f, NoonElevation), Wotah::MakeSunDirection(19.5f, NoonElevation)));
    }

    TEST_CASE("the time of day runs a whole day in the day's length, wraps past midnight, and stops on a bad length")
    {
        // A 10-minute day: 24 hours in 600 seconds, so 25 seconds are an hour.
        CHECK(IsNear(Wotah::AdvanceTimeOfDay(9.0f, 25.0f, 10.0f), 10.0f));
        CHECK(IsNear(Wotah::AdvanceTimeOfDay(23.5f, 25.0f, 10.0f), 0.5f));
        CHECK(IsNear(Wotah::AdvanceTimeOfDay(-1.0f, 0.0f, 10.0f), 23.0f));

        // No length, a negative one, or no number: the clock stands still, still wrapped.
        constexpr float NoNumber = std::numeric_limits<float>::quiet_NaN();
        CHECK(IsNear(Wotah::AdvanceTimeOfDay(9.0f, 25.0f, 0.0f), 9.0f));
        CHECK(IsNear(Wotah::AdvanceTimeOfDay(9.0f, 25.0f, -5.0f), 9.0f));
        CHECK(IsNear(Wotah::AdvanceTimeOfDay(9.0f, 25.0f, NoNumber), 9.0f));
        CHECK(IsNear(Wotah::AdvanceTimeOfDay(9.0f, NoNumber, 10.0f), 9.0f));
        CHECK(IsNear(Wotah::AdvanceTimeOfDay(30.0f, 0.0f, 0.0f), 6.0f));

        // A time that is not a number starts the day again rather than spreading.
        CHECK(Wotah::AdvanceTimeOfDay(NoNumber, 1.0f, 10.0f) == 0.0f);
    }

    TEST_CASE("a light's elevation from its direction: up for a light above, 0 for no direction")
    {
        CHECK(IsNear(Wotah::GetElevation(Wotah::MakeLightDirection(1.0f, 0.3f)), 0.3f));
        CHECK(IsNear(Wotah::GetElevation(Wotah::MakeLightDirection(2.0f, -0.4f) * 5.0f), -0.4f));
        CHECK(Wotah::GetElevation(glm::vec3(0.0f)) == 0.0f);
        CHECK(Wotah::GetElevation(glm::vec3(std::numeric_limits<float>::quiet_NaN())) == 0.0f);
    }

    TEST_CASE("a setting sun or moon shows whole until it is 2 degrees down, behind the ground, then fades out by 6")
    {
        CHECK(Wotah::GetDiskShowing(glm::radians(30.0f)) == 1.0f);
        CHECK(Wotah::GetDiskShowing(0.0f) == 1.0f);
        CHECK(Wotah::GetDiskShowing(-Wotah::SkySetStart) == 1.0f);
        CHECK(Wotah::GetDiskShowing(glm::radians(-4.0f)) > 0.3f);
        CHECK(Wotah::GetDiskShowing(glm::radians(-4.0f)) < 0.7f);
        CHECK(Wotah::GetDiskShowing(-Wotah::SkySetEnd) == 0.0f);
        CHECK(Wotah::GetDiskShowing(glm::radians(-10.0f)) == 0.0f);

        // In between it only ever falls, and never by much from one hundredth of a degree to the next: no jump that
        // would show as the disk or its light snapping off.
        float last = 1.0f;
        for (float degrees = 0.0f; degrees >= -7.0f; degrees -= 0.01f)
        {
            const float showing = Wotah::GetDiskShowing(glm::radians(degrees));
            CHECK(showing <= last);
            CHECK(last - showing < 0.005f);
            last = showing;
        }
    }

    TEST_CASE("the share of a disk above the horizon: all a radius up, half on it, none a radius down")
    {
        CHECK(Wotah::GetHorizonShare(Wotah::SkyDiskRadius) == 1.0f);
        CHECK(Wotah::GetHorizonShare(glm::radians(20.0f)) == 1.0f);
        CHECK(IsNear(Wotah::GetHorizonShare(0.0f), 0.5f));
        CHECK(Wotah::GetHorizonShare(-Wotah::SkyDiskRadius) == 0.0f);
        CHECK(Wotah::GetHorizonShare(glm::radians(-20.0f)) == 0.0f);
    }

    TEST_CASE("sunset and night: a sunset below 20 degrees of sun, night from 12 degrees under the horizon")
    {
        CHECK(Wotah::GetSunsetShare(glm::radians(25.0f)) == 0.0f);
        CHECK(Wotah::GetSunsetShare(glm::radians(10.0f)) > 0.0f);
        CHECK(Wotah::GetSunsetShare(glm::radians(10.0f)) < 1.0f);
        CHECK(Wotah::GetSunsetShare(0.0f) == 1.0f);
        CHECK(Wotah::GetSunsetShare(glm::radians(-30.0f)) == 1.0f);

        CHECK(Wotah::GetNightShare(glm::radians(10.0f)) == 0.0f);
        CHECK(Wotah::GetNightShare(0.0f) == 0.0f);
        CHECK(Wotah::GetNightShare(glm::radians(-6.0f)) > 0.3f);
        CHECK(Wotah::GetNightShare(glm::radians(-6.0f)) < 0.7f);
        CHECK(Wotah::GetNightShare(glm::radians(-13.0f)) == 1.0f);
        CHECK(Wotah::GetNightShare(glm::radians(-60.0f)) == 1.0f);
    }
}
