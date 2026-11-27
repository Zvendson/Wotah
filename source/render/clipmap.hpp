/**
 * @file clipmap.hpp
 * A surface that reaches the horizon in every direction around the viewer, finely near it and coarser far away,
 * drawn from one small grid: a geometry clipmap (Losasso and Hoppe 2004, "Geometry Clipmaps: Terrain Rendering Using
 * Nested Regular Grids"). Level 0 is a square of cells around the viewer; every further level is a ring of cells twice
 * as large around the one inside it, so each level covers twice the distance with the same number of vertices. The
 * levels move with the viewer in whole cells of their own size, so nothing slides across the surface. A vertex shader
 * places each vertex (shaders/clipmap.glsl) and blends the outer edge of every level into the next coarser one, so the
 * levels meet without cracks. The land and the water both use it.
 *
 *     const Wotah::ClipmapGrid grid = Wotah::MakeClipmapGrid();   // once: one mesh for every level
 *     mesh.Create(device, grid.Mesh);
 *     // every frame:
 *     for (const Wotah::ClipmapLevel& level : Wotah::PlaceClipmap(viewer))
 *     {
 *         // push glm::vec4(level.Center, level.CellSize, Wotah::ClipmapHalfCells), then:
 *         mesh.Draw(commands, grid.Parts[level.Part]);
 *     }
 */
#pragma once

#include "render/mesh_data.hpp"

#include <glm/vec2.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace Wotah
{
    /**
     * How many levels there are, how many cells half a level is wide, and the finest cells' size in metres: level 0
     * covers 64 x 64 metres around the viewer in half-metre cells, level 7 reaches 4 km out in 64-metre cells, past
     * where the haze hides everything.
     */
    inline constexpr uint32_t ClipmapLevelCount = 8;
    inline constexpr uint32_t ClipmapHalfCells  = 64;
    inline constexpr float    ClipmapFinestCell = 0.5f;

    /**
     * The grid's parts: the whole square (level 0) and the four rings with a hole for the finer level inside, which
     * sits one cell to the +x and to the +z side of the middle, or not (index 1 + x + 2 z).
     */
    inline constexpr std::size_t ClipmapPartCount = 5;
    inline constexpr uint32_t    ClipmapWholePart = 0;

    /**
     * The grid every level is drawn from: (2 ClipmapHalfCells + 1)^2 vertices whose x and z are whole numbers from
     * -ClipmapHalfCells to ClipmapHalfCells (cells of a level, from its middle; y is 0), and its parts.
     */
    struct ClipmapGrid
    {
        MeshData                                Mesh;
        std::array<MeshRange, ClipmapPartCount> Parts{};
    };

    /** The clipmap's grid and its parts. */
    [[nodiscard]] ClipmapGrid MakeClipmapGrid();

    /** Where one level lies this frame: its middle (x, z) in the world, its cells' size, and which part to draw. */
    struct ClipmapLevel
    {
        glm::vec2 Center{0.0f};
        float     CellSize = 0.0f;
        uint32_t  Part     = ClipmapWholePart;
    };

    /** Every level, finest first. */
    using ClipmapPlacement = std::array<ClipmapLevel, ClipmapLevelCount>;

    /**
     * Where the levels lie around a viewer at (x, z): each level's middle on a corner of its own grid twice as coarse,
     * so the next level's ring fits around it exactly; the viewer stays within two cells of every level's middle.
     */
    [[nodiscard]] ClipmapPlacement PlaceClipmap(const glm::vec2& viewer);
} // namespace Wotah
