/**
 * @file sky_lights_tests.cpp
 * The sky's lights through a day: the sun's light at its reference height, a setting sun's disk sinking whole while
 * its light fades as it crosses the horizon (nothing snapping off), the moon's disk showing whenever the moon is up
 * (also at sunrise) while its light counts only at night, a rising moon's color, the dark blue night, and the
 * settings' brightness knobs.
 */
#include "render/sky_clock.hpp"
#include "render/sky_lights.hpp"

#include <doctest/doctest.h>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>

namespace
{
    /** How close two numbers must be to count as equal here. */
    constexpr float Tolerance = 1.0e-3f;

    bool IsNear(float first, float second)
    {
        return std::abs(first - second) <= Tolerance;
    }

    /** How strong a light is: its brightest channel times its intensity. */
    float GetStrength(const Wotah::DirectionalLight& light)
    {
        return std::max({light.Color.r, light.Color.g, light.Color.b}) * light.Intensity;
    }

    /** The program's day: the sun 60 degrees up at noon, everything at its own brightness. */
    Wotah::SkyLightSettings MakeDay(float hours)
    {
        Wotah::SkyLightSettings settings;
        settings.TimeOfDayHours = hours;
        settings.NoonElevation  = glm::radians(60.0f);
        return settings;
    }

    /** The hours after noon at which the sun of MakeDay stands at elevation (radians, below 60 degrees). */
    float FindAfternoonHour(float elevation)
    {
        // Bisect the afternoon: the sun only sinks from noon to midnight.
        float early = 12.0f;
        float late  = 24.0f;
        for (int step = 0; step < 40; ++step)
        {
            const float middle = 0.5f * (early + late);
            const float height = Wotah::GetElevation(Wotah::MakeSunDirection(middle, glm::radians(60.0f)));
            (height > elevation ? early : late) = middle;
        }
        return 0.5f * (early + late);
    }
} // namespace

