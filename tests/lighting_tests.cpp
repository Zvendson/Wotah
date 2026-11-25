/**
 * @file lighting_tests.cpp
 * The compass of sky lights: MakeLightDirection for lights in every direction and height.
 */
#include "render/lighting.hpp"
#include "test_utils.hpp"

#include <doctest/doctest.h>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

#include <cmath>

using Wotah::Testing::IsNear;

TEST_SUITE("lighting")
{
    TEST_CASE("a sky light shines away from where it stands: north -z, east +x, south +z, west -x")
    {
        const float half = std::sqrt(0.5f);

        // A sun in the east at 45 degrees shines down and towards the west.
        CHECK(IsNear(Wotah::MakeLightDirection(glm::radians(90.0f), glm::radians(45.0f)), {-half, -half, 0.0f}));

        // On the horizon in each direction: it shines straight across, to the opposite side.
        CHECK(IsNear(Wotah::MakeLightDirection(glm::radians(0.0f), 0.0f), {0.0f, 0.0f, 1.0f}));    // from the north
        CHECK(IsNear(Wotah::MakeLightDirection(glm::radians(90.0f), 0.0f), {-1.0f, 0.0f, 0.0f}));  // from the east
        CHECK(IsNear(Wotah::MakeLightDirection(glm::radians(180.0f), 0.0f), {0.0f, 0.0f, -1.0f})); // from the south
        CHECK(IsNear(Wotah::MakeLightDirection(glm::radians(270.0f), 0.0f), {1.0f, 0.0f, 0.0f}));  // from the west

        // Straight overhead, the compass angle does not matter: it shines straight down.
        CHECK(IsNear(Wotah::MakeLightDirection(glm::radians(123.0f), glm::radians(90.0f)), {0.0f, -1.0f, 0.0f}));

        // Always length 1.
        CHECK(IsNear(glm::length(Wotah::MakeLightDirection(1.0f, 0.3f)), 1.0f));
    }
}
