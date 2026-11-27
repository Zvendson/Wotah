/**
 * @file clipmap.cpp
 * The clipmap's grid, cut into its parts, and where the levels lie around the viewer.
 */
#include "render/clipmap.hpp"

#include <glm/common.hpp>

#include <cstddef>

namespace
{
    /** A level's width in cells, and the finer level's (the hole's) width in this level's cells. */
    constexpr uint32_t LevelCells = 2 * Wotah::ClipmapHalfCells;
    constexpr uint32_t HoleCells  = Wotah::ClipmapHalfCells;

    /** Where the hole starts when the finer level sits at the middle: a quarter of the way in. */
    constexpr uint32_t HoleStart = Wotah::ClipmapHalfCells / 2;

    static_assert(Wotah::ClipmapHalfCells % 2 == 0, "the finer level's edges must lie on this level's vertices");

    /** MakeGrid gives every cell six indices (two triangles), cell by cell, row by row. */
    constexpr uint32_t IndicesPerCell = 6;

    /** Whether a cell (column, row) lies in a hole of HoleCells that starts at (holeColumn, holeRow). */
    bool IsInHole(uint32_t column, uint32_t row, uint32_t holeColumn, uint32_t holeRow) noexcept
    {
        return column >= holeColumn && column < holeColumn + HoleCells && row >= holeRow && row < holeRow + HoleCells;
    }
} // namespace

Wotah::ClipmapGrid Wotah::MakeClipmapGrid()
{
    // One cell per unit: the vertices' x and z come out as whole numbers from -ClipmapHalfCells to ClipmapHalfCells.
    constexpr float Width = static_cast<float>(LevelCells);
    const MeshData  cells = MakeGrid(Width, Width, LevelCells, LevelCells);

    // The parts, one after the other: the whole square, then each ring, copying the cells they cover.
    ClipmapGrid grid;
    grid.Mesh.Vertices = cells.Vertices;
    for (uint32_t part = 0; part < ClipmapPartCount; ++part)
    {
        const bool     isWhole    = part == ClipmapWholePart;
        const uint32_t ring       = isWhole ? 0 : part - 1; // which ring: x shift + 2 z shift
        const uint32_t holeColumn = HoleStart + ring % 2;
        const uint32_t holeRow    = HoleStart + ring / 2;

        MeshRange& range = grid.Parts[part];
        range.FirstIndex = static_cast<uint32_t>(grid.Mesh.Indices.size());
        for (uint32_t row = 0; row < LevelCells; ++row)
        {
            for (uint32_t column = 0; column < LevelCells; ++column)
            {
                if (!isWhole && IsInHole(column, row, holeColumn, holeRow))
                {
                    continue;
                }
                const std::size_t first = std::size_t{row * LevelCells + column} * IndicesPerCell;
                for (std::size_t index = first; index < first + IndicesPerCell; ++index)
                {
                    grid.Mesh.Indices.push_back(cells.Indices[index]);
                }
            }
        }
        range.IndexCount = static_cast<uint32_t>(grid.Mesh.Indices.size()) - range.FirstIndex;
    }
    return grid;
}

Wotah::ClipmapPlacement Wotah::PlaceClipmap(const glm::vec2& viewer)
{
    // Each level's middle lies on its own grid twice as coarse (step 2 cells), the corner at or below the viewer. A
    // finer level's middle lies on a grid as coarse as this level's cells, so it sits 0 or 1 of this level's cells to
    // the +x and +z side of this level's middle: which ring part leaves the right hole.
    ClipmapPlacement placement;
    glm::vec2        finerSteps{0.0f}; // the finer level's middle, in this level's cells
    float            cellSize = ClipmapFinestCell;
    for (uint32_t index = 0; index < ClipmapLevelCount; ++index)
    {
        const glm::vec2 steps = glm::floor(viewer / (2.0f * cellSize)) * 2.0f; // the middle, in this level's cells
        ClipmapLevel&   level = placement[index];
        level.Center          = steps * cellSize;
        level.CellSize        = cellSize;
        if (index != 0)
        {
            const glm::vec2 offset = finerSteps - steps; // 0 or 1 on each side
            level.Part             = 1 + static_cast<uint32_t>(offset.x) + 2 * static_cast<uint32_t>(offset.y);
        }
        finerSteps = steps * 0.5f; // in the next level's cells, which are twice as large
        cellSize *= 2.0f;
    }
    return placement;
}
