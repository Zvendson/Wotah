/**
 * @file physics_tests.cpp
 * The physics world (physics/physics_world.hpp): gravity, landing and resting on the ground, bouncing, balls pushing
 * each other, a box to land on, the turning kinematic cube, steps that do not depend on the frame rate, smooth blending
 * for drawing, and refused input. No GPU needed.
 */
#include "physics/physics_world.hpp"

#include <doctest/doctest.h>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>

namespace
{
    /** The balls' radius, as in the program. */
    constexpr float BallRadius = 0.5f;

    /** How far a resting body may sit off where it should: the world lets bodies sink 5 mm, plus room for rounding. */
    constexpr float RestingSlack = 0.01f;

    /** A flat floor at height 0, 40 x 40 units, two triangles facing up (counter-clockwise seen from above). */
    constexpr std::array<glm::vec3, 4> FloorCorners{
        {{-20.0f, 0.0f, -20.0f}, {20.0f, 0.0f, -20.0f}, {20.0f, 0.0f, 20.0f}, {-20.0f, 0.0f, 20.0f}}};
    constexpr std::array<uint32_t, 6> FloorIndices{{0, 2, 1, 0, 3, 2}};

    /** The floor grips like the default but does not bounce, so a ball bounces only as much as its own surface says. */
    constexpr Wotah::BodySurface FloorSurface{0.5f, 0.0f};

    /** Starts world, with the floor or without it. */
    void SetUpWorld(Wotah::PhysicsWorld& world, bool hasFloor)
    {
        REQUIRE(world.Create());
        if (hasFloor)
        {
            REQUIRE(world.AddTriangles(FloorCorners, FloorIndices, FloorSurface).has_value());
        }
    }

    /** A ball's settings at a place, rubber-like by default, without any damping unless asked. */
    Wotah::BodySettings MakeBall(const glm::vec3& position, float bounciness = 0.0f)
    {
        Wotah::BodySettings ball;
        ball.Position           = position;
        ball.Surface.Bounciness = bounciness;
        ball.SpinDamping        = 0.0f;
        return ball;
    }

    /** Where the body is now, as drawn. */
    glm::vec3 GetPosition(const Wotah::PhysicsWorld& world, Wotah::BodyId body)
    {
        const std::optional<glm::mat4> transform = world.FindTransform(body);
        REQUIRE(transform.has_value());
        return glm::vec3((*transform)[3]);
    }

    /** How many 60-a-second frames fit into seconds. */
    int CountFrames(float seconds)
    {
        return static_cast<int>(std::lround(seconds / Wotah::PhysicsStepSeconds));
    }

    /** Runs the world for seconds at 60 frames a second. */
    void Run(Wotah::PhysicsWorld& world, float seconds)
    {
        for (int frame = 0; frame < CountFrames(seconds); ++frame)
        {
            world.Step(Wotah::PhysicsStepSeconds);
        }
    }
} // namespace

