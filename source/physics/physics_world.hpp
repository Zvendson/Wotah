/**
 * @file physics_world.hpp
 * Gravity and collisions, on top of Jolt Physics: bodies (spheres, boxes, fixed ground as triangles or as a grid of
 * heights) that fall, bounce, roll and push each other. One world unit is one metre, and y is up.
 *
 *     Wotah::PhysicsWorld physics;
 *     if (physics.Create())
 *     {
 *         physics.AddHeightField(heights, 256, 1.0f, corner, sand);   // fixed in place
 *         Wotah::BodySettings ball;
 *         ball.Position = {0.0f, 3.0f, 0.0f};                        // starts in the air
 *         const std::optional<Wotah::BodyId> id = physics.AddSphere(0.5f, ball);
 *
 *         physics.Step(frameSeconds);                                 // every frame
 *         const std::optional<glm::mat4> model = physics.FindTransform(*id);
 *     }
 *
 * The world runs in fixed steps (PhysicsStepSeconds), whatever the frame rate, so it behaves the same on every machine;
 * FindTransform blends between the last two steps, so what is drawn moves smoothly. Jolt stays inside
 * physics_world.cpp. One thread uses a world at a time; within a step, the world shares its work out over worker
 * threads of its own (Jolt's, which also sample the water under floating bodies) and the result is the same as on one.
 */
#pragma once

