/**
 * @file terrain_shape_tests.cpp
 * The land's shape: the waterline through the origin for every seed, land rising to the east and the sea floor
 * falling to the west, so the sun sets over the sea, dunes and hills inland, smooth everywhere, less detail asked for
 * changing it only a little, flat shapes flat, shapes checked before use, and patches sampling the same heights in the
 * documented order.
 */
#include "render/sky_clock.hpp"
#include "terrain/terrain_shape.hpp"

#include <doctest/doctest.h>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace
{
    /** The highest and lowest height along a line of places in the coast's direction (z), at a given x. */
    struct Range
    {
        float Lowest  = std::numeric_limits<float>::max();
        float Highest = std::numeric_limits<float>::lowest();
        float Sum     = 0.0f;
    };

    /** The heights along z from -400 to 400 (every 10 units) at a fixed x. */
    Range MeasureAlongCoast(const Wotah::TerrainShape& shape, float x)
    {
        Range range;
        for (float z = -400.0f; z <= 400.0f; z += 10.0f)
        {
            const float height = Wotah::GetTerrainHeight(shape, {x, z});
            range.Lowest       = std::min(range.Lowest, height);
            range.Highest      = std::max(range.Highest, height);
            range.Sum += height;
        }
        return range;
    }
} // namespace

