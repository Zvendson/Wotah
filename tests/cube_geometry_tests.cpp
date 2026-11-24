/**
 * @file cube_geometry_tests.cpp
 * The example cube's data: counts, valid indices, every triangle facing outwards (counter-clockwise seen from outside,
 * which back-face culling relies on), and every vertex on its face.
 */
#include "render/cube_geometry.hpp"
#include "test_utils.hpp"

#include <doctest/doctest.h>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <cmath>
#include <cstddef>

using Wotah::Testing::IsNear;

TEST_SUITE("cube geometry")
{
    TEST_CASE("24 vertices, 36 indices, all of them valid")
    {
        CHECK(Wotah::CubeGeometry::Vertices.size() == 24);
        CHECK(Wotah::CubeGeometry::Indices.size() == 36);
        CHECK(Wotah::CubeGeometry::IndexCount == 36);
        for (const uint16_t index : Wotah::CubeGeometry::Indices)
        {
            CHECK(index < Wotah::CubeGeometry::Vertices.size());
        }
    }

    TEST_CASE("every vertex lies on its face, at a corner of the cube")
    {
        for (const Wotah::CubeGeometry::Vertex& vertex : Wotah::CubeGeometry::Vertices)
        {
            // On the face: the position along the normal is half the cube's size.
            CHECK(IsNear(glm::dot(vertex.Position, vertex.Normal), 0.5f));
            CHECK(IsNear(std::abs(vertex.Position.x), 0.5f));
            CHECK(IsNear(std::abs(vertex.Position.y), 0.5f));
            CHECK(IsNear(std::abs(vertex.Position.z), 0.5f));
            CHECK(IsNear(glm::length(vertex.Normal), 1.0f));
        }
    }

    TEST_CASE("every triangle is counter-clockwise when seen from outside")
    {
        const auto& vertices = Wotah::CubeGeometry::Vertices;
        const auto& indices  = Wotah::CubeGeometry::Indices;
        for (std::size_t first = 0; first + 2 < indices.size(); first += 3)
        {
            CAPTURE(first);
            const glm::vec3 a = vertices[indices[first]].Position;
            const glm::vec3 b = vertices[indices[first + 1]].Position;
            const glm::vec3 c = vertices[indices[first + 2]].Position;

            // Counter-clockwise from outside means the right-hand normal points out, along the face's normal.
            const glm::vec3 turning = glm::cross(b - a, c - a);
            CHECK(glm::dot(turning, vertices[indices[first]].Normal) > 0.0f);
        }
    }
}
