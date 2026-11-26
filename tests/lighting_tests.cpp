/**
 * @file lighting_tests.cpp
 * The compass of sky lights (MakeLightDirection for lights in every direction and height), how much sunlight crosses
 * the air (MakeSunlightThroughAir), and the shadow cascades:
 * where they end, the stretch of view each shows, and a light's view of it (MakeShadowViewProjection).
 */
#include "render/camera.hpp"
#include "render/lighting.hpp"
#include "render/shadow_maps.hpp"
#include "test_utils.hpp"

#include <doctest/doctest.h>
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <optional>

using Wotah::Testing::IsNear;

namespace
{
    /** Where a world point lands in a light's shadow map: x and y across the map (-1..1), z its depth (0..1). */
    glm::vec3 ToMap(const glm::mat4& viewProjection, const glm::vec3& point)
    {
        const glm::vec4 clip = viewProjection * glm::vec4(point, 1.0f);
        return glm::vec3(clip) / clip.w;
    }

    /** Whether a point lands on the map, inside its depth range (with Tolerance). */
    bool IsOnMap(const glm::vec3& spot)
    {
        constexpr float Edge = 1.0f + Wotah::Testing::Tolerance;
        return std::abs(spot.x) <= Edge && std::abs(spot.y) <= Edge && spot.z >= -Wotah::Testing::Tolerance &&
               spot.z <= Edge;
    }
} // namespace

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

    TEST_CASE(
        "sunlight through the air: all of it overhead, dimmer and redder the lower the sun, none below the horizon")
    {
        const glm::vec3 all(1.0f);
        const glm::vec3 none(0.0f);
        CHECK(IsNear(Wotah::MakeSunlightThroughAir(glm::radians(90.0f)), all));
        CHECK(IsNear(Wotah::MakeSunlightThroughAir(glm::radians(120.0f)), all)); // past overhead counts as overhead

        // Lower and lower: every color keeps less, blue the least, so the light dims and turns red.
        glm::vec3 higher = Wotah::MakeSunlightThroughAir(glm::radians(90.0f));
        for (const float degrees : {60.0f, 45.0f, 30.0f, 20.0f, 10.0f, 5.0f, 2.0f})
        {
            CAPTURE(degrees);
            const glm::vec3 light = Wotah::MakeSunlightThroughAir(glm::radians(degrees));
            CHECK(light.r < higher.r);
            CHECK(light.g < higher.g);
            CHECK(light.b < higher.b);
            CHECK(light.r > light.g);
            CHECK(light.g > light.b);
            higher = light;
        }

        // At the scene's start, 45 degrees up, most of it gets through; 2 degrees up, the light is clearly red.
        CHECK(Wotah::MakeSunlightThroughAir(glm::radians(45.0f)).b > 0.8f);
        const glm::vec3 sunset = Wotah::MakeSunlightThroughAir(glm::radians(2.0f));
        CHECK(sunset.r < 0.5f);
        CHECK(sunset.r > 20.0f * sunset.b);

        // On the horizon the light crosses about 38 times the air of straight up: a dim red glow is left, hardly any
        // blue. Below it, or for an elevation that is not a number, nothing.
        const glm::vec3 horizon = Wotah::MakeSunlightThroughAir(0.0f);
        CHECK(horizon.r > 0.1f);
        CHECK(horizon.r < 0.2f);
        CHECK(horizon.b < 0.001f);
        CHECK(IsNear(Wotah::MakeSunlightThroughAir(glm::radians(-1.0f)), none));
        CHECK(IsNear(Wotah::MakeSunlightThroughAir(std::numeric_limits<float>::quiet_NaN()), none));
    }

    TEST_CASE("a light's view of a shown sphere: all of it on the map, the casters' whole depth in 0..1")
    {
        const Wotah::ShadowArea shown{{2.0f, 1.0f, -3.0f}, 10.0f};
        const Wotah::ShadowArea casters{{0.0f, 0.0f, 0.0f}, 30.0f};

        // Straight down (a light that needs another up direction), along the compass, low and slanted, from below.
        const std::array<glm::vec3, 5> directions{{
            {0.0f, -1.0f, 0.0f},
            {1.0f, -1.0f, 0.0f},
            {0.0f, -0.3f, -1.0f},
            {-0.4f, -1.0f, -0.25f},
            {0.0f, 1.0f, 0.0f},
        }};
        const std::array<glm::vec3, 6> sides{{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
        const float                    onePixel = 2.0f / static_cast<float>(Wotah::ShadowMapSize); // in map units
        for (const glm::vec3& direction : directions)
        {
            CAPTURE(direction.x);
            CAPTURE(direction.y);
            CAPTURE(direction.z);
            const std::optional<glm::mat4> view = Wotah::MakeShadowViewProjection(direction * 3.0f, shown, casters);
            REQUIRE(view.has_value());

            // The shown middle in the middle of the map, give or take the pixel it was moved onto the grid.
            const glm::vec3 middle = ToMap(*view, shown.Center);
            CHECK(std::abs(middle.x) <= onePixel);
            CHECK(std::abs(middle.y) <= onePixel);

            // Every point on the shown sphere lands on the map (with that pixel of room), every caster in its depth.
            for (const glm::vec3& side : sides)
            {
                const glm::vec3 spot = ToMap(*view, shown.Center + side * shown.Radius);
                CHECK(std::abs(spot.x) <= 1.0f + onePixel);
                CHECK(std::abs(spot.y) <= 1.0f + onePixel);
                CHECK(IsOnMap(glm::vec3(0.0f, 0.0f, ToMap(*view, casters.Center + side * casters.Radius).z)));
            }

            // Nearer the light means less depth: depth 0 where the light enters the casters' sphere, 1 where it leaves.
            const glm::vec3 along = glm::normalize(direction);
            CHECK(IsNear(ToMap(*view, casters.Center - along * casters.Radius).z, 0.0f));
            CHECK(IsNear(ToMap(*view, casters.Center + along * casters.Radius).z, 1.0f));
        }

        // The size of one map pixel: the shown diameter over the map's pixels.
        CHECK(IsNear(Wotah::GetShadowTexelSize(shown), 20.0f / static_cast<float>(Wotah::ShadowMapSize)));
    }

    TEST_CASE("a map that follows the camera moves in whole map pixels, so its shadows do not shimmer")
    {
        const Wotah::ShadowArea casters{{0.0f, 0.0f, 0.0f}, 30.0f};
        const glm::vec3         light{0.4f, -1.0f, -0.25f};
        const glm::vec3         world{1.234f, 0.5f, -2.345f}; // any fixed point in the world
        const float             pixels = static_cast<float>(Wotah::ShadowMapSize) * 0.5f; // map units to pixels

        // The shown sphere moves by odd fractions of a pixel, as a camera's does: the fixed point's place on the map
        // changes by whole pixels only, so it always falls on the same spot within a map pixel.
        Wotah::ShadowArea              shown{{0.3f, 0.2f, 0.1f}, 5.0f};
        const std::optional<glm::mat4> before = Wotah::MakeShadowViewProjection(light, shown, casters);
        REQUIRE(before.has_value());
        const glm::vec3 start = ToMap(*before, world) * pixels;
        for (const float move : {0.0011f, 0.37f, 2.5f, -7.77f})
        {
            CAPTURE(move);
            shown.Center += glm::vec3(move, move * 0.5f, -move);
            const std::optional<glm::mat4> after = Wotah::MakeShadowViewProjection(light, shown, casters);
            REQUIRE(after.has_value());
            const glm::vec3 now = ToMap(*after, world) * pixels;
            CHECK(std::abs(std::remainder(now.x - start.x, 1.0f)) < 1.0e-2f);
            CHECK(std::abs(std::remainder(now.y - start.y, 1.0f)) < 1.0e-2f);
        }
    }

    TEST_CASE("cascades: short ones near the camera, longer ones away from it, the last ending at the reach")
    {
        const std::optional<Wotah::CascadeEnds> ends = Wotah::MakeCascadeEnds(0.1f, 50.0f);
        REQUIRE(ends.has_value());
        float before = 0.1f;
        float length = 0.0f;
        for (const float end : *ends)
        {
            CHECK(end > before);
            CHECK(end - before > length); // each longer than the one before
            length = end - before;
            before = end;
        }
        CHECK(ends->back() == 50.0f);
        CHECK(ends->front() < 5.0f); // the nearest one is short, so its map pixels are small

        CHECK_FALSE(Wotah::MakeCascadeEnds(0.0f, 50.0f).has_value());
        CHECK_FALSE(Wotah::MakeCascadeEnds(10.0f, 5.0f).has_value());
        CHECK_FALSE(Wotah::MakeCascadeEnds(0.1f, std::numeric_limits<float>::quiet_NaN()).has_value());
    }

    TEST_CASE("a cascade's sphere holds its whole stretch of the view, the same size wherever the camera looks")
    {
        Wotah::Camera camera;
        camera.SetPosition({1.0f, 2.0f, 3.0f});
        camera.SetTarget({0.0f, 0.5f, 0.0f});
        REQUIRE(camera.SetAspectRatio(16.0f / 9.0f));
        const glm::mat4 toWorld = glm::inverse(camera.GetViewProjection());

        for (const std::array<float, 2> stretch :
             {std::array<float, 2>{0.1f, 3.0f}, std::array<float, 2>{2.7f, 7.0f}, std::array<float, 2>{16.0f, 50.0f}})
        {
            CAPTURE(stretch[0]);
            CAPTURE(stretch[1]);
            const std::optional<Wotah::ShadowArea> area = Wotah::MakeCascadeArea(camera, stretch[0], stretch[1]);
            REQUIRE(area.has_value());

            // The stretch's eight corners: the screen's corners at both distances, from the camera's own matrices.
            for (const float distance : stretch)
            {
                const glm::vec4 atDistance = camera.GetProjection() * glm::vec4(0.0f, 0.0f, -distance, 1.0f);
                const float     depth      = atDistance.z / atDistance.w;
                for (const glm::vec2 corner : {glm::vec2(-1, -1), glm::vec2(-1, 1), glm::vec2(1, -1), glm::vec2(1, 1)})
                {
                    const glm::vec4 point = toWorld * glm::vec4(corner, depth, 1.0f);
                    CHECK(glm::length(glm::vec3(point) / point.w - area->Center) <= area->Radius + 1.0e-3f);
                }
            }

            // Looking elsewhere changes where the sphere is, never its size.
            Wotah::Camera turned = camera;
            turned.SetTarget({-5.0f, 3.0f, 1.0f});
            const std::optional<Wotah::ShadowArea> turnedArea = Wotah::MakeCascadeArea(turned, stretch[0], stretch[1]);
            REQUIRE(turnedArea.has_value());
            CHECK(turnedArea->Radius == area->Radius);
        }

        CHECK_FALSE(Wotah::MakeCascadeArea(camera, 5.0f, 5.0f).has_value());
        CHECK_FALSE(Wotah::MakeCascadeArea(camera, -1.0f, 5.0f).has_value());
    }

    TEST_CASE("no light's view without a direction or spheres with a radius")
    {
        const float             notANumber = std::numeric_limits<float>::quiet_NaN();
        const glm::vec3         down{0.0f, -1.0f, 0.0f};
        const Wotah::ShadowArea unit{glm::vec3(0.0f), 1.0f};
        CHECK_FALSE(Wotah::MakeShadowViewProjection(down, {}, unit).has_value()); // radius 0
        CHECK_FALSE(Wotah::MakeShadowViewProjection(down, unit, {}).has_value());
        CHECK_FALSE(Wotah::MakeShadowViewProjection(down, {glm::vec3(0.0f), -1.0f}, unit).has_value());
        CHECK_FALSE(Wotah::MakeShadowViewProjection(down, {glm::vec3(0.0f), notANumber}, unit).has_value());
        CHECK_FALSE(Wotah::MakeShadowViewProjection(down, {glm::vec3(notANumber), 1.0f}, unit).has_value());
        CHECK_FALSE(Wotah::MakeShadowViewProjection(glm::vec3(0.0f), unit, unit).has_value());
        CHECK_FALSE(Wotah::MakeShadowViewProjection(glm::vec3(notANumber), unit, unit).has_value());
    }
}
