/**
 * @file water_tests.cpp
 * The sea's surface: at its level everywhere, and nowhere for a place that is not a number.
 */
#include "render/water.hpp"

#include <doctest/doctest.h>

#include <limits>

TEST_SUITE("water")
{
    TEST_CASE("the sea's surface lies at its level everywhere, near and far, and nowhere for a place that is no number")
    {
        const Wotah::Sea sea{-1.5f};
        CHECK(Wotah::FindWaterHeight(sea, 0.0f, 0.0f) == -1.5f);
        CHECK(Wotah::FindWaterHeight(sea, 31.9f, -15.9f) == -1.5f);
        CHECK(Wotah::FindWaterHeight(sea, -5000.0f, 12000.0f) == -1.5f);

        constexpr float NoNumber = std::numeric_limits<float>::quiet_NaN();
        constexpr float Endless  = std::numeric_limits<float>::infinity();
        CHECK_FALSE(Wotah::FindWaterHeight(sea, NoNumber, 0.0f).has_value());
        CHECK_FALSE(Wotah::FindWaterHeight(sea, 0.0f, NoNumber).has_value());
        CHECK_FALSE(Wotah::FindWaterHeight(sea, Endless, 0.0f).has_value());
    }
}
