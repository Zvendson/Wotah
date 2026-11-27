/**
 * @file beach_tests.cpp
 * The example scene (scene/beach.hpp): the start view over the sand, the start settings, and the beach's physics: the
 * ball dropped into the shallow water comes to rest on the sand under it, Reset puts it back into the air, the
 * settings reach the world, the surf carries the ball in; more balls dropped where a ray points, picked by a ray,
 * held in place. No GPU needed.
 */
#include "scene/beach.hpp"

#include <doctest/doctest.h>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>

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

    /** Where the first ball's middle is. */
    glm::vec3 GetBallPlace(const Wotah::BeachWorld& world)
    {
        return glm::vec3(world.GetBallTransform(0)[3]);
    }

    /** Where a ball's middle is. */
    glm::vec3 GetBallPlace(const Wotah::BeachWorld& world, std::size_t ball)
    {
        return glm::vec3(world.GetBallTransform(ball)[3]);
    }

    /** Straight up: a ray into the sky, which meets neither land nor water. */
    constexpr glm::vec3 Skywards{0.0f, 1.0f, 0.0f};
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
        CHECK(world.GetBallTransform(0) == glm::mat4(1.0f)); // nothing yet
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

    TEST_CASE("more balls: each drops from the air where the view points and floats; Reset takes them away again")
    {
        Wotah::BeachWorld world;
        REQUIRE(world.Create());
        REQUIRE(world.ApplySettings(Wotah::MakeBeachSettings()));
        CHECK(world.GetBallCount() == 1);
        const glm::vec3 start = GetBallPlace(world);

        // From the start view, its middle points at the shallow water: the new ball drops over it, from the air.
        const Wotah::Camera camera = Wotah::MakeBeachCamera();
        REQUIRE(world.DropBall(camera.GetPosition(), camera.GetPictureDirection({0.5f, 0.5f})));
        REQUIRE(world.GetBallCount() == 2);
        const glm::vec3 dropped = GetBallPlace(world, 1);
        const glm::vec3 target  = camera.GetTarget();
        CAPTURE(dropped.x);
        CAPTURE(dropped.z);
        CHECK(glm::length(glm::vec2(dropped.x - target.x, dropped.z - target.z)) < 4.0f);
        CHECK(dropped.y > Wotah::BeachSea.Level + 3.0f);

        // It falls into the water and floats there, as the first ball does.
        RunWorld(world, 15.0f);
        const glm::vec3            afloat = GetBallPlace(world, 1);
        const std::optional<float> water  = Wotah::FindWaterHeight(world.GetSea(), afloat.x, afloat.z);
        REQUIRE(water.has_value());
        CAPTURE(afloat.y);
        CAPTURE(*water);
        CHECK(std::abs(afloat.y - *water) < Wotah::BeachBallRadius);

        // Reset: one ball again, back in the air where it started; the dropped one is gone.
        REQUIRE(world.Reset());
        CHECK(world.GetBallCount() == 1);
        CHECK(glm::length(GetBallPlace(world) - start) < 1.0e-4f);
        CHECK(world.GetBallTransform(1) == glm::mat4(1.0f));

        // And more can be dropped after it.
        CHECK(world.DropBall(camera.GetPosition(), camera.GetPictureDirection({0.5f, 0.5f})));
        CHECK(world.GetBallCount() == 2);
    }

    TEST_CASE("a ray into the sky drops the ball some way ahead over the ground; the beach holds BeachMaxBalls balls")
    {
        Wotah::BeachWorld world;
        CHECK_FALSE(world.DropBall({0.0f, 5.0f, 0.0f}, Skywards)); // not created yet
        REQUIRE(world.Create());

        // Up and to the west: ahead along its way over the ground, from the air above the water there.
        const glm::vec3 origin{-20.0f, 3.0f, 4.0f};
        REQUIRE(world.DropBall(origin, glm::normalize(glm::vec3(-1.0f, 1.0f, 0.0f))));
        const glm::vec3 ahead = GetBallPlace(world, 1);
        CHECK(std::abs(ahead.z - origin.z) < 1.0e-3f);
        CHECK(ahead.x < origin.x - 5.0f);
        CHECK(ahead.y > Wotah::BeachSea.Level + 3.0f);

        // Filled up, the next is refused and the count stays.
        while (world.GetBallCount() < Wotah::BeachMaxBalls)
        {
            REQUIRE(world.DropBall(origin, Skywards));
        }
        CHECK_FALSE(world.DropBall(origin, Skywards));
        CHECK(world.GetBallCount() == Wotah::BeachMaxBalls);
    }

    TEST_CASE("a ray picks the nearest ball it passes through, and none when it passes them all")
    {
        Wotah::BeachWorld world;
        CHECK_FALSE(world.FindBallAt({0.0f, 0.0f, 0.0f}, Skywards).has_value()); // no balls yet
        REQUIRE(world.Create());
        REQUIRE(world.DropBall({-20.0f, 3.0f, 0.0f}, Skywards));
        REQUIRE(world.PlaceBall(0, {0.0f, 10.0f, 0.0f}));
        REQUIRE(world.PlaceBall(1, {-5.0f, 10.0f, 0.0f}));

        // Along the row of both, from either end: the nearer one.
        CHECK(world.FindBallAt({10.0f, 10.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}) == std::optional<std::size_t>(0));
        CHECK(world.FindBallAt({-10.0f, 10.0f, 0.0f}, {1.0f, 0.0f, 0.0f}) == std::optional<std::size_t>(1));

        // Across one of them, near its edge; past both, just over their tops; away from them.
        CHECK(world.FindBallAt({-5.0f, 10.4f, 5.0f}, {0.0f, 0.0f, -1.0f}) == std::optional<std::size_t>(1));
        CHECK_FALSE(world.FindBallAt({10.0f, 10.6f, 0.0f}, {-1.0f, 0.0f, 0.0f}).has_value());
        CHECK_FALSE(world.FindBallAt({5.0f, 10.0f, 0.0f}, {1.0f, 0.0f, 0.0f}).has_value());
    }

    TEST_CASE("a ball put somewhere after every step hangs there as the world runs, turned as it was; let go it falls")
    {
        Wotah::BeachWorld world;
        REQUIRE(world.Create());
        RunWorld(world, 0.5f);
        const glm::mat4 before = world.GetBallTransform(0);

        // Held 10 m over the beach for a second, as the move gizmo holds it: the world steps, then it is put back.
        const glm::vec3 held{2.0f, 10.0f, 3.0f};
        for (int step = 0; step < 60; ++step)
        {
            world.Step(StepSeconds, Wotah::MakeBeachCamera().GetPosition());
            REQUIRE(world.PlaceBall(0, held));
            CHECK(glm::length(GetBallPlace(world) - held) < 1.0e-4f);
        }
        const glm::mat4 after = world.GetBallTransform(0);
        for (int column = 0; column < 3; ++column)
        {
            CHECK(glm::length(glm::vec3(after[column]) - glm::vec3(before[column])) < 1.0e-3f); // not turned round
        }

        // Let go, it falls from there as from rest: about half of g t^2 in half a second (1.2 m).
        RunWorld(world, 0.5f);
        const float fallen = held.y - GetBallPlace(world).y;
        CAPTURE(fallen);
        CHECK(fallen > 1.0f);
        CHECK(fallen < 1.4f);

        // A ball that is not there, or a place that is no place, is refused.
        CHECK_FALSE(world.PlaceBall(1, held));
        CHECK_FALSE(world.PlaceBall(0, {std::nanf(""), 0.0f, 0.0f}));
    }
}