TEST_SUITE("terrain shape")
{
    TEST_CASE("the beach's waterline passes through the origin for every seed, at the sea level")
    {
        for (uint32_t seed = 0; seed < 8; ++seed)
        {
            Wotah::TerrainShape shape;
            shape.Seed     = seed;
            shape.SeaLevel = 1.5f;
            CAPTURE(seed);
            CHECK(std::abs(Wotah::GetTerrainHeight(shape, {0.0f, 0.0f}) - 1.5f) < 1.0e-5f);
        }
    }

    TEST_CASE("land rises to the east into dunes and hills, the sea floor falls to the west to the sea's depth")
    {
        const Wotah::TerrainShape beach;

        // Right at the origin the beach rises eastwards (+x) about as steeply as BeachSlope.
        const float east = Wotah::GetTerrainHeight(beach, {2.0f, 0.0f});
        const float west = Wotah::GetTerrainHeight(beach, {-2.0f, 0.0f});
        CHECK(east > west);
        CHECK(std::abs((east - west) / 4.0f - beach.BeachSlope) < 0.5f * beach.BeachSlope);

        // Far out to sea the floor lies near SeaDepth below the sea level, all along the coast.
        const Range deep = MeasureAlongCoast(beach, -600.0f);
        CHECK(deep.Highest < beach.SeaLevel - 0.7f * beach.SeaDepth);
        CHECK(deep.Lowest > beach.SeaLevel - beach.SeaDepth - 2.0f * beach.BumpHeight);

        // Inland, past the dunes and into the hills, all of it stands above the beach's top, and the hills rise well
        // above the dunes somewhere.
        const Range hills = MeasureAlongCoast(beach, 500.0f);
        CHECK(hills.Lowest > beach.SeaLevel + beach.BeachTop - 2.0f * beach.BumpHeight);
        CHECK(hills.Highest > beach.SeaLevel + beach.BeachTop + 0.5f * beach.HillHeight);

        // Between them, in the dunes, some places stand above the beach's top.
        const Range dunes = MeasureAlongCoast(beach, beach.DuneStart + beach.DuneRamp + 10.0f);
        CHECK(dunes.Highest > beach.SeaLevel + beach.BeachTop + 0.5f * beach.DuneHeight);
    }

    TEST_CASE("the sun sets over the sea: towards the sunset, from anywhere along the beach, water to the horizon")
    {
        const Wotah::TerrainShape beach;

        // Where the sun touches the horizon in the evening (the sky clock's 18:00, an equinox day: due west whatever
        // the latitude), as a direction over the ground.
        constexpr float SunsetHours = 18.0f;
        const glm::vec3 towardsSun  = -Wotah::MakeSunDirection(SunsetHours, glm::radians(60.0f));
        const glm::vec2 sunset      = glm::normalize(glm::vec2(towardsSun.x, towardsSun.z));

        // From spots on the coast's middle line every 50 units over a kilometre, past the surf (the waterline swings
        // up to CoastWander + BendStrength off that line), the ground lies under the sea level all the way to where
        // the camera's sight ends (the beach's start camera, scene/beach.cpp: 2 km).
        constexpr float SightEnd = 2000.0f;
        const float     surf     = beach.CoastWander + beach.BendStrength + 30.0f;
        float           highest  = std::numeric_limits<float>::lowest();
        for (float along = -500.0f; along <= 500.0f; along += 50.0f)
        {
            for (float out = surf; out <= SightEnd; out += 10.0f)
            {
                highest = std::max(highest, Wotah::GetTerrainHeight(beach, glm::vec2(0.0f, along) + out * sunset));
            }
        }
        CHECK(highest < beach.SeaLevel);
    }

    TEST_CASE("the land is smooth: neighbouring places a tenth of a unit apart differ by little")
    {
        const Wotah::TerrainShape beach;
        float                     steepest = 0.0f;
        for (float z = -300.0f; z <= 300.0f; z += 7.3f)
        {
            for (float x = -300.0f; x <= 300.0f; x += 11.1f)
            {
                const float here = Wotah::GetTerrainHeight(beach, {x, z});
                steepest         = std::max(steepest, std::abs(Wotah::GetTerrainHeight(beach, {x + 0.1f, z}) - here));
                steepest         = std::max(steepest, std::abs(Wotah::GetTerrainHeight(beach, {x, z + 0.1f}) - here));
            }
        }
        CHECK(steepest < 0.15f); // a slope below 1.5: no cliffs, no jumps
    }

    TEST_CASE("asking for less detail changes the land smoothly and only by the left-out detail's size")
    {
        const Wotah::TerrainShape beach;
        for (const glm::vec2 place : {glm::vec2(41.2f, 13.7f), glm::vec2(210.5f, -88.0f), glm::vec2(-30.0f, 5.0f)})
        {
            CAPTURE(place.x);
            CAPTURE(place.y);
            const float full = Wotah::GetTerrainHeight(beach, place);

            // Leaving out everything under 4 units only takes the bumps' finer layers: a fraction of their height.
            CHECK(std::abs(Wotah::GetTerrainHeight(beach, place, 4.0f) - full) < beach.BumpHeight);

            // A little less detail asked for, a little change: no jumps as the smallest kept detail grows by 1 % at a
            // time (fading out even the 3.5-unit dunes takes some 70 such steps).
            float last = full;
            for (float smallest = 1.0f; smallest < 64.0f; smallest *= 1.01f)
            {
                const float height = Wotah::GetTerrainHeight(beach, place, smallest);
                CHECK(std::abs(height - last) < 0.1f);
                last = height;
            }
        }
    }

    TEST_CASE("a flat shape is flat at its height everywhere, whatever detail is asked for")
    {
        const Wotah::TerrainShape flat = Wotah::MakeFlatTerrainShape(2.5f);
        CHECK(Wotah::IsUsable(flat));
        for (const glm::vec2 place : {glm::vec2(0.0f), glm::vec2(100.0f, -300.0f), glm::vec2(-7.0f, 900.0f)})
        {
            CHECK(Wotah::GetTerrainHeight(flat, place) == 2.5f);
            CHECK(Wotah::GetTerrainHeight(flat, place, 16.0f) == 2.5f);
        }
    }

    TEST_CASE("a shape is only usable with finite numbers, wavelengths and ramps above 0, no negative heights")
    {
        CHECK(Wotah::IsUsable(Wotah::TerrainShape{}));

        Wotah::TerrainShape noWavelength;
        noWavelength.HillWavelength = 0.0f;
        CHECK_FALSE(Wotah::IsUsable(noWavelength));

        Wotah::TerrainShape noRamp;
        noRamp.DuneRamp = 0.0f;
        CHECK_FALSE(Wotah::IsUsable(noRamp));

        Wotah::TerrainShape negativeHeight;
        negativeHeight.DuneHeight = -1.0f;
        CHECK_FALSE(Wotah::IsUsable(negativeHeight));

        Wotah::TerrainShape noNumber;
        noNumber.SeaLevel = std::numeric_limits<float>::quiet_NaN();
        CHECK_FALSE(Wotah::IsUsable(noNumber));
    }

    TEST_CASE("a patch holds the land's heights on its grid, row by row from -z, each from -x, around its center")
    {
        const Wotah::TerrainShape beach;
        const Wotah::TerrainPatch patch = Wotah::SampleTerrainPatch(beach, {10.0f, -20.0f}, 2.0f, 5);
        REQUIRE(patch.Heights.size() == 25);
        CHECK(patch.SamplesPerSide == 5);
        CHECK(patch.Spacing == 2.0f);
        CHECK(patch.Corner.x == 6.0f);
        CHECK(patch.Corner.y == -24.0f);
        for (uint32_t row = 0; row < 5; ++row)
        {
            for (uint32_t column = 0; column < 5; ++column)
            {
                const glm::vec2 place =
                    patch.Corner + glm::vec2(static_cast<float>(column), static_cast<float>(row)) * 2.0f;
                CHECK(patch.Heights[std::size_t{row} * 5 + column] == Wotah::GetTerrainHeight(beach, place));
            }
        }

        // Bad spacings, sample counts or shapes give no patch.
        CHECK(Wotah::SampleTerrainPatch(beach, {}, 0.0f, 5).Heights.empty());
        CHECK(Wotah::SampleTerrainPatch(beach, {}, 1.0f, 1).Heights.empty());
        CHECK(Wotah::SampleTerrainPatch(beach, {}, 1.0f, 5000).Heights.empty());
        Wotah::TerrainShape broken;
        broken.BumpWavelength = -1.0f;
        CHECK(Wotah::SampleTerrainPatch(broken, {}, 1.0f, 5).Heights.empty());
    }

    TEST_CASE("a patch sampled in bands of rows, in any order, holds the very heights of one sampled whole")
    {
        // As the land collider samples a tile it waits for: bands of rows, each on its own thread.
        const Wotah::TerrainShape beach;
        const Wotah::TerrainPatch whole = Wotah::SampleTerrainPatch(beach, {-30.0f, 12.0f}, 1.0f, 9);
        Wotah::TerrainPatch       parts = Wotah::MakeTerrainPatch(beach, {-30.0f, 12.0f}, 1.0f, 9);
        REQUIRE(parts.Heights.size() == whole.Heights.size());
        CHECK(parts.Corner == whole.Corner);
        CHECK(std::ranges::all_of(parts.Heights, [](float height) { return height == 0.0f; })); // not sampled yet
        Wotah::SampleTerrainRows(beach, parts, 6, 20); // the last band reaching past the patch
        Wotah::SampleTerrainRows(beach, parts, 0, 3);
        Wotah::SampleTerrainRows(beach, parts, 3, 6);
        CHECK(parts.Heights == whole.Heights);

        // A grid that cannot be is refused as by SampleTerrainPatch; sampling its rows does nothing.
        Wotah::TerrainPatch none = Wotah::MakeTerrainPatch(beach, {}, 0.0f, 5);
        CHECK(none.Heights.empty());
        Wotah::SampleTerrainRows(beach, none, 0, 5);
        CHECK(none.Heights.empty());
    }
}
