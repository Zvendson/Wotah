/**
 * @file mesh_data_tests.cpp
 * The shape makers: counts, valid indices, every triangle facing outwards (counter-clockwise from its front, which
 * back-face culling relies on) and never degenerate, positions and normals where they belong, and refused sizes.
 */
#include "render/mesh_data.hpp"
#include "test_utils.hpp"

#include <doctest/doctest.h>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

using Wotah::Testing::IsNear;

namespace
{
    /** Checks that indices come in triangles, name real vertices, face outwards and are never degenerate. */
    void CheckTriangles(const Wotah::MeshData& mesh)
    {
        REQUIRE_FALSE(mesh.Indices.empty());
        REQUIRE(mesh.Indices.size() % 3 == 0);
        for (std::size_t first = 0; first < mesh.Indices.size(); first += 3)
        {
            CAPTURE(first);
            const uint32_t ia = mesh.Indices[first];
            const uint32_t ib = mesh.Indices[first + 1];
            const uint32_t ic = mesh.Indices[first + 2];
            REQUIRE(ia < mesh.Vertices.size());
            REQUIRE(ib < mesh.Vertices.size());
            REQUIRE(ic < mesh.Vertices.size());

            const Wotah::MeshVertex& a = mesh.Vertices[ia];
            const Wotah::MeshVertex& b = mesh.Vertices[ib];
            const Wotah::MeshVertex& c = mesh.Vertices[ic];

            // Counter-clockwise from the front means the right-hand turn points the way the normals point.
            const glm::vec3 turning = glm::cross(b.Position - a.Position, c.Position - a.Position);
            CHECK(glm::length(turning) > 1.0e-6f);
            CHECK(glm::dot(turning, a.Normal + b.Normal + c.Normal) > 0.0f);
        }
    }

    /** Checks that every normal has length 1. */
    void CheckUnitNormals(const Wotah::MeshData& mesh)
    {
        for (const Wotah::MeshVertex& vertex : mesh.Vertices)
        {
            CHECK(IsNear(glm::length(vertex.Normal), 1.0f));
        }
    }
} // namespace

TEST_SUITE("mesh data")
{
    TEST_CASE("the cube: 24 vertices on 6 flat faces, 12 outward triangles")
    {
        const Wotah::MeshData cube = Wotah::MakeCube();
        CHECK(cube.Vertices.size() == 24);
        CHECK(cube.Indices.size() == 36);
        CheckTriangles(cube);
        CheckUnitNormals(cube);
        for (const Wotah::MeshVertex& vertex : cube.Vertices)
        {
            CHECK(IsNear(glm::dot(vertex.Position, vertex.Normal), 0.5f)); // on its face
            CHECK(IsNear(std::abs(vertex.Position.x), 0.5f));              // at a corner
            CHECK(IsNear(std::abs(vertex.Position.y), 0.5f));
            CHECK(IsNear(std::abs(vertex.Position.z), 0.5f));
        }
    }

    TEST_CASE("a grid: flat in x-z, facing up, the asked size and resolution")
    {
        const Wotah::MeshData grid = Wotah::MakeGrid(4.0f, 2.0f, 4, 2);
        CHECK(grid.Vertices.size() == 5 * 3);
        CHECK(grid.Indices.size() == 4 * 2 * 6);
        CheckTriangles(grid);
        CheckUnitNormals(grid);

        float lowestX  = 1.0e9f;
        float highestX = -1.0e9f;
        float lowestZ  = 1.0e9f;
        float highestZ = -1.0e9f;
        for (const Wotah::MeshVertex& vertex : grid.Vertices)
        {
            CHECK(vertex.Position.y == 0.0f);
            CHECK(IsNear(vertex.Normal, {0.0f, 1.0f, 0.0f}));
            lowestX  = std::min(lowestX, vertex.Position.x);
            highestX = std::max(highestX, vertex.Position.x);
            lowestZ  = std::min(lowestZ, vertex.Position.z);
            highestZ = std::max(highestZ, vertex.Position.z);
        }
        CHECK(IsNear(lowestX, -2.0f));
        CHECK(IsNear(highestX, 2.0f));
        CHECK(IsNear(lowestZ, -1.0f));
        CHECK(IsNear(highestZ, 1.0f));
    }

    TEST_CASE("a sphere: every vertex at the radius, normals straight out, closed at the poles")
    {
        constexpr float       Radius   = 2.0f;
        constexpr uint32_t    Segments = 8;
        constexpr uint32_t    Rings    = 4;
        const Wotah::MeshData sphere   = Wotah::MakeSphere(Radius, Segments, Rings);
        CHECK(sphere.Vertices.size() == (Segments + 1) * (Rings + 1));
        CHECK(sphere.Indices.size() == Segments * (2 * Rings - 2) * 3); // the poles have one triangle per segment
        CheckTriangles(sphere);
        CheckUnitNormals(sphere);
        for (const Wotah::MeshVertex& vertex : sphere.Vertices)
        {
            CHECK(IsNear(glm::length(vertex.Position), Radius));
            CHECK(IsNear(vertex.Normal, vertex.Position / Radius));
        }
        CHECK(IsNear(sphere.Vertices.front().Position, {0.0f, Radius, 0.0f}));
        CHECK(IsNear(sphere.Vertices.back().Position, {0.0f, -Radius, 0.0f}));
    }

    TEST_CASE("sizes that make no shape are refused")
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        CHECK(Wotah::MakeGrid(0.0f, 1.0f, 1, 1).Vertices.empty());
        CHECK(Wotah::MakeGrid(1.0f, nan, 1, 1).Vertices.empty());
        CHECK(Wotah::MakeGrid(1.0f, 1.0f, 0, 1).Vertices.empty());
        CHECK(Wotah::MakeGrid(1.0f, 1.0f, 70000, 70000).Vertices.empty()); // over 2^32 vertices
        CHECK(Wotah::MakeSphere(0.0f, 8, 4).Vertices.empty());
        CHECK(Wotah::MakeSphere(nan, 8, 4).Vertices.empty());
        CHECK(Wotah::MakeSphere(1.0f, 2, 4).Vertices.empty());
        CHECK(Wotah::MakeSphere(1.0f, 8, 1).Vertices.empty());
    }
}
