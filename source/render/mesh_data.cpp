/**
 * @file mesh_data.cpp
 * Builds the cube, grid and sphere vertex and index lists.
 */
#include "render/mesh_data.hpp"

#include "core/logger.hpp"

#include <glm/gtc/constants.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace
{
    /** A face of the cube: its normal and its 4 corners, counter-clockwise when seen from outside. */
    struct CubeFace
    {
        glm::vec3              Normal{0.0f};
        std::array<uint8_t, 4> Corners{};
    };

    /** The cube's 8 corners, numbered by bits: x = bit 0, y = bit 1, z = bit 2 (0 means -0.5, 1 means +0.5). */
    constexpr std::array<glm::vec3, 8> CubeCorners{{
        {-0.5f, -0.5f, -0.5f},
        {0.5f, -0.5f, -0.5f},
        {-0.5f, 0.5f, -0.5f},
        {0.5f, 0.5f, -0.5f},
        {-0.5f, -0.5f, 0.5f},
        {0.5f, -0.5f, 0.5f},
        {-0.5f, 0.5f, 0.5f},
        {0.5f, 0.5f, 0.5f},
    }};

    /** The cube's 6 faces: +x, -x, +y, -y, +z, -z. */
    constexpr std::array<CubeFace, 6> CubeFaces{{
        {{1.0f, 0.0f, 0.0f}, {1, 3, 7, 5}},
        {{-1.0f, 0.0f, 0.0f}, {0, 4, 6, 2}},
        {{0.0f, 1.0f, 0.0f}, {2, 6, 7, 3}},
        {{0.0f, -1.0f, 0.0f}, {0, 1, 5, 4}},
        {{0.0f, 0.0f, 1.0f}, {4, 5, 7, 6}},
        {{0.0f, 0.0f, -1.0f}, {0, 2, 3, 1}},
    }};

    /** Whether (columns + 1) x (rows + 1) vertices can all be numbered by uint32_t indices. */
    bool FitsIndices(uint32_t columns, uint32_t rows)
    {
        const uint64_t vertices = (uint64_t{columns} + 1) * (uint64_t{rows} + 1);
        return vertices <= std::numeric_limits<uint32_t>::max();
    }

    /** Adds the two triangles of a quad whose corners a, b, c, d are counter-clockwise from its front: a b c, a c d. */
    void AddQuad(std::vector<uint32_t>& indices, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
    {
        indices.insert(indices.end(), {a, b, c, a, c, d});
    }
} // namespace

Wotah::MeshData Wotah::MakeCube()
{
    MeshData cube;
    cube.Vertices.reserve(CubeFaces.size() * 4);
    cube.Indices.reserve(CubeFaces.size() * 6);
    for (const CubeFace& face : CubeFaces)
    {
        const auto first = static_cast<uint32_t>(cube.Vertices.size());
        for (const uint8_t corner : face.Corners)
        {
            MeshVertex vertex;
            vertex.Position = CubeCorners[corner];
            vertex.Normal   = face.Normal;
            cube.Vertices.push_back(vertex);
        }
        AddQuad(cube.Indices, first, first + 1, first + 2, first + 3);
    }
    return cube;
}

Wotah::MeshData Wotah::MakeGrid(float width, float depth, uint32_t columns, uint32_t rows)
{
    if (!(width > 0.0f && depth > 0.0f) || columns == 0 || rows == 0 || !FitsIndices(columns, rows))
    {
        Logger::Error(
            "A grid needs a size above 0, at least 1 column and row, and under 2^32 vertices; not {}x{} with {}x{}",
            width, depth, columns, rows);
        return {};
    }

    // Vertices row by row from -z to +z, each row from -x to +x.
    MeshData        grid;
    const uint32_t  rowLength = columns + 1;
    const glm::vec3 up{0.0f, 1.0f, 0.0f};
    grid.Vertices.reserve(static_cast<std::size_t>(rowLength) * (rows + 1));
    for (uint32_t row = 0; row <= rows; ++row)
    {
        const float z = depth * (static_cast<float>(row) / static_cast<float>(rows) - 0.5f);
        for (uint32_t column = 0; column <= columns; ++column)
        {
            MeshVertex vertex;
            vertex.Position = {width * (static_cast<float>(column) / static_cast<float>(columns) - 0.5f), 0.0f, z};
            vertex.Normal   = up;
            grid.Vertices.push_back(vertex);
        }
    }

    // Seen from above, with x to the right and z towards the viewer, (row, column) -> (row + 1, column) ->
    // (row + 1, column + 1) -> (row, column + 1) runs counter-clockwise.
    grid.Indices.reserve(static_cast<std::size_t>(columns) * rows * 6);
    for (uint32_t row = 0; row < rows; ++row)
    {
        for (uint32_t column = 0; column < columns; ++column)
        {
            const uint32_t current = row * rowLength + column;
            const uint32_t nextRow = current + rowLength;
            AddQuad(grid.Indices, current, nextRow, nextRow + 1, current + 1);
        }
    }
    return grid;
}

Wotah::MeshData Wotah::MakeSphere(float radius, uint32_t segments, uint32_t rings)
{
    constexpr uint32_t MinSegments = 3; // fewer cannot enclose anything
    constexpr uint32_t MinRings    = 2;
    if (!(radius > 0.0f) || segments < MinSegments || rings < MinRings || !FitsIndices(segments, rings))
    {
        Logger::Error("A sphere needs a radius above 0, at least 3 segments and 2 rings, and under 2^32 vertices; not "
                      "{} with {}x{}",
                      radius, segments, rings);
        return {};
    }

    // Vertices ring by ring from the top (polar angle 0) to the bottom (pi), each ring once around the up axis. The
    // first and last vertex of a ring sit on the same spot (the seam), which keeps the index math uniform.
    MeshData       sphere;
    const uint32_t ringLength = segments + 1;
    sphere.Vertices.reserve(static_cast<std::size_t>(ringLength) * (rings + 1));
    for (uint32_t ring = 0; ring <= rings; ++ring)
    {
        const float polar = glm::pi<float>() * static_cast<float>(ring) / static_cast<float>(rings);
        for (uint32_t segment = 0; segment <= segments; ++segment)
        {
            const float     around = glm::two_pi<float>() * static_cast<float>(segment) / static_cast<float>(segments);
            const glm::vec3 direction{std::sin(polar) * std::cos(around), std::cos(polar),
                                      std::sin(polar) * std::sin(around)};
            MeshVertex      vertex;
            vertex.Position = direction * radius;
            vertex.Normal   = direction;
            sphere.Vertices.push_back(vertex);
        }
    }

    // Each square between two rings: upper (around, around + 1) and lower (around, around + 1). Seen from outside,
    // upper -> upper next -> lower and upper next -> lower next -> lower run counter-clockwise. At the poles one of the
    // two triangles has two corners on the same point; it is left out.
    for (uint32_t ring = 0; ring < rings; ++ring)
    {
        for (uint32_t segment = 0; segment < segments; ++segment)
        {
            const uint32_t upper     = ring * ringLength + segment;
            const uint32_t upperNext = upper + 1;
            const uint32_t lower     = upper + ringLength;
            const uint32_t lowerNext = lower + 1;
            if (ring != 0)
            {
                sphere.Indices.insert(sphere.Indices.end(), {upper, upperNext, lower});
            }
            if (ring != rings - 1)
            {
                sphere.Indices.insert(sphere.Indices.end(), {upperNext, lowerNext, lower});
            }
        }
    }
    return sphere;
}
