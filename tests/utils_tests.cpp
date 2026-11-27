/**
 * @file utils_tests.cpp
 * The helpers in core/utils.hpp: the executable's folder, where a ray meets a ball, and the noise (the same every time,
 * zero on its grid, a slope that matches its values, within its range, and smooth as detail is left out).
 */
#include "core/utils.hpp"

#include <doctest/doctest.h>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <optional>

TEST_SUITE("utils")
{
    TEST_CASE("the executable's folder holds this test program")
    {
        const std::optional<std::filesystem::path> folder = Wotah::FindExecutableDirectory();
        REQUIRE(folder.has_value());
        CHECK(std::filesystem::is_directory(*folder));
        CHECK(
            (std::filesystem::exists(*folder / "wotah_tests.exe") || std::filesystem::exists(*folder / "wotah_tests")));
    }

    TEST_CASE("gradient noise: the same for the same place and seed, 0 on its grid, another seed another noise")
    {
        const glm::vec2 place{3.37f, -12.81f};
        CHECK(Wotah::GetGradientNoise(place, 7) == Wotah::GetGradientNoise(place, 7));
        CHECK(Wotah::GetGradientNoise(place, 7).x != Wotah::GetGradientNoise(place, 8).x);
        for (const glm::vec2 corner : {glm::vec2(0.0f), glm::vec2(5.0f, -3.0f), glm::vec2(-1000.0f, 42.0f)})
        {
            CHECK(Wotah::GetGradientNoise(corner, 7).x == 0.0f);
        }
    }

    TEST_CASE("gradient noise: its slope is how fast its value changes, and it stays between -1 and 1")
    {
        constexpr float Step       = 1.0e-3f;
        constexpr float SlopeSlack = 0.02f; // a central difference over Step is this close, relatively
        float           range      = 0.0f;
        for (float y = -3.0f; y < 3.0f; y += 0.173f)
        {
            for (float x = -3.0f; x < 3.0f; x += 0.191f)
            {
                const glm::vec3 noise = Wotah::GetGradientNoise({x, y}, 11);
                const float     alongX =
                    (Wotah::GetGradientNoise({x + Step, y}, 11).x - Wotah::GetGradientNoise({x - Step, y}, 11).x) /
                    (2.0f * Step);
                const float alongY =
                    (Wotah::GetGradientNoise({x, y + Step}, 11).x - Wotah::GetGradientNoise({x, y - Step}, 11).x) /
                    (2.0f * Step);
                CHECK(std::abs(noise.y - alongX) <= SlopeSlack * (1.0f + std::abs(alongX)));
                CHECK(std::abs(noise.z - alongY) <= SlopeSlack * (1.0f + std::abs(alongY)));
                range = std::max(range, std::abs(noise.x));
            }
        }
        CHECK(range <= 1.0f);
        CHECK(range > 0.3f); // it does vary
    }

    TEST_CASE("layered noise: 0 at the origin, between -1 and 1, and smooth as finer layers are left out")
    {
        CHECK(Wotah::GetLayeredNoise(glm::vec2(0.0f), 3, 6, 0.0f) == 0.0f);
        CHECK(Wotah::GetErodedNoise(glm::vec2(0.0f), 3, 6, 0.0f) == 0.0f);

        float largest = 0.0f;
        for (float y = -20.0f; y < 20.0f; y += 0.77f)
        {
            for (float x = -20.0f; x < 20.0f; x += 0.83f)
            {
                largest = std::max(largest, std::abs(Wotah::GetLayeredNoise({x, y}, 3, 6, 0.0f)));
                largest = std::max(largest, std::abs(Wotah::GetErodedNoise({x, y}, 3, 6, 0.0f)));
            }
        }
        CHECK(largest <= 1.0f);

        // Leaving out finer and finer layers changes the value step by step, never by a jump.
        const glm::vec2 place{4.21f, -7.93f};
        float           last = Wotah::GetLayeredNoise(place, 3, 6, 0.0f);
        for (float smallest = 0.01f; smallest < 2.0f; smallest *= 1.05f)
        {
            const float value = Wotah::GetLayeredNoise(place, 3, 6, smallest);
            CHECK(std::abs(value - last) < 0.05f);
            last = value;
        }

        // With every layer left out (all shorter than smallest), nothing is left.
        CHECK(Wotah::GetLayeredNoise(place, 3, 6, 2.0f) == 0.0f);
    }

    TEST_CASE("a layer's share: none below smallest, all from twice it, all when smallest is 0 or no number")
    {
        CHECK(Wotah::GetLayerShare(0.5f, 1.0f) == 0.0f);
        CHECK(Wotah::GetLayerShare(1.0f, 1.0f) == 0.0f);
        CHECK(std::abs(Wotah::GetLayerShare(1.5f, 1.0f) - 0.5f) < 1.0e-6f);
        CHECK(Wotah::GetLayerShare(2.0f, 1.0f) == 1.0f);
        CHECK(Wotah::GetLayerShare(0.1f, 0.0f) == 1.0f);
        CHECK(Wotah::GetLayerShare(0.1f, std::nanf("")) == 1.0f);
    }

    TEST_CASE("a ray meets a ball where it first enters it; from inside at once; past it or behind never")
    {
        const glm::vec3 center{0.0f, 0.0f, -10.0f};
        const glm::vec3 ahead{0.0f, 0.0f, -1.0f};

        // Straight at it: the near side, the radius short of the middle.
        const std::optional<float> straight = Wotah::FindRaySphereHit({0.0f, 0.0f, 0.0f}, ahead, center, 2.0f);
        REQUIRE(straight.has_value());
        CHECK(std::abs(*straight - 8.0f) < 1.0e-5f);

        // Half the radius aside: sqrt(2^2 - 1^2) = sqrt(3) short of the middle.
        const std::optional<float> aside = Wotah::FindRaySphereHit({1.0f, 0.0f, 0.0f}, ahead, center, 2.0f);
        REQUIRE(aside.has_value());
        CHECK(std::abs(*aside - (10.0f - std::sqrt(3.0f))) < 1.0e-5f);

        // From inside it, at once; passing beside it, or with the ball behind, not at all.
        CHECK(Wotah::FindRaySphereHit({0.0f, 0.0f, -9.0f}, ahead, center, 2.0f) == 0.0f);
        CHECK_FALSE(Wotah::FindRaySphereHit({2.5f, 0.0f, 0.0f}, ahead, center, 2.0f).has_value());
        CHECK_FALSE(Wotah::FindRaySphereHit({0.0f, 0.0f, -13.0f}, ahead, center, 2.0f).has_value());
    }
}