TEST_SUITE("physics")
{
    TEST_CASE("a ball in the air falls with gravity")
    {
        Wotah::PhysicsWorld world;
        SetUpWorld(world, false);
        const std::optional<Wotah::BodyId> ball = world.AddSphere(BallRadius, MakeBall({0.0f, 10.0f, 0.0f}));
        REQUIRE(ball.has_value());

        // Half a second of free fall: 1/2 g t^2 = 1.23 units down, falling at g t = 4.9 units a second. Jolt moves by
        // the new speed each step, which falls a little further (1.27 after 30 steps), so a few centimetres of room.
        Run(world, 0.5f);
        const float fallen = 0.5f * Wotah::StandardGravity * 0.25f;
        CHECK(std::abs(GetPosition(world, *ball).y - (10.0f - fallen)) < 0.05f);
        CHECK(std::abs(world.FindVelocity(*ball)->y + Wotah::StandardGravity * 0.5f) < 0.1f);
        CHECK(GetPosition(world, *ball).x == 0.0f); // straight down
    }

    TEST_CASE("a dropped ball lands on the ground and comes to rest on it, never sinking in")
    {
        Wotah::PhysicsWorld world;
        SetUpWorld(world, true);
        const std::optional<Wotah::BodyId> ball = world.AddSphere(BallRadius, MakeBall({0.0f, 3.0f, 0.0f}));
        REQUIRE(ball.has_value());

        float lowest = std::numeric_limits<float>::max();
        for (int frame = 0; frame < CountFrames(4.0f); ++frame)
        {
            world.Step(Wotah::PhysicsStepSeconds);
            lowest = std::min(lowest, GetPosition(world, *ball).y);
        }
        CHECK(lowest >= BallRadius - RestingSlack);
        CHECK(std::abs(GetPosition(world, *ball).y - BallRadius) <= RestingSlack);
        CHECK(glm::length(*world.FindVelocity(*ball)) < 0.05f);
    }

    TEST_CASE("a bouncy ball bounces back up, a dead one stays down")
    {
        // Dropped from 2 units (its lowest point); a bounciness of 0.9 gives back 0.9 of the speed, so 0.81 of the
        // height.
        for (const float bounciness : {0.9f, 0.0f})
        {
            CAPTURE(bounciness);
            Wotah::PhysicsWorld world;
            SetUpWorld(world, true);
            const std::optional<Wotah::BodyId> ball =
                world.AddSphere(BallRadius, MakeBall({0.0f, 2.0f + BallRadius, 0.0f}, bounciness));
            REQUIRE(ball.has_value());

            bool  hasLanded        = false;
            float highAfterLanding = 0.0f;
            for (int frame = 0; frame < CountFrames(2.0f); ++frame)
            {
                world.Step(Wotah::PhysicsStepSeconds);
                const float height = GetPosition(world, *ball).y - BallRadius;
                hasLanded          = hasLanded || height < RestingSlack;
                highAfterLanding   = hasLanded ? std::max(highAfterLanding, height) : 0.0f;
            }
            REQUIRE(hasLanded);
            if (bounciness > 0.0f)
            {
                CHECK(highAfterLanding > 1.2f);
                CHECK(highAfterLanding < 2.0f); // never higher than it fell
            }
            else
            {
                CHECK(highAfterLanding < RestingSlack);
            }
        }
    }

    TEST_CASE("a ball dropped onto another pushes it aside and glances off; both end on the ground, apart")
    {
        Wotah::PhysicsWorld world;

        // As in the program: one ball on the ground, one dropped over its side; both slow down as on grass.
        SetUpWorld(world, true);
        Wotah::BodySettings lying                = MakeBall({0.0f, BallRadius, 0.0f});
        lying.SpinDamping                        = 1.0f;
        lying.MoveDamping                        = 0.2f;
        Wotah::BodySettings falling              = lying;
        falling.Position                         = {0.3f, 2.0f + BallRadius, 0.3f};
        const std::optional<Wotah::BodyId> below = world.AddSphere(BallRadius, lying);
        const std::optional<Wotah::BodyId> above = world.AddSphere(BallRadius, falling);
        REQUIRE(below.has_value());
        REQUIRE(above.has_value());

        Run(world, 8.0f);
        const glm::vec3 belowNow = GetPosition(world, *below);
        const glm::vec3 aboveNow = GetPosition(world, *above);

        // The hit came from the +x +z side: the lying ball went the other way, the falling one on along its side.
        const glm::vec2 side{1.0f, 1.0f};
        CHECK(glm::dot(glm::vec2(belowNow.x, belowNow.z), side) < -0.05f);
        CHECK(glm::dot(glm::vec2(aboveNow.x - 0.3f, aboveNow.z - 0.3f), side) > 0.05f);

        // Both on the ground, not inside each other, and slowed to (nearly) a stop.
        CHECK(std::abs(belowNow.y - BallRadius) <= RestingSlack);
        CHECK(std::abs(aboveNow.y - BallRadius) <= RestingSlack);
        CHECK(glm::length(aboveNow - belowNow) >= 2.0f * BallRadius - RestingSlack);
        CHECK(glm::length(*world.FindVelocity(*below)) < 0.1f);
        CHECK(glm::length(*world.FindVelocity(*above)) < 0.1f);
    }

    TEST_CASE("a ball dropped onto a fixed box lands on top of it")
    {
        Wotah::PhysicsWorld world;
        SetUpWorld(world, true);
        Wotah::BodySettings box;
        box.Motion   = Wotah::BodyMotion::Static;
        box.Position = {0.0f, 0.5f, 0.0f};
        REQUIRE(world.AddBox(glm::vec3(0.5f), box).has_value());
        const std::optional<Wotah::BodyId> ball = world.AddSphere(BallRadius, MakeBall({0.0f, 3.0f, 0.0f}));
        REQUIRE(ball.has_value());

        Run(world, 3.0f);
        const glm::vec3 position = GetPosition(world, *ball);
        CHECK(std::abs(position.y - (1.0f + BallRadius)) <= RestingSlack); // on the box's top at height 1
        CHECK(std::abs(position.x) < 0.01f);
        CHECK(std::abs(position.z) < 0.01f);
    }

    TEST_CASE("the kinematic cube turns as told, and what hits it does not push it")
    {
        Wotah::PhysicsWorld world;
        SetUpWorld(world, true);
        Wotah::BodySettings cube;
        cube.Motion                            = Wotah::BodyMotion::Kinematic;
        cube.Position                          = {0.0f, 0.5f, 0.0f};
        const std::optional<Wotah::BodyId> box = world.AddBox(glm::vec3(0.5f), cube);
        REQUIRE(box.has_value());
        const float quarterTurn = std::numbers::pi_v<float> * 0.5f;
        REQUIRE(world.SetVelocity(*box, glm::vec3(0.0f), {0.0f, quarterTurn, 0.0f})); // a quarter turn a second

        // A ball lands on its top corner on the way.
        const std::optional<Wotah::BodyId> ball = world.AddSphere(BallRadius, MakeBall({0.35f, 3.0f, 0.35f}));
        REQUIRE(ball.has_value());

        // One second: a quarter turn counter-clockwise seen from above, so its x axis now points to -z. Drawing runs a
        // step behind, so it is a step's turn short.
        Run(world, 1.0f + Wotah::PhysicsStepSeconds);
        const std::optional<glm::mat4> turned = world.FindTransform(*box);
        REQUIRE(turned.has_value());
        CHECK(glm::length(glm::vec3((*turned)[0]) - glm::vec3(0.0f, 0.0f, -1.0f)) < 0.01f);
        CHECK(glm::length(glm::vec3((*turned)[3]) - cube.Position) < 1.0e-4f); // still where it was
    }

    TEST_CASE("the same motion at any frame rate: the world runs in fixed steps")
    {
        // Two seconds of a ball falling and bouncing, at 30 frames a second, at 144, and at uneven frame times.
        std::array<glm::vec3, 3>                  ends{};
        const std::array<std::array<float, 2>, 3> frameTimes{
            {{1.0f / 30.0f, 1.0f / 30.0f}, {1.0f / 144.0f, 1.0f / 144.0f}, {0.004f, 0.029f}}};
        for (std::size_t run = 0; run < frameTimes.size(); ++run)
        {
            Wotah::PhysicsWorld world;
            SetUpWorld(world, true);
            const std::optional<Wotah::BodyId> ball = world.AddSphere(BallRadius, MakeBall({0.0f, 2.5f, 0.0f}, 0.6f));
            REQUIRE(ball.has_value());
            float elapsed = 0.0f;
            for (int frame = 0; elapsed < 2.0f - 1.0e-4f; ++frame)
            {
                const float frameSeconds =
                    std::min(frameTimes[run][static_cast<std::size_t>(frame % 2)], 2.0f - elapsed);
                world.Step(frameSeconds);
                elapsed += frameSeconds;
            }
            ends[run] = GetPosition(world, *ball);
        }
        CHECK(glm::length(ends[0] - ends[1]) < 0.01f);
        CHECK(glm::length(ends[0] - ends[2]) < 0.01f);
    }

    TEST_CASE("drawing blends between the last two steps, so motion is smooth between them")
    {
        Wotah::PhysicsWorld world;
        SetUpWorld(world, false);
        const std::optional<Wotah::BodyId> ball = world.AddSphere(BallRadius, MakeBall({0.0f, 10.0f, 0.0f}));
        REQUIRE(ball.has_value());

        // Two whole steps: drawn where the first step left it (one step behind), the second one known.
        world.Step(Wotah::PhysicsStepSeconds);
        const float firstStep = GetPosition(world, *ball).y; // still the start: the blend starts at the step before
        world.Step(Wotah::PhysicsStepSeconds);
        const float secondStep = GetPosition(world, *ball).y;
        CHECK(firstStep == 10.0f);
        CHECK(secondStep < firstStep);

        // Half a step more: no new step yet, drawn halfway on to where the world already is.
        world.Step(Wotah::PhysicsStepSeconds * 0.5f);
        const float halfway = GetPosition(world, *ball).y;
        CHECK(halfway < secondStep);
        world.Step(Wotah::PhysicsStepSeconds * 0.5f);
        const float thirdStep = GetPosition(world, *ball).y;
        CHECK(halfway > thirdStep);
        CHECK(std::abs((halfway - thirdStep) - (secondStep - halfway)) < 0.05f * (secondStep - halfway)); // mid-step
    }

    TEST_CASE("a long stall does not freeze the program: one call runs at most a limited number of steps")
    {
        Wotah::PhysicsWorld world;
        SetUpWorld(world, false);
        const std::optional<Wotah::BodyId> ball = world.AddSphere(BallRadius, MakeBall({0.0f, 100.0f, 0.0f}));
        REQUIRE(ball.has_value());

        // 100 seconds handed over at once: only MaxStepsPerCall steps (half a second) run, the rest is dropped.
        world.Step(100.0f);
        const float limitSeconds = static_cast<float>(Wotah::PhysicsWorld::MaxStepsPerCall) * Wotah::PhysicsStepSeconds;
        const float fallen       = 100.0f - GetPosition(world, *ball).y;
        CHECK(fallen < 0.5f * Wotah::StandardGravity * limitSeconds * limitSeconds * 1.1f);
        CHECK(fallen > 0.0f);
    }

    TEST_CASE("bad bodies, steps and ids are refused, and several worlds live side by side")
    {
        const float notANumber = std::numeric_limits<float>::quiet_NaN();
        {
            Wotah::PhysicsWorld neverCreated; // destroyed without Create: safe
            CHECK_FALSE(neverCreated.AddSphere(BallRadius, {}).has_value());
            neverCreated.Step(1.0f);
        }

        Wotah::PhysicsWorld world;

        SetUpWorld(world, true);
        Wotah::PhysicsWorld second; // Jolt is started once for both
        SetUpWorld(second, true);
        const Wotah::BodySettings good;
        CHECK_FALSE(world.AddSphere(0.0f, good).has_value());
        CHECK_FALSE(world.AddSphere(-1.0f, good).has_value());
        CHECK_FALSE(world.AddSphere(notANumber, good).has_value());
        CHECK_FALSE(world.AddBox({0.5f, 0.0f, 0.5f}, good).has_value());
        Wotah::BodySettings nowhere;
        nowhere.Position = glm::vec3(notANumber);
        CHECK_FALSE(world.AddSphere(BallRadius, nowhere).has_value());
        Wotah::BodySettings noRotation;
        noRotation.Rotation = glm::quat(0.0f, 0.0f, 0.0f, 0.0f);
        CHECK_FALSE(world.AddBox(glm::vec3(0.5f), noRotation).has_value());

        const std::array<uint32_t, 3> pastTheCorners{{0, 1, 4}};
        const std::array<uint32_t, 4> notTriangles{{0, 1, 2, 3}};
        CHECK_FALSE(world.AddTriangles(FloorCorners, {}, {}).has_value());
        CHECK_FALSE(world.AddTriangles(FloorCorners, pastTheCorners, {}).has_value());
        CHECK_FALSE(world.AddTriangles(FloorCorners, notTriangles, {}).has_value());

        // A fixed body takes no velocity; unknown ids find nothing.
        Wotah::BodySettings fixedBox;
        fixedBox.Motion                          = Wotah::BodyMotion::Static;
        const std::optional<Wotah::BodyId> fixed = world.AddBox(glm::vec3(0.5f), fixedBox);
        REQUIRE(fixed.has_value());
        CHECK_FALSE(world.SetVelocity(*fixed, glm::vec3(1.0f), glm::vec3(0.0f)));
        CHECK_FALSE(world.SetVelocity(Wotah::BodyId{}, glm::vec3(1.0f), glm::vec3(0.0f)));
        CHECK_FALSE(world.FindTransform(Wotah::BodyId{}).has_value());
        CHECK_FALSE(world.FindVelocity(Wotah::BodyId{}).has_value());

        // Bad step times change nothing.
        const std::optional<Wotah::BodyId> ball = world.AddSphere(BallRadius, MakeBall({0.0f, 5.0f, 0.0f}));
        REQUIRE(ball.has_value());
        world.Step(-1.0f);
        world.Step(notANumber);
        world.Step(std::numeric_limits<float>::infinity());
        Run(world, 0.1f);
        CHECK(GetPosition(world, *ball).y > 4.9f); // only the tenth of a second ran

        // The second world still runs after the first is gone.
        world.Destroy();
        world.Destroy(); // twice is safe
        const std::optional<Wotah::BodyId> other = second.AddSphere(BallRadius, MakeBall({0.0f, 5.0f, 0.0f}));
        REQUIRE(other.has_value());
        Run(second, 0.5f);
        CHECK(GetPosition(second, *other).y < 5.0f);
    }
}
