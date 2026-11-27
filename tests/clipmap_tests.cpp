/**
 * @file clipmap_tests.cpp
 * The geometry clipmap: its grid and parts (the whole square, and rings whose hole is exactly where the finer level
 * goes), and where the levels lie around the viewer: around it, each twice as coarse, each finer level filling its
 * coarser level's hole exactly, wherever the viewer stands.
 */
#include "render/clipmap.hpp"
#include "render/mesh_data.hpp"

#include <doctest/doctest.h>
#include <glm/common.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace
{
    constexpr float HalfCells = static_cast<float>(Wotah::ClipmapHalfCells);

    /** A triangle's middle on the grid, in cells from the level's middle. */
    glm::vec2 GetTriangleMiddle(const Wotah::MeshData& mesh, std::size_t firstIndex)
    {
        const glm::vec3 sum = mesh.Vertices[mesh.Indices[firstIndex]].Position +
                              mesh.Vertices[mesh.Indices[firstIndex + 1]].Position +
                              mesh.Vertices[mesh.Indices[firstIndex + 2]].Position;
        return glm::vec2(sum.x, sum.z) / 3.0f;
    }

    /** The hole of a ring part, in cells from the level's middle: its lowest corner (it is HalfCells wide). */
    glm::vec2 GetHoleCorner(uint32_t part)
    {
        const uint32_t ring = part - 1;
        return glm::vec2(-HalfCells / 2.0f) + glm::vec2(static_cast<float>(ring % 2), static_cast<float>(ring / 2));
    }

    /** Whether a point (cells from the level's middle) lies inside a hole of HalfCells starting at corner. */
    bool IsInHole(const glm::vec2& point, const glm::vec2& corner)
    {
        return point.x > corner.x && point.x < corner.x + HalfCells && point.y > corner.y &&
               point.y < corner.y + HalfCells;
    }
} // namespace

TEST_SUITE("clipmap")
{
    TEST_CASE(
        "the grid: whole-number places from one edge of a level to the other, the square whole, rings around holes")
    {
        const Wotah::ClipmapGrid grid = Wotah::MakeClipmapGrid();
        constexpr std::size_t    Side = 2 * Wotah::ClipmapHalfCells + 1;
        REQUIRE(grid.Mesh.Vertices.size() == Side * Side);
        for (const Wotah::MeshVertex& vertex : grid.Mesh.Vertices)
        {
            CHECK(vertex.Position.x == std::round(vertex.Position.x));
            CHECK(vertex.Position.z == std::round(vertex.Position.z));
            CHECK(std::abs(vertex.Position.x) <= HalfCells);
            CHECK(std::abs(vertex.Position.z) <= HalfCells);
            CHECK(vertex.Position.y == 0.0f);
        }

        // The whole square has every cell; a ring every cell but the hole's, a quarter of the square.
        constexpr uint32_t      Cells = 4 * Wotah::ClipmapHalfCells * Wotah::ClipmapHalfCells;
        const Wotah::MeshRange& whole = grid.Parts[Wotah::ClipmapWholePart];
        CHECK(whole.FirstIndex == 0);
        CHECK(whole.IndexCount == Cells * 6);
        uint32_t next = whole.IndexCount;
        for (uint32_t part = 1; part < Wotah::ClipmapPartCount; ++part)
        {
            CAPTURE(part);
            const Wotah::MeshRange& ring = grid.Parts[part];
            CHECK(ring.FirstIndex == next);
            CHECK(ring.IndexCount == (Cells - Cells / 4) * 6);
            next += ring.IndexCount;

            // No triangle of the ring lies in its hole.
            const glm::vec2 corner = GetHoleCorner(part);
            for (uint32_t index = ring.FirstIndex; index < ring.FirstIndex + ring.IndexCount; index += 3)
            {
                CHECK_FALSE(IsInHole(GetTriangleMiddle(grid.Mesh, index), corner));
            }
        }
        CHECK(next == grid.Mesh.Indices.size());
    }

    TEST_CASE("the levels lie around the viewer, each twice as coarse, each finer one filling its coarser one's hole")
    {
        for (const glm::vec2 viewer : {glm::vec2(0.0f), glm::vec2(0.3f, -0.2f), glm::vec2(1.1f, 2.7f),
                                       glm::vec2(-37.3f, 81.9f), glm::vec2(1234.56f, -987.6f)})
        {
            CAPTURE(viewer.x);
            CAPTURE(viewer.y);
            const Wotah::ClipmapPlacement placement = Wotah::PlaceClipmap(viewer);
            CHECK(placement.front().CellSize == Wotah::ClipmapFinestCell);
            CHECK(placement.front().Part == Wotah::ClipmapWholePart);
            for (std::size_t index = 0; index < placement.size(); ++index)
            {
                CAPTURE(index);
                const Wotah::ClipmapLevel& level = placement[index];

                // The viewer stands within two cells of the level's middle, which lies on the level's grid.
                const glm::vec2 offset = (viewer - level.Center) / level.CellSize;
                CHECK(offset.x >= 0.0f);
                CHECK(offset.x < 2.0f);
                CHECK(offset.y >= 0.0f);
                CHECK(offset.y < 2.0f);
                CHECK(level.Center.x / level.CellSize == std::round(level.Center.x / level.CellSize));
                if (index == 0)
                {
                    continue;
                }

                // Twice as coarse as the finer level, whose square (HalfCells of its cells to each side of its middle)
                // is exactly this ring's hole.
                const Wotah::ClipmapLevel& finer = placement[index - 1];
                CHECK(level.CellSize == 2.0f * finer.CellSize);
                REQUIRE(level.Part >= 1);
                REQUIRE(level.Part < Wotah::ClipmapPartCount);
                const glm::vec2 holeCorner  = level.Center + GetHoleCorner(level.Part) * level.CellSize;
                const glm::vec2 finerCorner = finer.Center - glm::vec2(HalfCells * finer.CellSize);
                CHECK(holeCorner.x == finerCorner.x);
                CHECK(holeCorner.y == finerCorner.y);
            }
        }
    }
}
