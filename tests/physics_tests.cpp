/**
 * @file physics_tests.cpp
 * The physics world (physics/physics_world.hpp): gravity, landing and resting on the ground (triangles or a grid of
 * heights), bouncing, balls pushing each other, a box to land on, the turning kinematic cube, steps that do not depend
 * on the frame rate, smooth blending for drawing, a world made again from the start, bodies placed back at once,
 * gravity and surfaces changed, and refused input. No GPU needed.
 */
#include "physics/physics_world.hpp"
#include "test_utils.hpp"
#include "water/sea.hpp"

#include <doctest/doctest.h>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <vector>

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

    /**
     * Runs the world for 2 seconds at 60 frames a second and tells how high a ball (BallRadius) rose again after it
     * first touched the floor, measured from the floor to its lowest point; empty when it never landed.
     */
    std::optional<float> FindBounceHeight(Wotah::PhysicsWorld& world, Wotah::BodyId ball)
    {
        bool  hasLanded        = false;
        float highAfterLanding = 0.0f;
        for (int frame = 0; frame < CountFrames(2.0f); ++frame)
        {
            world.Step(Wotah::PhysicsStepSeconds);
            const float height = GetPosition(world, ball).y - BallRadius;
            hasLanded          = hasLanded || height < RestingSlack;
            highAfterLanding   = hasLanded ? std::max(highAfterLanding, height) : 0.0f;
        }
        if (!hasLanded)
        {
            return std::nullopt;
        }
        return highAfterLanding;
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

    TEST_CASE("ground from a grid of heights: a ball lands at the height there and rolls down its slope")
    {
        // A slope rising towards +x by 0.2 per unit, 64 x 64 samples a unit apart, its first corner at
        // (-31.5, 1, -31.5): heights in rows from -z, each from -x, as TerrainPatch keeps them.
        constexpr uint32_t Samples = 64;
        constexpr float    Rise    = 0.2f;
        const glm::vec3    corner{-31.5f, 1.0f, -31.5f};
        std::vector<float> heights(std::size_t{Samples} * Samples);
        for (uint32_t row = 0; row < Samples; ++row)
        {
            for (uint32_t column = 0; column < Samples; ++column)
            {
                heights[std::size_t{row} * Samples + column] = Rise * static_cast<float>(column);
            }
        }
        Wotah::PhysicsWorld world;
        REQUIRE(world.Create());
        REQUIRE(world.AddHeightField(heights, Samples, 1.0f, corner, FloorSurface).has_value());

        // Dropped over the middle, it lands on the slope and rolls downhill, towards -x, resting on the surface: its
        // middle a radius from the slope, which is the radius times sqrt(1 + rise^2) straight above it.
        const std::optional<Wotah::BodyId> ball = world.AddSphere(BallRadius, MakeBall({0.0f, 12.0f, 0.0f}));
        REQUIRE(ball.has_value());
        Run(world, 2.5f);
        const glm::vec3 position = GetPosition(world, *ball);
        const float     ground   = corner.y + Rise * (position.x - corner.x);
        CHECK(position.x < -0.5f);
        CHECK(position.x > corner.x); // still on the slope
        CHECK(std::abs(position.y - BallRadius * std::sqrt(1.0f + Rise * Rise) - ground) < RestingSlack + 0.01f);
    }

    TEST_CASE("a height field needs heights that fit its count, at least 4 to a side, a spacing and numbers only")
    {
        Wotah::PhysicsWorld world;
        REQUIRE(world.Create());
        const std::array<float, 16> heights{};
        CHECK(world.AddHeightField(heights, 4, 1.0f, glm::vec3(0.0f), FloorSurface).has_value());
        CHECK_FALSE(world.AddHeightField(heights, 5, 1.0f, glm::vec3(0.0f), FloorSurface).has_value());
        CHECK_FALSE(world.AddHeightField(std::span(heights).first(9), 3, 1.0f, glm::vec3(0.0f), FloorSurface));
        CHECK_FALSE(world.AddHeightField(heights, 4, 0.0f, glm::vec3(0.0f), FloorSurface).has_value());
        std::array<float, 16> broken{};
        broken[5] = std::numeric_limits<float>::quiet_NaN();
        CHECK_FALSE(world.AddHeightField(broken, 4, 1.0f, glm::vec3(0.0f), FloorSurface).has_value());

        Wotah::PhysicsWorld notCreated;
        CHECK_FALSE(notCreated.AddHeightField(heights, 4, 1.0f, glm::vec3(0.0f), FloorSurface).has_value());
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

            const std::optional<float> bounce = FindBounceHeight(world, *ball);
            REQUIRE(bounce.has_value());
            if (bounciness > 0.0f)
            {
                CHECK(*bounce > 1.2f);
                CHECK(*bounce < 2.0f); // never higher than it fell
            }
            else
            {
                CHECK(*bounce < RestingSlack);
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

    TEST_CASE(
        "creating a world again starts it afresh: nothing of the old one is left, and the same start runs the same")
    {
        // The program's Reset scene: the same world made again must replay exactly.
        Wotah::PhysicsWorld world;
        SetUpWorld(world, true);
        const glm::vec3                    start{0.0f, 5.0f, 0.0f};
        const std::optional<Wotah::BodyId> first = world.AddSphere(BallRadius, MakeBall(start));
        REQUIRE(first.has_value());
        Run(world, 0.5f);
        const glm::vec3 halfwayBefore = GetPosition(world, *first);
        Run(world, 2.0f); // it lands and rests on the floor

        SetUpWorld(world, true);
        const std::optional<Wotah::BodyId> again = world.AddSphere(BallRadius, MakeBall(start));
        REQUIRE(again.has_value());
        CHECK(GetPosition(world, *again).y == start.y); // back at the start, not where the old ball came to rest
        Run(world, 0.5f);
        CHECK(glm::length(GetPosition(world, *again) - halfwayBefore) < 1.0e-5f);

        // The old ball is gone: the new one comes to rest on the floor right below its start, neither on top of the old
        // one nor knocked aside by it.
        Run(world, 2.0f);
        const glm::vec3 rest = GetPosition(world, *again);
        CHECK(std::abs(rest.y - BallRadius) < RestingSlack);
        CHECK(glm::length(glm::vec2(rest.x, rest.z)) < RestingSlack);
    }

    TEST_CASE("placing a body puts it there at once and stops it, so a ball placed back falls as the first time")
    {
        // The program's Reset scene: the balls go back where they started, in the same world.
        Wotah::PhysicsWorld world;
        SetUpWorld(world, true);
        const glm::vec3                    start{0.0f, 5.0f, 0.0f};
        const std::optional<Wotah::BodyId> ball = world.AddSphere(BallRadius, MakeBall(start));
        REQUIRE(ball.has_value());
        Run(world, 0.5f);
        const glm::vec3 halfwayFirst = GetPosition(world, *ball);
        Run(world, 2.0f); // it lands and rests on the floor

        // Drawn right at the start, with no slide from the floor, and standing still.
        world.Step(Wotah::PhysicsStepSeconds * 0.5f); // half a step carried over: drawing blends the last two steps
        REQUIRE(world.PlaceBody(*ball, start, glm::quat(1.0f, 0.0f, 0.0f, 0.0f)));
        CHECK(glm::length(GetPosition(world, *ball) - start) < 1.0e-6f);
        CHECK(glm::length(*world.FindVelocity(*ball)) < 1.0e-6f);

        // It falls as it did the first time; the floor's contact from its rest leaves no trace.
        world.Step(Wotah::PhysicsStepSeconds * 0.5f); // the carried half step completes the first step
        Run(world, 0.5f - Wotah::PhysicsStepSeconds);
        CHECK(glm::length(GetPosition(world, *ball) - halfwayFirst) < 1.0e-3f);

        // A kinematic body loses its velocity too; static and unknown bodies and bad places are refused.
        Wotah::BodySettings turning;
        turning.Motion                             = Wotah::BodyMotion::Kinematic;
        turning.Position                           = {3.0f, 0.5f, 0.0f};
        const std::optional<Wotah::BodyId> spinner = world.AddBox(glm::vec3(0.5f), turning);
        REQUIRE(spinner.has_value());
        REQUIRE(world.SetVelocity(*spinner, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
        Run(world, 0.1f);
        REQUIRE(world.PlaceBody(*spinner, turning.Position, turning.Rotation));
        Run(world, 0.1f);
        CHECK(glm::length(GetPosition(world, *spinner) - turning.Position) < 1.0e-6f);

        Wotah::BodySettings fixedBox;
        fixedBox.Motion                          = Wotah::BodyMotion::Static;
        const std::optional<Wotah::BodyId> fixed = world.AddBox(glm::vec3(0.5f), fixedBox);
        REQUIRE(fixed.has_value());
        const glm::quat noTurn(1.0f, 0.0f, 0.0f, 0.0f);
        CHECK_FALSE(world.PlaceBody(*fixed, start, noTurn));
        CHECK_FALSE(world.PlaceBody(Wotah::BodyId{}, start, noTurn));
        CHECK_FALSE(world.PlaceBody(*ball, glm::vec3(std::numeric_limits<float>::quiet_NaN()), noTurn));
        CHECK_FALSE(world.PlaceBody(*ball, start, glm::quat(0.0f, 0.0f, 0.0f, 0.0f)));
    }

    TEST_CASE("gravity can be changed: 0 lets a ball float, and a ball that fell asleep floating falls once it returns")
    {
        Wotah::PhysicsWorld world;
        SetUpWorld(world, true);
        const glm::vec3                    start{0.0f, 5.0f, 0.0f};
        const std::optional<Wotah::BodyId> ball = world.AddSphere(BallRadius, MakeBall(start));
        REQUIRE(ball.has_value());

        // Without gravity it hangs where it is, long enough for Jolt to put it to sleep (half a second of rest).
        REQUIRE(world.SetGravity(0.0f));
        Run(world, 2.0f);
        CHECK(glm::length(GetPosition(world, *ball) - start) < 1.0e-5f);

        // Twice the standard pull wakes it: half a second of fall is 1/2 (2 g) t^2 = 2.45 units. Jolt's steps fall 2.37
        // here, as the new pull shows from the second step on (measured the same for a ball that never slept), so the
        // check allows one step's worth of fall either way. Left asleep, the ball would not fall at all (measured: 0).
        const float pull = 2.0f * Wotah::StandardGravity;
        REQUIRE(world.SetGravity(pull));
        Run(world, 0.5f);
        const float fallen      = start.y - GetPosition(world, *ball).y;
        const float expected    = 0.5f * pull * 0.25f;
        const float oneStepFall = pull * Wotah::PhysicsStepSeconds * 0.5f; // a step's worth at half a second's speed
        CHECK(std::abs(fallen - expected) < oneStepFall);

        // A pull below 0 or not a number is refused, as is any pull before the world is made.
        CHECK_FALSE(world.SetGravity(-1.0f));
        CHECK_FALSE(world.SetGravity(std::numeric_limits<float>::quiet_NaN()));
        Wotah::PhysicsWorld neverCreated;
        CHECK_FALSE(neverCreated.SetGravity(Wotah::StandardGravity));
    }

    TEST_CASE("a body's surface can be changed: a dead ball made bouncy bounces; bad surfaces and bodies are refused")
    {
        Wotah::PhysicsWorld world;
        SetUpWorld(world, true);
        const glm::vec3                    start{0.0f, 2.0f + BallRadius, 0.0f};
        const std::optional<Wotah::BodyId> ball = world.AddSphere(BallRadius, MakeBall(start, 0.0f));
        REQUIRE(ball.has_value());
        const std::optional<float> deadBounce = FindBounceHeight(world, *ball);
        REQUIRE(deadBounce.has_value());
        CHECK(*deadBounce < RestingSlack);

        // Bouncy now, as the settings window's slider makes the program's balls: dropped again, it bounces back up.
        Wotah::BodySurface bouncy;
        bouncy.Bounciness = 0.9f;
        REQUIRE(world.SetSurface(*ball, bouncy));
        REQUIRE(world.PlaceBody(*ball, start, glm::quat(1.0f, 0.0f, 0.0f, 0.0f)));
        const std::optional<float> bouncyBounce = FindBounceHeight(world, *ball);
        REQUIRE(bouncyBounce.has_value());
        CHECK(*bouncyBounce > 1.2f);

        Wotah::BodySurface noGrip;
        noGrip.Friction = -0.1f;
        Wotah::BodySurface noNumber;
        noNumber.Bounciness = std::numeric_limits<float>::quiet_NaN();
        CHECK_FALSE(world.SetSurface(*ball, noGrip));
        CHECK_FALSE(world.SetSurface(*ball, noNumber));
        CHECK_FALSE(world.SetSurface(Wotah::BodyId{}, bouncy));
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

    TEST_CASE("in the sea a buoyant ball floats a third under, a heavy one sinks to the floor; the world keeps a clock")
    {
        // The floor at 0, the sea 3 deep over it, still. Three balls dropped from a metre over the water: one that
        // floats (buoyancy 3), one as heavy as water three times over (buoyancy 1/3) and one the water leaves alone.
        Wotah::PhysicsWorld world;
        CHECK_FALSE(world.SetSea(Wotah::Testing::MakeStillSea(3.0f))); // refused (logged) before Create
        CHECK(world.GetSeconds() == 0.0);
        SetUpWorld(world, true);
        CHECK(world.SetSea(Wotah::Testing::MakeStillSea(3.0f)));
        Wotah::BodySettings floating               = MakeBall({-4.0f, 4.5f, 0.0f});
        floating.Buoyancy                          = 3.0f;
        Wotah::BodySettings heavy                  = MakeBall({0.0f, 4.5f, 0.0f});
        heavy.Buoyancy                             = 1.0f / 3.0f;
        const Wotah::BodySettings          dry     = MakeBall({4.0f, 4.5f, 0.0f});
        const std::optional<Wotah::BodyId> floater = world.AddSphere(BallRadius, floating);
        const std::optional<Wotah::BodyId> sinker  = world.AddSphere(BallRadius, heavy);
        const std::optional<Wotah::BodyId> stone   = world.AddSphere(BallRadius, dry);
        REQUIRE(floater.has_value());
        REQUIRE(sinker.has_value());
        REQUIRE(stone.has_value());
        Run(world, 10.0f);

        // A third of a ball's volume is a cap 0.387 of its width deep (h^2 (3r - h) = 4r^3 / 3): its middle rests
        // 0.113 over the water. The others lie on the floor.
        const glm::vec3 afloat = GetPosition(world, *floater);
        CAPTURE(afloat.y);
        CHECK(std::abs(afloat.y - (3.0f + BallRadius - 0.387f * 2.0f * BallRadius)) < 0.03f);
        CHECK(std::abs(GetPosition(world, *sinker).y - BallRadius) < RestingSlack);
        CHECK(std::abs(GetPosition(world, *stone).y - BallRadius) < RestingSlack);

        // The clock: 10 seconds of steps, as drawn one step behind.
        CHECK(world.GetSeconds() ==
              doctest::Approx(10.0 - static_cast<double>(Wotah::PhysicsStepSeconds)).epsilon(1e-6));
    }

    TEST_CASE("on waves a floating ball rides up and down with the surface, moved by its water")
    {
        Wotah::PhysicsWorld world;
        SetUpWorld(world, true);
        Wotah::Sea sea = Wotah::Testing::MakeStillSea(3.0f);
        sea.Waves      = Wotah::MakeSeaWaves(0.8f, {1.0f, 0.0f});
        REQUIRE(world.SetSea(sea));
        Wotah::BodySettings floating            = MakeBall({0.0f, 3.2f, 0.0f});
        floating.Buoyancy                       = 3.0f;
        const std::optional<Wotah::BodyId> ball = world.AddSphere(BallRadius, floating);
        REQUIRE(ball.has_value());
        Run(world, 4.0f);

        // Over ten seconds its middle stays near its still-water height over the surface under it (0.113), while the
        // surface lifts and drops it; and it moves, back and forth with the water.
        float lowest   = 1.0e9f;
        float highest  = -1.0e9f;
        float farthest = 0.0f;
        for (int sample = 0; sample < 40; ++sample)
        {
            Run(world, 0.25f);
            const glm::vec3 place = GetPosition(world, *ball);
            sea.Seconds           = world.GetSeconds();
            const float surface   = Wotah::FindWaterHeight(sea, place.x, place.z).value_or(0.0f);
            lowest                = std::min(lowest, place.y);
            highest               = std::max(highest, place.y);
            farthest              = std::max(farthest, std::abs(place.y - surface - 0.113f));
        }
        CAPTURE(lowest);
        CAPTURE(highest);
        CAPTURE(farthest);
        CHECK(highest - lowest > 0.2f);
        CHECK(farthest < 0.25f);
    }

    TEST_CASE("a floating ball is carried back and forth by the water under a long swell, not only lifted")
    {
        // A swell 1.6 m high, 8 s long, over deep water: the water at the surface runs back and forth with each wave,
        // about its height's half each way. A floating ball goes with it (Jolt's drag towards the water's speed).
        Wotah::PhysicsWorld world;
        SetUpWorld(world, false);
        Wotah::Sea sea         = Wotah::Testing::MakeStillSea(0.0f);
        sea.Waves.Swell.Height = 1.6f;
        sea.Waves.Swell.Period = 8.0f;
        REQUIRE(world.SetSea(sea));
        REQUIRE(world.SetGravity(Wotah::StandardGravity));
        Wotah::BodySettings floating            = MakeBall({0.0f, 0.2f, 0.0f});
        floating.Buoyancy                       = 3.0f;
        const std::optional<Wotah::BodyId> ball = world.AddSphere(BallRadius, floating);
        REQUIRE(ball.has_value());
        Run(world, 8.0f);
        float west = 1.0e9f;
        float east = -1.0e9f;
        for (int sample = 0; sample < 64; ++sample)
        {
            Run(world, 0.25f);
            const float x = GetPosition(world, *ball).x;
            west          = std::min(west, x);
            east          = std::max(east, x);
        }
        CAPTURE(west);
        CAPTURE(east);
        CHECK(east - west > 0.6f);
        CHECK(east - west < 3.0f); // with it, not thrown about
    }
}