#include <glm/ext/quaternion_float.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace Wotah
{
    struct Sea;

    /** The pull of gravity in metres per second squared: standard gravity, 9.80665 (ISO 80000-3). */
    inline constexpr float StandardGravity = 9.80665f;

    /**
     * How long one physics step is: 1/60 s. Small enough that a falling ball moves well under its own size per step,
     * the step size Jolt is tuned for (Jolt docs, "PhysicsSystem::Update").
     */
    inline constexpr float PhysicsStepSeconds = 1.0f / 60.0f;

    /** How a body moves. */
    enum class BodyMotion
    {
        /** Never moves: the ground, walls. */
        Static,
        /** Moves only as told (SetVelocity) and pushes what is in its way; nothing pushes it: a turning platform. */
        Kinematic,
        /** Moved by gravity and everything that hits it: a ball. */
        Dynamic,
    };

    /** What a body's surface does in a contact. */
    struct BodySurface
    {
        /**
         * How much it grips: 0 slides like ice, 1 grips like rubber on concrete. Two surfaces in contact grip by the
         * square root of the product of theirs (Jolt's default).
         */
        float Friction = 0.5f;
        /**
         * How much it bounces back (restitution): 0 stops dead, 1 bounces back as high as it fell. Two surfaces in
         * contact bounce by the larger of theirs (Jolt's default).
         */
        float Bounciness = 0.3f;
    };

    /** Where a new body starts, how it moves and what its surface is like. */
    struct BodySettings
    {
        BodyMotion Motion = BodyMotion::Dynamic;
        /** Where its middle starts, in world units. */
        glm::vec3 Position{0.0f};
        /** How it is turned at the start. */
        glm::quat   Rotation{1.0f, 0.0f, 0.0f, 0.0f};
        BodySurface Surface;
        /**
         * How quickly moving and spinning die down by themselves, per second (Jolt's damping: dv/dt = -c v). Moving: 0,
         * as air barely slows a ball; spinning: a little, so a rolling ball comes to rest some time.
         */
        float MoveDamping = 0.0f;
        float SpinDamping = 0.05f;
        /**
         * How it floats in the sea (SetSea): water as dense as Buoyancy times the body would be (Jolt's buoyancy: 1
         * hovers, below 1 sinks, 3 floats with a third under the water); 0 leaves it untouched by the water.
         */
        float Buoyancy = 0.0f;
    };

    /** Names a body of a PhysicsWorld. */
    struct BodyId
    {
        /** Jolt's body number with its reuse counter; meaningless outside the world that gave it. */
        uint32_t Value = UINT32_MAX;

        friend bool operator==(const BodyId&, const BodyId&) = default;
    };

    /**
     * A world of bodies under gravity. The destructor runs Destroy, so nothing leaks when Create fails part way.
     * Several worlds may exist at once.
     */
    class PhysicsWorld final
    {
    public:
        PhysicsWorld();
        ~PhysicsWorld();

        PhysicsWorld(const PhysicsWorld&)            = delete;
        PhysicsWorld& operator=(const PhysicsWorld&) = delete;

        /** Starts an empty world with gravity pulling down (-y); false (and logged) when that fails. */
        [[nodiscard]] bool Create();

        /** Removes every body and stops the world; safe to call more than once or after a failed Create. */
        void Destroy();

        /** Adds a ball; empty (and logged) for a radius that is not above 0, bad settings or a full world. */
        [[nodiscard]] std::optional<BodyId> AddSphere(float radius, const BodySettings& settings);

        /**
         * Adds a box reaching halfSize to each side of its middle; empty (and logged) for a size not above 0, bad
         * settings or a full world.
         */
        [[nodiscard]] std::optional<BodyId> AddBox(const glm::vec3& halfSize, const BodySettings& settings);

        /**
         * Adds a fixed body made of triangles (three indices into corners each, counter-clockwise seen from the side
         * that faces out), such as the ground. Empty (and logged) for no triangles, an index past the corners, or a
         * full world.
         */
        [[nodiscard]] std::optional<BodyId> AddTriangles(std::span<const glm::vec3> corners,
                                                         std::span<const uint32_t> indices, const BodySurface& surface);

        /**
         * Adds fixed ground from heights on a square grid: samplesPerSide x samplesPerSide of them, spacing apart, the
         * first at corner (its y is added to every height), row by row from -z to +z, each from -x to +x (as
         * terrain/terrain_shape.hpp's TerrainPatch). Jolt keeps the heights packed to a few millimetres. Empty (and
         * logged) for a count that does not fit the heights or is below 4, a spacing not above 0, a height that is not
         * a number, or a full world.
         */
        [[nodiscard]] std::optional<BodyId> AddHeightField(std::span<const float> heights, uint32_t samplesPerSide,
                                                           float spacing, const glm::vec3& corner,
                                                           const BodySurface& surface);

        /**
         * Takes a body out of the world for good; its id names nothing from now on. False (and logged) for an unknown
         * body.
         */
        bool RemoveBody(BodyId body);

        /**
         * Sets how fast a moving body moves (world units per second) and spins (radians per second around each axis);
         * a kinematic body keeps that until told otherwise. False (and logged) for a static or unknown body.
         */
        bool SetVelocity(BodyId body, const glm::vec3& moving, const glm::vec3& spinning);

        /**
         * Gives the body's surface new grip and bounciness from now on. False (and logged) for an unknown body or
         * values that are below 0 or not numbers.
         */
        bool SetSurface(BodyId body, const BodySurface& surface);

        /**
         * Sets gravity's pull straight down, in metres per second squared (Create starts with StandardGravity); 0 lets
         * everything float. Wakes every moving body, so resting ones feel the change. False (and logged) for a pull
         * below 0 or not a number, or before Create.
         */
        bool SetGravity(float pull);

        /**
         * Puts a moving body at a place and turn at once and stops it, as if it had stood there all along: drawing does
         * not slide it over from where it was. A kinematic body loses its velocity too; give it again with SetVelocity.
         * False (and logged) for a static or unknown body, or a place or turn that is not made of numbers.
         */
        bool PlaceBody(BodyId body, const glm::vec3& position, const glm::quat& rotation);

        /**
         * Lets the world run on by seconds (a frame's time): as many fixed steps as fit, the rest carried over to the
         * next call. At most MaxStepsPerCall steps per call, so a long stall does not freeze the program catching up;
         * the world then runs slower for a moment. Negative or not-a-number times are logged and ignored.
         */
        void Step(float seconds);

        /**
         * Where the body is and how it is turned, as a model matrix for drawing: blended between the last two steps by
         * the time carried over, so it moves smoothly at any frame rate. Empty for an unknown body.
         */
        [[nodiscard]] std::optional<glm::mat4> FindTransform(BodyId body) const;

        /** How fast the body moves, in world units per second, as of the last step; empty for an unknown body. */
        [[nodiscard]] std::optional<glm::vec3> FindVelocity(BodyId body) const;

        /**
         * Lets bodies with a Buoyancy float in sea from the next step on: each step lifts them by the water they push
         * aside where the surface is at that step (water/sea.hpp, its waves at the world's clock, GetSeconds; the
         * sea's own Seconds is not used), and the water drags them along with it. False (and logged) before Create.
         */
        bool SetSea(const Sea& sea);

        /**
         * The world's clock as FindTransform shows it: the seconds of every step so far, less the part of the last step
         * the blend has not reached yet. 0 before the first step.
         */
        [[nodiscard]] double GetSeconds() const;

        /** The most fixed steps one Step call runs: half a second of physics at 60 steps a second. */
        static constexpr int MaxStepsPerCall = 30;

    private:
        /** Jolt's objects; defined in physics_world.cpp, so this header stays free of Jolt. */
        struct State;
        std::unique_ptr<State> m_state;
    };
} // namespace Wotah
