/**
 * @file beach_tests.cpp
 * The example scene (scene/beach.hpp): the start view over the sand, the start settings, and the beach's physics: the
 * ball dropped into the shallow water comes to rest on the sand under it, Reset puts it back into the air, the
 * settings reach the world. No GPU needed.
 */ \
#include "scene/beach.hpp"

#include <doctest/doctest.h>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <cmath>

namespace
{
    /** One physics step: the world's fixed step (physics/physics_world.hpp). */
    constexpr float StepSeconds = 1.0f / 60.0f;

    /** Moves the world on by seconds in single steps, the viewer far off at the start camera. */
    void RunWorld(Wotah::BeachWorld& world, float seconds)
    {
        const glm::vec3 viewer = Wotah::MakeBeachCamera().GetPosition();
        const long      steps  = std::lround(seconds / StepSeconds);
        for (long step = 0; step < steps; ++step)
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

    TEST_CASE("the ball drops into the shallow water and floats there, a third under; Reset puts it back in the air")
    {
        Wotah::BeachWorld world;
        CHECK(world.GetBallTransform() == glm::mat4(1.0f)); // nothing yet
        REQUIRE(world.Create());

        // It starts over the water, well above it.
        const glm::vec3 start = GetBallPlace(world);
        CHECK(start.y > Wotah::BeachSea.Level + Wotah::BeachBallRadius + 1.0f);
        CHECK(Wotah::GetTerrainHeight(Wotah::BeachLand, {start.x, start.z}) < Wotah::BeachSea.Level);

        // A few seconds on (the sea still: no settings yet), it floats where it fell, clear of the floor, a third of
        // it under the water: a cap of a third of a ball's volume is 0.387 of its width deep (h^2 (3r - h) = 4r^3/3).
        // Once, without water physics, it sank to the floor and rolled down its slope out to sea for ever.
        RunWorld(world, 6.0f);
        const glm::vec3 floating = GetBallPlace(world);
        const float     floor    = Wotah::GetTerrainHeight(Wotah::BeachLand, {floating.x, floating.z});
        const float     under    = Wotah::BeachSea.Level - (floating.y - Wotah::BeachBallRadius);
        CAPTURE(floating.x);
        CAPTURE(floating.y);
        CAPTURE(floating.z);
        CAPTURE(under);
        CHECK(std::abs(under - 0.387f * 2.0f * Wotah::BeachBallRadius) < 0.05f);
        CHECK(floating.y - Wotah::BeachBallRadius > floor + 0.1f);
        CHECK(glm::distance(glm::vec2(floating.x, floating.z), glm::vec2(start.x, start.z)) < 0.5f);
        RunWorld(world, 2.0f);
        CHECK(glm::distance(GetBallPlace(world), floating) < 0.05f); // it stays

        // Back in the air at once, and it falls again.
        CHECK(world.Reset());
        CHECK(glm::distance(GetBallPlace(world), start) < 1e-3f);
        RunWorld(world, 0.5f);
        CHECK(GetBallPlace(world).y < start.y - 0.5f);
    }

    TEST_CASE("on the start's waves the ball rides up and down with the surface under it, the sea's clock the world's")
    {
        Wotah::BeachWorld world;
        REQUIRE(world.Create());
        CHECK(world.ApplySettings(Wotah::MakeBeachSettings()));
        REQUIRE(world.GetSea().Waves.Count > 0);
        RunWorld(world, 6.0f);

        // Over the next ten seconds: the ball's middle stays about as high over the surface under it as in still water
        // (0.113 m), while the surface lifts and drops it by more than a hand's width.
        float lowest   = 1.0e9f;
        float highest  = -1.0e9f;
        float farthest = 0.0f;
        for (int sample = 0; sample < 40; ++sample)
        {
            RunWorld(world, 0.25f);
            const glm::vec3  ball    = GetBallPlace(world);
            const Wotah::Sea sea     = world.GetSea();
            const float      surface = Wotah::FindWaterHeight(sea, ball.x, ball.z).value_or(0.0f);
            lowest                   = std::min(lowest, ball.y);
            highest                  = std::max(highest, ball.y);
            farthest                 = std::max(farthest, std::abs(ball.y - surface - 0.113f));
        }
        CAPTURE(lowest);
        CAPTURE(highest);
        CAPTURE(farthest);
        CHECK(highest - lowest > 0.1f);
        CHECK(farthest < 0.2f);

        // The sea's clock is the world's: 6 + 10 seconds of steps, less at most one step the drawing still blends.
        const double seconds = world.GetSea().Seconds;
        CHECK(seconds > 16.0 - 2.0 / 60.0);
        CHECK(seconds <= 16.0 + 1.0e-6);
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

    TEST_CASE("the surf carries the ball in: on 1 m waves it drifts to the beach and the swash moves it about")
    {
        // The ball dropped 16 m out on 1 m waves: the bores' rollers bring it in (what floats goes in with them; the
        // water they bring flows back out along the floor), until the swash runs it up the sand and back. Once the
        // water under the surf ran in and out alike and stood still on the swash's sheet: the ball drifted back out.
        Wotah::BeachWorld world;
        REQUIRE(world.Create());
        Wotah::SceneSettings settings = Wotah::MakeBeachSettings();
        settings.WaveHeight           = 1.0f;
        REQUIRE(world.ApplySettings(settings));
        const glm::vec3 start = GetBallPlace(world);
        float           most  = start.x;
        float           least = 1.0e9f;
        float           late  = -1.0e9f;
        for (int sample = 0; sample < 240; ++sample)
        {
            RunWorld(world, 0.5f);
            const glm::vec3 ball = GetBallPlace(world);
            most                 = std::max(most, ball.x);
            if (sample >= 120)
            {
                least = std::min(least, ball.x); // in the second minute
                late  = std::max(late, ball.x);
            }
        }
        CAPTURE(late - least);
        const float     shoreward = most - start.x;
        const glm::vec3 now       = GetBallPlace(world);
        const float     ground    = Wotah::GetTerrainHeight(Wotah::BeachLand, {now.x, now.z});
        CAPTURE(shoreward);
        CAPTURE(least);
        CAPTURE(now.x);
        CAPTURE(ground);
        CHECK(shoreward > 10.0f);                     // it came in to the waterline (16 m out at the start)
        CHECK(least > start.x + 10.0f);               // and stays there, in the swash, not back out at sea
        CHECK(ground > Wotah::BeachSea.Level - 0.8f); // near the beach
    }
}
