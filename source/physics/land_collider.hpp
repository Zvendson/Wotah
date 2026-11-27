/**
 * @file land_collider.hpp
 * Keeps the land solid wherever something may touch it, however far anyone travels: square tiles of the land's shape
 * (terrain/terrain_shape.hpp) as fixed height fields in a physics world, around a few places (the ball, the camera),
 * made as those places move and dropped once nothing is near them any more. A tile's heights are sampled on a worker
 * thread, so moving on never stalls a frame; only the tile under a place is waited for when it is missing, so nothing
 * falls through (sampled in bands of rows on several threads at once then, to keep the wait short).
 *
 *     Wotah::LandCollider land;
 *     land.Create(physics, shape, sand);
 *     // every frame, before the physics steps:
 *     land.Follow(std::array{ballPosition, cameraPosition});
 */
#pragma once

#include "physics/physics_world.hpp"
#include "terrain/terrain_shape.hpp"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <future>
#include <span>
#include <vector>

namespace Wotah
{
    /**
     * A tile's samples to a side, a metre apart: 64, a multiple of Jolt's blocks of samples (HeightFieldShapeSettings),
     * so a tile reaches 63 metres and shares its edge samples with its neighbours (the same heights, no seam).
     */
    inline constexpr uint32_t LandTileSamples = 64;
    inline constexpr float    LandTileSpacing = 1.0f;
    inline constexpr float    LandTileSize    = static_cast<float>(LandTileSamples - 1) * LandTileSpacing;

    /**
     * How many tiles to each side of the tile a place is on are kept: one, so 3 x 3 tiles, at least 63 metres of land
     * in every direction. Tiles a further one off stay until nothing is that near, so a place going to and fro over a
     * tile's edge does not make and drop tiles all the time.
     */
    inline constexpr int LandTileReach = 1;

    /** The tile a place (x, z) lies on: tile (i, j) reaches from i, j times LandTileSize up to the next. */
    [[nodiscard]] glm::ivec2 FindLandTile(const glm::vec2& place);

    /**
     * The land's tiles in a physics world, made and dropped as the places they are kept around move. The destructor
     * runs Destroy. The world must outlive it; one thread uses both at a time.
     */
    class LandCollider final
    {
    public:
        LandCollider() = default;
        ~LandCollider();

        LandCollider(const LandCollider&)            = delete;
        LandCollider& operator=(const LandCollider&) = delete;

        /**
         * Keeps the land of shape solid in world from now on, with surface's grip and bounciness, once Follow says
         * where; false (and logged) for a shape that is not usable or a world that is not created. Replaces what an
         * earlier Create kept.
         */
        [[nodiscard]] bool Create(PhysicsWorld& world, const TerrainShape& shape, const BodySurface& surface);

        /** Takes every tile out of the world, after waiting for the ones being sampled; safe to call more than once. */
        void Destroy();

        /**
         * Keeps the tiles within LandTileReach of every place: the tile under each place is made at once (waiting for
         * it, if needed), the others are sampled on worker threads and added by a later Follow; tiles nothing is near
         * any more are dropped. Allocates nothing while the places stay on their tiles.
         */
        void Follow(std::span<const glm::vec3> places);

        /** Waits until every tile being sampled is done and adds the ones still wanted: before a test or a start. */
        void FinishPending();

        /** How many tiles are in the world now. */
        [[nodiscard]] std::size_t GetTileCount() const noexcept { return m_tiles.size(); }

        /** Whether a tile is in the world now. */
        [[nodiscard]] bool HasTile(const glm::ivec2& tile) const;

    private:
        /** A tile in the world, and one being sampled. */
        struct Tile
        {
            glm::ivec2 Cell{0};
            BodyId     Body;
        };

        struct PendingTile
        {
            glm::ivec2                Cell{0};
            std::future<TerrainPatch> Patch;
        };

        /** Marks the tiles within reach of every place as wanted (m_wanted), and the places' own tiles (m_under). */
        void ListWantedTiles(std::span<const glm::vec3> places);

        /** Adds the finished tiles still near something; drops finished ones that are not (waits for none). */
        void TakeFinishedTiles(bool isWaiting);

        /** Takes out the tiles nothing is near any more (beyond LandTileReach + 1 of every place's tile). */
        void DropFarTiles();

        /** Samples a tile's heights (on the calling thread) and adds it; false (and logged) when that fails. */
        bool AddTile(const glm::ivec2& cell, const TerrainPatch& patch);

        /** Whether a tile is being sampled. */
        [[nodiscard]] bool IsPending(const glm::ivec2& cell) const;

        /** Whether a tile lies within reach of one of the places' tiles. */
        [[nodiscard]] bool IsNear(const glm::ivec2& cell, int reach) const;

        PhysicsWorld*            m_world = nullptr;
        TerrainShape             m_shape;
        BodySurface              m_surface;
        std::vector<Tile>        m_tiles;
        std::vector<PendingTile> m_pending;
        std::vector<glm::ivec2>  m_wanted; // kept between calls, so steady frames allocate nothing
        std::vector<glm::ivec2>  m_under;
    };
} // namespace Wotah
