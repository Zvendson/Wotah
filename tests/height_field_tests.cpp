/**
 * @file height_field_tests.cpp
 * HeightField: building flat ones, refusing bad ones, finding the height anywhere (exactly on the drawn triangles), and
 * turning heights into terrain triangles with normals that follow the slopes.
 */
#include "render/height_field.hpp"
#include "test_utils.hpp"

#include <doctest/doctest.h>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

using Wotah::Testing::IsNear;

namespace
{
    /** A 4 x 2 field of 4 x 2 squares (1 unit each) shaped as the tilted plane height = 0.5 x + 0.25 z + 1. */
    Wotah::HeightField MakeTiltedField()
    {
        Wotah::HeightField field = Wotah::MakeFlatHeightField(4.0f, 2.0f, 4, 2);
        for (uint32_t row = 0; row <= field.Rows; ++row)
        {
            for (uint32_t column = 0; column <= field.Columns; ++column)
            {
                const float x = static_cast<float>(column) - 2.0f; // the area runs from -2 to 2 along x
                const float z = static_cast<float>(row) - 1.0f;    // and from -1 to 1 along z
                field.Heights[Wotah::GetHeightIndex(field, column, row)] = 0.5f * x + 0.25f * z + 1.0f;
            }
        }
        return field;
    }
} // namespace

TEST_SUITE("height field")
{
    TEST_CASE("a flat field has one height of 0 per grid point")
    {
        const Wotah::HeightField field = Wotah::MakeFlatHeightField(64.0f, 32.0f, 128, 64);
        CHECK(field.Heights.size() == 129 * 65);
        CHECK(Wotah::IsUsable(field));
        for (const float height : field.Heights)
        {
            CHECK(height == 0.0f);
        }
        CHECK(Wotah::GetHeightIndex(field, 0, 1) == 129); // the second row starts after the first's 129 points
    }

    TEST_CASE("sizes that make no field are refused")
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        CHECK(Wotah::MakeFlatHeightField(0.0f, 1.0f, 1, 1).Heights.empty());
        CHECK(Wotah::MakeFlatHeightField(1.0f, nan, 1, 1).Heights.empty());
        CHECK(Wotah::MakeFlatHeightField(1.0f, 1.0f, 0, 1).Heights.empty());
        CHECK(Wotah::MakeFlatHeightField(1.0f, 1.0f, 70000, 70000).Heights.empty()); // over 2^32 points

        Wotah::HeightField broken = Wotah::MakeFlatHeightField(2.0f, 2.0f, 2, 2);
        broken.Heights.pop_back(); // one height short
        CHECK_FALSE(Wotah::IsUsable(broken));
        CHECK_FALSE(Wotah::FindHeight(broken, 0.0f, 0.0f).has_value());
        CHECK(Wotah::MakeTerrainMesh(broken).Vertices.empty());

        Wotah::HeightField notANumber = Wotah::MakeFlatHeightField(2.0f, 2.0f, 2, 2);
        notANumber.Heights[4]         = nan;
        CHECK(Wotah::MakeTerrainMesh(notANumber).Vertices.empty());
    }

    TEST_CASE("the height anywhere: on the plane inside the area, nothing outside")
    {
        const Wotah::HeightField field = MakeTiltedField();
        for (const float x : {-2.0f, -1.3f, 0.0f, 0.71f, 1.99f, 2.0f})
        {
            for (const float z : {-1.0f, -0.4f, 0.0f, 0.55f, 1.0f})
            {
                CAPTURE(x);
                CAPTURE(z);
                const std::optional<float> height = Wotah::FindHeight(field, x, z);
                REQUIRE(height.has_value());
                CHECK(IsNear(*height, 0.5f * x + 0.25f * z + 1.0f));
            }
        }
        CHECK_FALSE(Wotah::FindHeight(field, 2.01f, 0.0f).has_value());
        CHECK_FALSE(Wotah::FindHeight(field, 0.0f, -1.01f).has_value());
        CHECK_FALSE(Wotah::FindHeight(field, std::numeric_limits<float>::quiet_NaN(), 0.0f).has_value());
    }

    TEST_CASE("the height follows the drawn triangles, not a smooth blend")
    {
        // One raised corner of one square: the square's diagonal runs from (column, row) to (column + 1, row + 1), so
        // the triangle without the raised corner stays flat.
        Wotah::HeightField field = Wotah::MakeFlatHeightField(1.0f, 1.0f, 1, 1); // one square from -0.5 to 0.5
        field.Heights[Wotah::GetHeightIndex(field, 1, 0)] = 1.0f;                // raise corner (x 0.5, z -0.5)
        CHECK(IsNear(*Wotah::FindHeight(field, -0.25f, 0.25f), 0.0f));           // the other triangle: flat
        CHECK(IsNear(*Wotah::FindHeight(field, 0.5f, -0.5f), 1.0f));             // the raised corner itself
        CHECK(IsNear(*Wotah::FindHeight(field, 0.25f, -0.25f), 0.5f));           // halfway towards it
    }

    TEST_CASE("terrain triangles: raised to their heights, facing up, normals along the slope")
    {
        const Wotah::HeightField field   = MakeTiltedField();
        const Wotah::MeshData    terrain = Wotah::MakeTerrainMesh(field);
        REQUIRE(terrain.Vertices.size() == field.Heights.size());
        CHECK(terrain.Indices.size() == 4 * 2 * 6);

        // The plane 0.5 x + 0.25 z + 1 leans against its slope: normal (-0.5, 1, -0.25), made length 1.
        const glm::vec3 expectedNormal = glm::normalize(glm::vec3(-0.5f, 1.0f, -0.25f));
        for (std::size_t index = 0; index < terrain.Vertices.size(); ++index)
        {
            const Wotah::MeshVertex& vertex = terrain.Vertices[index];
            CHECK(IsNear(vertex.Position.y, field.Heights[index]));
            CHECK(IsNear(vertex.Normal, expectedNormal));
        }

        // Every triangle faces up (counter-clockwise from above), whatever the slope.
        for (std::size_t first = 0; first < terrain.Indices.size(); first += 3)
        {
            const glm::vec3 a = terrain.Vertices[terrain.Indices[first]].Position;
            const glm::vec3 b = terrain.Vertices[terrain.Indices[first + 1]].Position;
            const glm::vec3 c = terrain.Vertices[terrain.Indices[first + 2]].Position;
            CHECK(glm::cross(b - a, c - a).y > 0.0f);
        }
    }
}
