/**
 * @file beach.hpp
 * The example scene: a sandy beach along a wandering coast with dunes and green hills behind it (terrain/), the sea in
 * front to the west, where the sun sets, its waves rolling in onto the beach, and a blue ball dropped from the air into
 * the shallow water, where it floats; more balls can be dropped in, picked by a ray and held in place. Here are its
 * land, its sea, the view and the settings it starts with, and its moving part, BeachWorld: the physics that drops the
 * balls, floats them on the waves and lets them roll where they land on sand. How it is drawn is up to the caller
 * (app.cpp); nothing here knows Vulkan.
 *
 *     Wotah::BeachWorld world;
 *     world.Create();
 *     world.DropBall(camera.GetPosition(), camera.GetPictureDirection({0.5f, 0.5f})); // one more where the view points
 *     world.Step(seconds, camera.GetPosition());                                      // each frame
 *     for (std::size_t ball = 0; ball < world.GetBallCount(); ++ball)
 *     {
 *         const glm::mat4 model = world.GetBallTransform(ball);
 *     }
 *     const Wotah::Sea sea = world.GetSea(); // its waves where the balls see them
 */
#pragma once

#include "physics/land_collider.hpp"
#include "physics/physics_world.hpp"
#include "render/camera.hpp"
#include "terrain/terrain_shape.hpp"
#include "ui/settings_window.hpp"
#include "water/sea.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <optional>
#include <vector>

namespace Wotah
{
    /**
     * The land: the beach the default TerrainShape makes, its waterline through the origin, the sea to the west (-x).
     * The sea's still surface lies at the beach's sea level, the land its floor; its waves come from the settings.
     */
    inline constexpr TerrainShape BeachLand{};
    inline constexpr Sea          BeachSea{BeachLand.SeaLevel, SeaWaves{}, 0.0, BeachLand};

    /** The ball's radius: half a unit across. */
    inline constexpr float BeachBallRadius = 0.5f;

    /**
     * The most balls the beach holds at once, the first included: plenty to play with, while the physics stays well
     * under a millisecond a step. Measured (Release, all afloat on 3 m waves): 0.35 ms a step with 32, 0.013 ms with 1;
     * each ball samples the waves every step.
     */
    inline constexpr std::size_t BeachMaxBalls = 32;

    /**
     * The view the scene starts from: over the dry beach, 4 units above the sand (as from a lifeguard's tower), looking
     * west-south-west across the waterline into the shallow water where the ball falls in, with the coast running
     * across the picture and the evening sun setting into the sea left of the middle.
     */
    [[nodiscard]] Camera MakeBeachCamera();

    /**
     * The settings as the scene starts: 9 in the morning on a day that runs, the sun reaching 60 degrees at noon,
     * normal gravity, real time, the ball's rubber bounce, gentle waves, every camera's field of view.
     */
    [[nodiscard]] SceneSettings MakeBeachSettings();

    /**
     * The beach's physics: the land solid around the balls and the camera wherever they go, the sea with its waves, and
     * the balls, which float on them. Balls are counted from 0 in the order they came; the first is the one the scene
     * starts with. The destructor frees everything.
     */
    class BeachWorld final
    {
    public:
        /**
         * Makes the world with the ball in the air over the water and the land under it already solid, the sea still
         * until ApplySettings; false (and logged) when a part fails.
         */
        [[nodiscard]] bool Create();

        /** Frees the world; safe to call more than once or after a failed Create. */
        void Destroy();

        /**
         * Moves the world on by seconds: first the land turns solid around the balls and the viewer (the camera, so
         * whatever it may drop on lands), then the physics steps.
         */
        void Step(float seconds, const glm::vec3& viewer);

        /**
         * Puts the scene back as it started: the balls dropped in since are gone, the first is back in the air where
         * it started, standing still, at once and without making anything again; false (and logged) when that failed.
         */
        bool Reset();

        /** Puts the settings' gravity, balls' bounce and waves into the world; false (and logged) when that failed. */
        bool ApplySettings(const SceneSettings& settings);

        /**
         * Drops one more ball, from as high as the first one starts above the water, over where a ray (from origin
         * along direction, of length 1) first meets the land or the water within a few hundred metres; a ray that
         * meets neither (into the sky) drops it some way ahead over the ground. False (and logged) once the beach
         * holds BeachMaxBalls balls, or when the physics could not add it.
         */
        bool DropBall(const glm::vec3& origin, const glm::vec3& direction);

        /** How many balls there are: 1 after Create, more as DropBall adds them; 0 before Create. */
        [[nodiscard]] std::size_t GetBallCount() const noexcept { return m_balls.size(); }

        /**
         * A ball's own space to world space, blended between the last two steps, as it is drawn; identity for a ball
         * that does not exist.
         */
        [[nodiscard]] glm::mat4 GetBallTransform(std::size_t ball) const;

        /**
         * The ball a ray (from origin along direction, of length 1) meets first, as the balls are drawn; empty when it
         * meets none.
         */
        [[nodiscard]] std::optional<std::size_t> FindBallAt(const glm::vec3& origin, const glm::vec3& direction) const;

        /**
         * Puts a ball at position at once, turned as it is, standing still, as if it had been there all along. Placed
         * after every step (as a drag does each frame) it hangs there; left alone it falls from there. False (and
         * logged) for a ball that does not exist or a position that is not made of numbers.
         */
        bool PlaceBall(std::size_t ball, const glm::vec3& position);

        /** The sea with its waves where they stand at the moment GetBallTransform shows, for drawing them. */
        [[nodiscard]] Sea GetSea() const;

    private:
        /** Adds a ball with settings to the physics and the list; false (and logged) when the physics could not. */
        [[nodiscard]] bool AddBall(const BodySettings& settings);

        // Order matters: the solid land is destroyed before the world it lives in.
        PhysicsWorld m_physics;
        LandCollider m_solidLand;

        /** The balls, the first one the scene starts with; and where that one started, so Reset can put it back. */
        std::vector<BodyId> m_balls;
        BodySettings        m_ballStart;

        /** The surface every ball has: rubber, with the settings' bounce. */
        BodySurface m_ballSurface;

        /** The places the land is kept solid around (balls and viewer); kept, so Step does not allocate again. */
        std::vector<glm::vec3> m_solidAround;

        /** The sea, its waves as the settings last made them. */
        Sea m_sea = BeachSea;
    };
} // namespace Wotah
