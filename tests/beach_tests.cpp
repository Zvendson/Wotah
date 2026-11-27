/**
 * @file beach_tests.cpp
 * The example scene (scene/beach.hpp): the start view over the sand, the start settings, and the beach's physics: the
 * ball dropped into the shallow water comes to rest on the sand under it, Reset puts it back into the air, the
 * settings reach the world. No GPU needed.
 */
#include "scene/beach.hpp"

#include <doctest/doctest.h>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>

#include <cmath>

namespace
{
    /** One physics step: the world's fixed step (physics/physics_world.hpp). */
    constexpr float StepSeconds = 1.0f / 60.0f;

    /** How far a resting ball may sit off the ground: the world lets bodies sink 5 mm, plus slope and rounding. */
    constexpr float RestingSlack = 0.05f;

    /** Moves the world on by seconds in single steps, the viewer far off at the start camera. */
    void RunWorld(Wotah::BeachWorld& world, float seconds)
    {
        const glm::vec3 viewer = Wotah::MakeBeachCamera().GetPosition();
        for (float run = 0.0f; run < seconds; run += StepSeconds)
        {
            world.Step(StepSeconds, viewer);
        }
    }

    /** Where the ball's middle is. */
    glm::vec3 GetBallPlace(const Wotah::BeachWorld& world)
    {
        return glm::vec3(world.GetBallTransform()[3]);
    }
} // namespace

TEST_SUITE("beach")
{
    TEST_CASE("the start view stands 4 units over the sand and looks out west over the water")
    {
        const Wotah::Camera camera = Wotah::MakeBeachCamera();
        const glm::vec3     eye    = camera.GetPosition();
        const float         ground = Wotah::GetTerrainHeight(Wotah::BeachLand, {eye.x, eye.z});
        CHECK(std::abs(eye.y - ground - 4.0f) < 1e-3f);
        CHECK(ground > Wotah::BeachSea.Level); // on dry sand
        const glm::vec3 forward = glm::normalize(camera.GetTarget() - eye);
        CHECK(forward.x < -0.5f);                 // westwards, towards the sea
        CHECK(camera.GetFarDistance() > 1000.0f); // land and sea reach the haze
    }

    TEST_CASE("the start settings: a running morning, normal gravity, real time, the ball's rubber bounce")
    {
        const Wotah::SceneSettings settings = Wotah::MakeBeachSettings();
        CHECK(settings.TimeOfDayHours == 9.0f);
        CHECK(settings.IsDayRunning);
        CHECK(settings.HasShadows);
        CHECK(settings.Gravity == Wotah::StandardGravity);
        CHECK(settings.TimeSpeed == 1.0f);
        CHECK(settings.BallBounciness > 0.0f);
        CHECK(std::abs(settings.FieldOfViewDegrees - glm::degrees(Wotah::Camera{}.GetFieldOfView())) < 1e-4f);
    }

    TEST_CASE("the ball drops into the shallow water, rests on the sand under it, and Reset puts it back in the air")
    {
        Wotah::BeachWorld world;
        CHECK(world.GetBallTransform() == glm::mat4(1.0f)); // nothing yet
        REQUIRE(world.Create());

        // It starts over the water, well above it.
        const glm::vec3 start = GetBallPlace(world);
        CHECK(start.y > Wotah::BeachSea.Level + Wotah::BeachBallRadius + 1.0f);
        CHECK(Wotah::GetTerrainHeight(Wotah::BeachLand, {start.x, start.z}) < Wotah::BeachSea.Level);

        // A few seconds on: it lies on the sea floor (the water has no physics yet), rolling slowly down the floor's
        // slope out to sea (west), about a metre per second: the damping slows it, but never stops it on that slope.
        RunWorld(world, 6.0f);
        const glm::vec3 onFloor = GetBallPlace(world);
        const float     floor   = Wotah::GetTerrainHeight(Wotah::BeachLand, {onFloor.x, onFloor.z});
        CAPTURE(onFloor.x);
        CAPTURE(onFloor.y);
        CAPTURE(onFloor.z);
        CHECK(std::abs(onFloor.y - floor - Wotah::BeachBallRadius) < RestingSlack);
        CHECK(onFloor.y + Wotah::BeachBallRadius < Wotah::BeachSea.Level); // under the water
        CHECK(onFloor.x < start.x);                                        // out to sea, never up the beach
        CHECK(glm::distance(glm::vec2(onFloor.x, onFloor.z), glm::vec2(start.x, start.z)) < 10.0f);

        // Back in the air at once, and it falls again.
        CHECK(world.Reset());
        CHECK(glm::distance(GetBallPlace(world), start) < 1e-3f);
        RunWorld(world, 0.5f);
        CHECK(GetBallPlace(world).y < start.y - 0.5f);
    }

    TEST_CASE("the settings reach the world: without gravity the ball hangs where it started")
    {
        Wotah::BeachWorld world;
        REQUIRE(world.Create());
        Wotah::SceneSettings settings = Wotah::MakeBeachSettings();
        settings.Gravity              = 0.0f;
        CHECK(world.ApplySettings(settings));
        const glm::vec3 start = GetBallPlace(world);
        RunWorld(world, 1.0f);
        CHECK(glm::distance(GetBallPlace(world), start) < 1e-3f);

        // Gravity back: it falls.
        settings.Gravity = Wotah::StandardGravity;
        CHECK(world.ApplySettings(settings));
        RunWorld(world, 0.5f);
        CHECK(GetBallPlace(world).y < start.y - 0.5f);

        // A second Create starts over.
        REQUIRE(world.Create());
        CHECK(glm::distance(GetBallPlace(world), start) < 1e-3f);
        world.Destroy();
        world.Destroy(); // twice is safe
    }
}
