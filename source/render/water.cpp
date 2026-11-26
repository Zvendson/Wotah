/**
 * @file water.cpp
 * The water's surface: where it is, and its grid, made the way the ground's is (render/height_field.hpp).
 */
#include "render/water.hpp"

#include "render/height_field.hpp"

#include <algorithm>
#include <cmath>

std::optional<float> Wotah::FindWaterHeight(const WaterArea& water, float x, float z)
{
    // Inside the rectangle, edges included; a NaN place fails every comparison and lands outside.
    const glm::vec2 half = water.Size * 0.5f;
    if (!(std::abs(x) <= half.x && std::abs(z) <= half.y))
    {
        return std::nullopt;
    }
    return water.Level;
}

Wotah::MeshData Wotah::MakeWaterMesh(const WaterArea& water, uint32_t cells)
{
    // A flat field at the water's height draws exactly as the ground does: the same grid, facing up.
    HeightField surface = MakeFlatHeightField(water.Size.x, water.Size.y, cells, cells);
    if (!IsUsable(surface))
    {
        return {}; // MakeFlatHeightField logged why
    }
    std::ranges::fill(surface.Heights, water.Level);
    return MakeTerrainMesh(surface);
}
