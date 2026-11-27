/**
 * @file land_collider_tests.cpp
 * The land's collider (physics/land_collider.hpp): which tile a place is on, the tiles kept around places as they
 * move (the one under a place at once, the rest once sampled, far ones dropped), and a ball landing on the land far
 * from the start, on a tile's edge too.
 */
#include "physics/land_collider.hpp"
#include "physics/physics_world.hpp"
#include "terrain/terrain_shape.hpp"

#include <doctest/doctest.h>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <span>

namespace
{
    /** Sand's surface, as in the program. */
    constexpr Wotah::BodySurface Sand{0.8f, 0.1f};

    /** Whether every tile within reach of a tile is in the collider. */
    bool HasTilesAround(const Wotah::LandCollider& land, const glm::ivec2& middle)
    {
        for (int z = -Wotah::LandTileReach; z <= Wotah::LandTileReach; ++z)
        {
            for (int x = -Wotah::LandTileReach; x <= Wotah::LandTileReach; ++x)
            {
                if (!land.HasTile(middle + glm::ivec2(x, z)))
                {
                    return false;
                }
            }
        }
        return true;
    }
} // namespace

TEST_SUITE("land collider")
{
    TEST_CASE("a place's tile: tiles reach 63 units from their corner on, both ways from the origin")
    {
        CHECK(Wotah::LandTileSize == 63.0f);
        CHECK(Wotah::FindLandTile({0.0f, 0.0f}) == glm::ivec2(0, 0));
        CHECK(Wotah::FindLandTile({62.9f, -0.1f}) == glm::ivec2(0, -1));
        CHECK(Wotah::FindLandTile({63.0f, 126.5f}) == glm::ivec2(1, 2));
        CHECK(Wotah::FindLandTile({-63.0f, -63.1f}) == glm::ivec2(-1, -2));
    }

    TEST_CASE("the tiles around a place: its own at once, all of them once sampled, the old ones dropped as it moves")
    {
        Wotah::PhysicsWorld world;
        REQUIRE(world.Create());
        Wotah::LandCollider land;
        REQUIRE(land.Create(world, Wotah::TerrainShape{}, Sand));

        std::array<glm::vec3, 1> place{glm::vec3(10.0f, 0.0f, 10.0f)};
        land.Follow(place);
        CHECK(land.HasTile({0, 0})); // under the place, made at once
        land.FinishPending();
        land.Follow(place);
        CHECK(land.GetTileCount() == 9);
        CHECK(HasTilesAround(land, {0, 0}));

        // Far away: the tile under it at once, the old ones gone, the new ones once sampled.
        place.front()            = {1000.0f, 0.0f, -500.0f};
        const glm::ivec2 farTile = Wotah::FindLandTile({1000.0f, -500.0f});
        land.Follow(place);
        CHECK(land.HasTile(farTile));
        CHECK_FALSE(land.HasTile({0, 0}));
        land.FinishPending();
        land.Follow(place);
        CHECK(land.GetTileCount() == 9);
        CHECK(HasTilesAround(land, farTile));

        // A step onto the next tile: the tiles one further off stay, the new row is added.
        place.front().x += Wotah::LandTileSize;
        land.Follow(place);
        land.FinishPending();
        land.Follow(place);
        CHECK(HasTilesAround(land, farTile + glm::ivec2(1, 0)));
        CHECK(land.HasTile(farTile + glm::ivec2(-1, 0))); // one further off: kept
        CHECK(land.GetTileCount() == 12);
    }

    TEST_CASE("two places far apart keep a group of tiles each; a place that is no number is passed over")
    {
        Wotah::PhysicsWorld world;
        REQUIRE(world.Create());
        Wotah::LandCollider land;
        REQUIRE(land.Create(world, Wotah::TerrainShape{}, Sand));
        const std::array<glm::vec3, 3> places{glm::vec3(0.0f), glm::vec3(-2000.0f, 0.0f, 3000.0f),
                                              glm::vec3(std::numeric_limits<float>::quiet_NaN())};
        land.Follow(places);
        land.FinishPending();
        land.Follow(places);
        CHECK(land.GetTileCount() == 18);
        CHECK(HasTilesAround(land, {0, 0}));
        CHECK(HasTilesAround(land, Wotah::FindLandTile({-2000.0f, 3000.0f})));

        land.Destroy(); // the tiles leave the world; a second Destroy is fine
        land.Destroy();
        CHECK(land.GetTileCount() == 0);
    }

    TEST_CASE("a ball dropped far from the start lands on the land there, on a tile's edge too")
    {
        const Wotah::TerrainShape shape;
        constexpr float           Radius = 0.5f;
        for (const glm::vec2 spot : {glm::vec2(500.0f, 500.0f), glm::vec2(10.0f * Wotah::LandTileSize, 300.0f)})
        {
            CAPTURE(spot.x);
            Wotah::PhysicsWorld world;
            REQUIRE(world.Create());
            Wotah::LandCollider land;
            REQUIRE(land.Create(world, shape, Sand));

            Wotah::BodySettings ball;
            ball.Position                           = {spot.x, Wotah::GetTerrainHeight(shape, spot) + 1.0f, spot.y};
            ball.Surface                            = Sand;
            const std::optional<Wotah::BodyId> body = world.AddSphere(Radius, ball);
            REQUIRE(body.has_value());

            // Three seconds of falling and settling, the land following the ball as the program does.
            for (int frame = 0; frame < 180; ++frame)
            {
                const glm::vec3 at = glm::vec3((*world.FindTransform(*body))[3]);
                land.Follow(std::span(&at, 1));
                world.Step(1.0f / 60.0f);
            }
            const glm::vec3 at     = glm::vec3((*world.FindTransform(*body))[3]);
            const float     ground = Wotah::GetTerrainHeight(shape, {at.x, at.z});
            CAPTURE(at.y);
            CAPTURE(ground);
            CHECK(at.y - Radius - ground > -0.05f); // not sunk in or fallen through
            CHECK(at.y - Radius - ground < 0.15f);  // resting on it (a little higher on a slope)
        }
    }

    TEST_CASE("a shape that cannot be used is refused; nothing happens before Create")
    {
        Wotah::PhysicsWorld world;
        REQUIRE(world.Create());
        Wotah::LandCollider            land;
        const std::array<glm::vec3, 1> place{glm::vec3(0.0f)};
        land.Follow(place); // not created: nothing to do
        CHECK(land.GetTileCount() == 0);
        Wotah::TerrainShape broken;
        broken.HillWavelength = 0.0f;
        CHECK_FALSE(land.Create(world, broken, Sand));
    }
}
