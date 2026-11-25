/**
 * @file height_field.cpp
 * Height fields: building flat ones, finding the height anywhere on them, and turning them into terrain triangles.
 */
#include "render/height_field.hpp"

#include "core/logger.hpp"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace
{
    /** How many grid points a columns x rows field has; 64-bit, so it cannot wrap. */
    uint64_t CountPoints(uint32_t columns, uint32_t rows)
    {
        return (uint64_t{columns} + 1) * (uint64_t{rows} + 1);
    }

    /** The height at a grid point, its column and row clamped into the field (for slopes at the edges). */
    float GetClampedHeight(const Wotah::HeightField& field, int64_t column, int64_t row)
    {
        const auto clampedColumn = static_cast<uint32_t>(std::clamp<int64_t>(column, 0, field.Columns));
        const auto clampedRow    = static_cast<uint32_t>(std::clamp<int64_t>(row, 0, field.Rows));
        return field.Heights[Wotah::GetHeightIndex(field, clampedColumn, clampedRow)];
    }
} // namespace

Wotah::HeightField Wotah::MakeFlatHeightField(float width, float depth, uint32_t columns, uint32_t rows)
{
    HeightField field;
    field.Width   = width;
    field.Depth   = depth;
    field.Columns = columns;
    field.Rows    = rows;
    if (!(width > 0.0f && depth > 0.0f) || columns == 0 || rows == 0 ||
        CountPoints(columns, rows) > std::numeric_limits<uint32_t>::max())
    {
        Logger::Error(
            "A height field needs a size above 0, at least 1 column and row, and under 2^32 points; not {}x{} "
            "with {}x{}",
            width, depth, columns, rows);
        return {};
    }
    field.Heights.assign(static_cast<std::size_t>(CountPoints(columns, rows)), 0.0f);
    return field;
}

std::size_t Wotah::GetHeightIndex(const HeightField& field, uint32_t column, uint32_t row) noexcept
{
    return static_cast<std::size_t>(row) * (static_cast<std::size_t>(field.Columns) + 1) + column;
}

bool Wotah::IsUsable(const HeightField& field)
{
    if (!(field.Width > 0.0f && field.Depth > 0.0f) || field.Columns == 0 || field.Rows == 0 ||
        CountPoints(field.Columns, field.Rows) > std::numeric_limits<uint32_t>::max())
    {
        Logger::Error("A height field needs a size above 0 and at least 1 column and row");
        return false;
    }
    if (field.Heights.size() != CountPoints(field.Columns, field.Rows))
    {
        Logger::Error("A {}x{} height field needs {} heights, not {}", field.Columns, field.Rows,
                      CountPoints(field.Columns, field.Rows), field.Heights.size());
        return false;
    }
    return true;
}

std::optional<float> Wotah::FindHeight(const HeightField& field, float x, float z)
{
    if (!IsUsable(field))
    {
        return std::nullopt;
    }

    // Where the point lies in grid units: 0 at the -x / -z edge, Columns / Rows at the far edge.
    const float u = (x / field.Width + 0.5f) * static_cast<float>(field.Columns);
    const float v = (z / field.Depth + 0.5f) * static_cast<float>(field.Rows);
    if (!(u >= 0.0f && u <= static_cast<float>(field.Columns) && v >= 0.0f && v <= static_cast<float>(field.Rows)))
    {
        return std::nullopt; // outside the area (or not a number)
    }

    // The square it lies in (the far edge belongs to the last square), and where in that square.
    const auto  column = std::min(static_cast<uint32_t>(u), field.Columns - 1);
    const auto  row    = std::min(static_cast<uint32_t>(v), field.Rows - 1);
    const float across = u - static_cast<float>(column);
    const float along  = v - static_cast<float>(row);

    const float h00 = field.Heights[GetHeightIndex(field, column, row)];
    const float h10 = field.Heights[GetHeightIndex(field, column + 1, row)];
    const float h01 = field.Heights[GetHeightIndex(field, column, row + 1)];
    const float h11 = field.Heights[GetHeightIndex(field, column + 1, row + 1)];

    // MakeGrid splits each square along the diagonal from (column, row) to (column + 1, row + 1): one triangle holds
    // the corner (column, row + 1), the other (column + 1, row). Each triangle is flat, so the height is linear in it.
    if (along >= across)
    {
        return h00 + along * (h01 - h00) + across * (h11 - h01);
    }
    return h00 + across * (h10 - h00) + along * (h11 - h10);
}

Wotah::MeshData Wotah::MakeTerrainMesh(const HeightField& field)
{
    if (!IsUsable(field))
    {
        return {};
    }
    if (!std::ranges::all_of(field.Heights, [](float height) { return std::isfinite(height); }))
    {
        Logger::Error("A height field's heights must all be numbers");
        return {};
    }

    // The flat grid has its vertices in the same order as the heights; raise each to its height.
    MeshData    terrain   = MakeGrid(field.Width, field.Depth, field.Columns, field.Rows);
    const float cellWidth = field.Width / static_cast<float>(field.Columns);
    const float cellDepth = field.Depth / static_cast<float>(field.Rows);
    for (uint32_t row = 0; row <= field.Rows; ++row)
    {
        for (uint32_t column = 0; column <= field.Columns; ++column)
        {
            MeshVertex& vertex = terrain.Vertices[GetHeightIndex(field, column, row)];
            vertex.Position.y  = field.Heights[GetHeightIndex(field, column, row)];

            // The slope from the neighbours on both sides (one side at the edges); the surface's normal leans against
            // the slope: (-dh/dx, 1, -dh/dz).
            const int64_t left        = std::max<int64_t>(int64_t{column} - 1, 0);
            const int64_t right       = std::min<int64_t>(int64_t{column} + 1, field.Columns);
            const int64_t previousRow = std::max<int64_t>(int64_t{row} - 1, 0);
            const int64_t nextRow     = std::min<int64_t>(int64_t{row} + 1, field.Rows);
            const float   slopeX      = (GetClampedHeight(field, right, row) - GetClampedHeight(field, left, row)) /
                                        (static_cast<float>(right - left) * cellWidth);
            const float   slopeZ =
                (GetClampedHeight(field, column, nextRow) - GetClampedHeight(field, column, previousRow)) /
                (static_cast<float>(nextRow - previousRow) * cellDepth);
            vertex.Normal = glm::normalize(glm::vec3(-slopeX, 1.0f, -slopeZ));
        }
    }
    return terrain;
}
