/**
 * @file beach.hpp
 * The example scene: a sandy beach along a wandering coast with dunes and green hills behind it (terrain/), the sea in
 * front to the west, where the sun sets, its waves rolling in onto the beach, and a blue ball dropped from the air into
 * the shallow water, where it floats. Here are its land, its sea, the view and the settings it starts with, and its
 * moving part, BeachWorld: the physics that drops the ball, floats it on the waves and lets it roll where it lands on
 * sand. How it is drawn is up to the caller (app.cpp); nothing here knows Vulkan.
 *
 *     Wotah::BeachWorld world;
 *     world.Create();
 *     world.Step(seconds, camera.GetPosition());   // each frame
 *     const glm::mat4  ball = world.GetBallTransform();
 *     const Wotah::Sea sea  = world.GetSea();       // its waves where the ball sees them
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
     * The beach's physics: the land solid around the ball and the camera wherever they go, the sea with its waves, and
     * the ball, which floats on them. The destructor frees everything.
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
         * Moves the world on by seconds: first the land turns solid around the ball and the viewer (the camera, so
         * whatever it may drop on lands), then the physics steps.
         */
        void Step(float seconds, const glm::vec3& viewer);

        /**
         * Puts the ball back where it started, in the air, standing still, at once and without making anything again;
         * false (and logged) when that failed.
         */
        bool Reset();

        /** Puts the settings' gravity, ball bounce and waves into the world; false (and logged) when that failed. */
        bool ApplySettings(const SceneSettings& settings);

        /** The ball's own space to world space, blended between the last two steps (identity before Create). */
        [[nodiscard]] glm::mat4 GetBallTransform() const;

        /** The sea with its waves where they stand at the moment GetBallTransform shows, for drawing them. */
        [[nodiscard]] Sea GetSea() const;

    private:
        // Order matters: the solid land is destroyed before the world it lives in.
        PhysicsWorld m_physics;
        LandCollider m_solidLand;

        /** The ball, and where it started, so Reset can put it back. */
        BodyId       m_ball;
        BodySettings m_ballStart;

        /** The sea, its waves as the settings last made them. */
        Sea m_sea = BeachSea;
    };
} // namespace Wotah
