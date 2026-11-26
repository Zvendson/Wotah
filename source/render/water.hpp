/**
 * @file water.hpp
 * A body of water: a surface over a rectangle around the origin, at one height. FindWaterHeight tells where the surface
 * is at any place over it (for putting things above it, and the one place water physics will ask), MakeWaterMesh turns
 * it into a fine grid to draw (shaders/water.frag), fine enough for waves to move later.
 *
 *     const Wotah::WaterArea water{{64.0f, 64.0f}, 0.0f};
 *     const std::optional<float> y = Wotah::FindWaterHeight(water, x, z);
 *     surface.Create(device, Wotah::MakeWaterMesh(water, 128));
 *
 * shortcut: the surface is flat and still, and nothing floats yet. Waves go into FindWaterHeight and a vertex shader
 * that moves the grid, buoyancy into the physics, asking FindWaterHeight; both once water physics comes.
 */
#pragma once

#include "render/mesh_data.hpp"

#include <glm/vec2.hpp>

#include <cstdint>
#include <optional>

namespace Wotah
{
    /** Where the water is: its surface's size and height. */
    struct WaterArea
    {
        /** The surface's size along x and along z, in world units, centered on the origin. */
        glm::vec2 Size{0.0f};

        /** The surface's height. */
        float Level = 0.0f;
    };

    /** The water surface's height at world x, z; empty outside the area or for a place that is not a number. */
    [[nodiscard]] std::optional<float> FindWaterHeight(const WaterArea& water, float x, float z);

    /**
     * The surface as a grid of cells x cells squares, facing up, at its height. Empty (and logged) for a size that is
     * not above 0, or for 0 cells or more grid points than 32-bit indices can number.
     */
    [[nodiscard]] MeshData MakeWaterMesh(const WaterArea& water, uint32_t cells);
} // namespace Wotah