TEST_SUITE("sky lights")
{
    TEST_CASE("the sky's lights go into the scene's: sun and moon in their places, later lights kept")
    {
        const Wotah::SkyLights sky = Wotah::MakeSkyLights(MakeDay(15.0f));

        // An empty scene gets room for both.
        Wotah::SceneLights empty;
        Wotah::PutSkyLights(sky, empty);
        REQUIRE(empty.DirectionalLights.size() == 2);
        CHECK(empty.DirectionalLights[Wotah::SunLightIndex].Direction == sky.Sun.Direction);
        CHECK(empty.DirectionalLights[Wotah::MoonLightIndex].Direction == sky.Moon.Direction);
        CHECK(empty.SkySun.Intensity == sky.SunDisk.Intensity);
        CHECK(empty.SkyMoon.Direction == sky.MoonDisk.Direction);
        CHECK(empty.AmbientColor == sky.AmbientColor);
        CHECK(empty.GroundBounce == sky.GroundBounce);
        CHECK(empty.HazeColor == sky.HazeColor);

        // A third light after them stays as it was, and so do the shadows and the sky's clock.
        Wotah::SceneLights      more;
        Wotah::DirectionalLight lamp;
        lamp.Intensity         = 0.25f;
        more.DirectionalLights = {Wotah::DirectionalLight{}, Wotah::DirectionalLight{}, lamp};
        more.Shadows           = {glm::vec3(1.0f, 2.0f, 3.0f), 40.0f};
        more.SkySeconds        = 12.0f;
        Wotah::PutSkyLights(sky, more);
        REQUIRE(more.DirectionalLights.size() == 3);
        CHECK(more.DirectionalLights[Wotah::SunLightIndex].Intensity == sky.Sun.Intensity);
        CHECK(more.DirectionalLights[2].Intensity == 0.25f);
        CHECK(more.Shadows.Radius == 40.0f);
        CHECK(more.SkySeconds == 12.0f);
    }

    TEST_CASE("the sun 45 degrees up gives its own warm white light, the sky its full light, the day's haze")
    {
        Wotah::SkyLightSettings settings = MakeDay(12.0f);
        settings.NoonElevation           = glm::radians(45.0f);
        settings.SunBrightness           = 0.9f;
        const Wotah::SkyLights sky       = Wotah::MakeSkyLights(settings);

        CHECK(IsNear(sky.Sun.Color.r, 1.0f));
        CHECK(IsNear(sky.Sun.Color.g, 0.95f));
        CHECK(IsNear(sky.Sun.Color.b, 0.85f));
        CHECK(sky.Sun.Intensity == 0.9f);
        CHECK(IsNear(sky.AmbientColor.b, 0.32f));
        CHECK(IsNear(sky.HazeColor.b, 0.94f));

        // By day the moon (below the horizon at noon) neither shines nor shows.
        CHECK(sky.Moon.Intensity == 0.0f);
        CHECK(sky.MoonDisk.Intensity == 0.0f);
    }

    TEST_CASE("a setting sun: its disk sinks behind the ground whole, its light fades as it crosses the horizon")
    {
        const Wotah::SkyLights high      = Wotah::MakeSkyLights(MakeDay(FindAfternoonHour(glm::radians(1.5f))));
        const Wotah::SkyLights onHorizon = Wotah::MakeSkyLights(MakeDay(FindAfternoonHour(0.0f)));
        const Wotah::SkyLights sinking   = Wotah::MakeSkyLights(MakeDay(FindAfternoonHour(glm::radians(-1.5f))));
        const Wotah::SkyLights set       = Wotah::MakeSkyLights(MakeDay(FindAfternoonHour(glm::radians(-6.5f))));

        // The disk the sky draws: whole and bright with its middle on the horizon and 1.5 degrees under it, where the
        // ground's far edge hides it from the start camera; gone 6.5 degrees down.
        CHECK(GetStrength(onHorizon.SunDisk) > 0.3f);
        CHECK(IsNear(GetStrength(sinking.SunDisk), GetStrength(onHorizon.SunDisk)));
        CHECK(GetStrength(set.SunDisk) == 0.0f);

        // Its light on the scene: all of it with the disk above the horizon, half with its middle on it, none from
        // below (the earth is in the way).
        CHECK(IsNear(GetStrength(high.Sun), GetStrength(high.SunDisk)));
        CHECK(IsNear(GetStrength(onHorizon.Sun), 0.5f * GetStrength(onHorizon.SunDisk)));
        CHECK(GetStrength(sinking.Sun) == 0.0f);

        // From 17:00 to 19:00 in steps of under a second of game time both only fall, never by a jump: the biggest
        // drop between two steps stays tiny (a light snapping off would drop by 0.3 or more at once).
        float lastLight   = GetStrength(Wotah::MakeSkyLights(MakeDay(17.0f)).Sun);
        float lastDisk    = GetStrength(Wotah::MakeSkyLights(MakeDay(17.0f)).SunDisk);
        float biggestDrop = 0.0f;
        for (float hours = 17.0f; hours <= 19.0f; hours += 0.0002f)
        {
            const Wotah::SkyLights sky   = Wotah::MakeSkyLights(MakeDay(hours));
            const float            light = GetStrength(sky.Sun);
            const float            disk  = GetStrength(sky.SunDisk);
            CHECK(light <= lastLight + Tolerance);
            CHECK(disk <= lastDisk + Tolerance);
            biggestDrop = std::max({biggestDrop, lastLight - light, lastDisk - disk});
            lastLight   = light;
            lastDisk    = disk;
        }
        CHECK(biggestDrop < 0.01f);
    }

    TEST_CASE("the moon's disk shows whenever the moon is up, at sunrise too; its light on the scene only at night")
    {
        // Midnight: the full moon high in the south, shining and showing.
        const Wotah::SkyLights midnight = Wotah::MakeSkyLights(MakeDay(0.0f));
        CHECK(midnight.Moon.Intensity > 0.3f);
        CHECK(midnight.MoonDisk.Intensity == 1.0f);

        // Sunrise: the sun's middle on the eastern horizon, the moon's on the western one. Day has come and the moon
        // is half down, so it lights nothing any more, but its disk still shows whole: it sets slowly like the sun.
        const Wotah::SkyLights sunrise = Wotah::MakeSkyLights(MakeDay(6.0f));
        CHECK(sunrise.Moon.Intensity < 1.0e-6f); // night share 0, up to float rounding of the sun's place
        CHECK(sunrise.MoonDisk.Intensity == 1.0f);

        // An hour later it has set.
        CHECK(Wotah::MakeSkyLights(MakeDay(7.0f)).MoonDisk.Intensity == 0.0f);
    }

    TEST_CASE("a rising moon is orange-red, high up it is white")
    {
        const glm::vec3 rising = Wotah::MakeSkyLights(MakeDay(18.1f)).MoonDisk.Color;
        CHECK(rising.r > 0.9f);
        CHECK(rising.b < rising.r * 0.5f);

        const glm::vec3 high = Wotah::MakeSkyLights(MakeDay(0.0f)).MoonDisk.Color;
        CHECK(high.r > 0.9f);
        CHECK(high.g > 0.85f);
        CHECK(high.b > 0.8f);
    }

    TEST_CASE("the night is dark and blue: little sky light, a blue moon, a dark haze")
    {
        const Wotah::SkyLights noon     = Wotah::MakeSkyLights(MakeDay(12.0f));
        const Wotah::SkyLights midnight = Wotah::MakeSkyLights(MakeDay(0.0f));
        CHECK(midnight.Sun.Intensity * glm::length(midnight.Sun.Color) == 0.0f);
        CHECK(midnight.AmbientColor.b < noon.AmbientColor.b * 0.3f);
        CHECK(midnight.AmbientColor.b > midnight.AmbientColor.r);
        CHECK(midnight.Moon.Color.b > midnight.Moon.Color.r);
        CHECK(midnight.HazeColor.b < 0.1f);
    }

    TEST_CASE("the brightness settings: 0 switches sun and moon off, the sky light scales")
    {
        Wotah::SkyLightSettings settings = MakeDay(0.0f);
        settings.MoonBrightness          = 0.0f;
        settings.SkyLight                = 2.0f;
        const Wotah::SkyLights dark      = Wotah::MakeSkyLights(settings);
        CHECK(dark.Moon.Intensity == 0.0f);
        CHECK(dark.MoonDisk.Intensity == 0.0f);
        CHECK(IsNear(dark.AmbientColor.b, 2.0f * Wotah::MakeSkyLights(MakeDay(0.0f)).AmbientColor.b));

        settings               = MakeDay(12.0f);
        settings.SunBrightness = 0.0f;
        CHECK(Wotah::MakeSkyLights(settings).Sun.Intensity == 0.0f);
    }
}
