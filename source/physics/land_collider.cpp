/**
 * @file land_collider.cpp
 * Which tiles are wanted, sampling them on worker threads, adding and dropping their bodies.
 */
#include "physics/land_collider.hpp"

#include "core/logger.hpp"
#include "core/utils.hpp"

#include <glm/common.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <optional>
#include <utility>

namespace
{
    /** Room for this many places' tiles without allocating while following them: the ball and the camera, and more. */
    constexpr std::size_t PlacesReserved = 4;

    /** The tiles kept around one place. */
    constexpr std::size_t TilesPerPlace = (2 * Wotah::LandTileReach + 1) * (2 * Wotah::LandTileReach + 1);

    /** A tile's heights from the shape: its samples from its own corner on, LandTileSpacing apart. */
    Wotah::TerrainPatch SampleTile(const Wotah::TerrainShape& shape, const glm::ivec2& cell)
    {
        const glm::vec2 middle = (glm::vec2(cell) + 0.5f) * Wotah::LandTileSize;
        return Wotah::SampleTerrainPatch(shape, middle, Wotah::LandTileSpacing, Wotah::LandTileSamples);
    }

    /** How many tiles apart two tiles are: the larger of the counts along x and along z. */
    int GetTileDistance(const glm::ivec2& first, const glm::ivec2& second)
    {
        return std::max(std::abs(first.x - second.x), std::abs(first.y - second.y));
    }

    /** Whether a list of tiles holds a tile. */
    bool Contains(const std::vector<glm::ivec2>& cells, const glm::ivec2& cell)
    {
        return std::ranges::find(cells, cell) != cells.end();
    }
} // namespace

glm::ivec2 Wotah::FindLandTile(const glm::vec2& place)
{
    return glm::ivec2(glm::floor(place / LandTileSize));
}

Wotah::LandCollider::~LandCollider()
{
    Destroy();
}

bool Wotah::LandCollider::Create(PhysicsWorld& world, const TerrainShape& shape, const BodySurface& surface)
{
    Destroy(); // a second Create replaces the first
    if (!IsUsable(shape))
    {
        Logger::Error("The land's collider needs a usable shape");
        return false;
    }
    m_world   = &world;
    m_shape   = shape;
    m_surface = surface;
    m_wanted.reserve(PlacesReserved * TilesPerPlace);
    m_under.reserve(PlacesReserved);
    m_tiles.reserve(PlacesReserved * TilesPerPlace * 2); // with the ones kept a tile further off
    m_pending.reserve(PlacesReserved * TilesPerPlace);
    return true;
}

void Wotah::LandCollider::Destroy()
{
    // The workers' results are waited for, so no thread outlives the shape it reads.
    for (PendingTile& pending : m_pending)
    {
        pending.Patch.wait();
    }
    m_pending.clear();
    if (m_world != nullptr)
    {
        for (const Tile& tile : m_tiles)
        {
            (void)m_world->RemoveBody(tile.Body);
        }
    }
    m_tiles.clear();
    m_world = nullptr;
}

void Wotah::LandCollider::Follow(std::span<const glm::vec3> places)
{
    if (m_world == nullptr)
    {
        return;
    }
    ListWantedTiles(places);
    TakeFinishedTiles(false);

    // The tiles under the places now, so nothing falls through: waited for if being sampled, else sampled here.
    for (const glm::ivec2& cell : m_under)
    {
        if (HasTile(cell))
        {
            continue;
        }
        const auto pending = std::ranges::find(m_pending, cell, &PendingTile::Cell);
        if (pending != m_pending.end())
        {
            const TerrainPatch patch = pending->Patch.get();
            m_pending.erase(pending);
            (void)AddTile(cell, patch);
        }
        else
        {
            (void)AddTile(cell, SampleTile(m_shape, cell));
        }
    }

    // The tiles around them on worker threads; a later Follow adds them.
    for (const glm::ivec2& cell : m_wanted)
    {
        if (!HasTile(cell) && !IsPending(cell))
        {
            m_pending.push_back({cell, std::async(std::launch::async, SampleTile, m_shape, cell)});
        }
    }
    DropFarTiles();
}

void Wotah::LandCollider::FinishPending()
{
    TakeFinishedTiles(true);
}

bool Wotah::LandCollider::HasTile(const glm::ivec2& tile) const
{
    return std::ranges::find(m_tiles, tile, &Tile::Cell) != m_tiles.end();
}

void Wotah::LandCollider::ListWantedTiles(std::span<const glm::vec3> places)
{
    m_wanted.clear();
    m_under.clear();
    for (const glm::vec3& place : places)
    {
        if (!IsFinite(place))
        {
            continue; // no tile to be found for it
        }
        const glm::ivec2 under = FindLandTile({place.x, place.z});
        if (!Contains(m_under, under))
        {
            m_under.push_back(under);
        }
        for (int z = -LandTileReach; z <= LandTileReach; ++z)
        {
            for (int x = -LandTileReach; x <= LandTileReach; ++x)
            {
                const glm::ivec2 cell = under + glm::ivec2(x, z);
                if (!Contains(m_wanted, cell))
                {
                    m_wanted.push_back(cell);
                }
            }
        }
    }
}

void Wotah::LandCollider::TakeFinishedTiles(bool isWaiting)
{
    for (auto pending = m_pending.begin(); pending != m_pending.end();)
    {
        const bool isDone = isWaiting || pending->Patch.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        if (!isDone)
        {
            ++pending;
            continue;
        }
        const glm::ivec2   cell  = pending->Cell;
        const TerrainPatch patch = pending->Patch.get();
        pending                  = m_pending.erase(pending);
        if (IsNear(cell, LandTileReach + 1) && !HasTile(cell))
        {
            (void)AddTile(cell, patch);
        }
    }
}

void Wotah::LandCollider::DropFarTiles()
{
    for (auto tile = m_tiles.begin(); tile != m_tiles.end();)
    {
        if (IsNear(tile->Cell, LandTileReach + 1))
        {
            ++tile;
            continue;
        }
        (void)m_world->RemoveBody(tile->Body);
        tile = m_tiles.erase(tile);
    }
}

bool Wotah::LandCollider::AddTile(const glm::ivec2& cell, const TerrainPatch& patch)
{
    if (patch.Heights.empty())
    {
        return false; // SampleTerrainPatch logged why
    }
    const std::optional<BodyId> body = m_world->AddHeightField(patch.Heights, patch.SamplesPerSide, patch.Spacing,
                                                               {patch.Corner.x, 0.0f, patch.Corner.y}, m_surface);
    if (!body)
    {
        return false; // AddHeightField logged why
    }
    m_tiles.push_back({cell, *body});
    return true;
}

bool Wotah::LandCollider::IsPending(const glm::ivec2& cell) const
{
    return std::ranges::find(m_pending, cell, &PendingTile::Cell) != m_pending.end();
}

bool Wotah::LandCollider::IsNear(const glm::ivec2& cell, int reach) const
{
    return std::ranges::any_of(m_under, [&cell, reach](const glm::ivec2& under)
                               { return GetTileDistance(cell, under) <= reach; });
}
