/**
 * @file water_tests.cpp
 * The water's surface: where it is over its area and nowhere else, and its grid: flat at its height, facing up,
 * covering the area, as fine as asked.
 */
#include "render/mesh_data.hpp"
#include "render/water.hpp"

#include <doctest/doctest.h>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>

TEST_SUITE("water")
{
    TEST_CASE("the water's height: its level over the whole area, edges included, nothing outside")
    {
        const Wotah::WaterArea water{{64.0f, 32.0f}, -1.5f};
        CHECK(Wotah::FindWaterHeight(water, 0.0f, 0.0f) == -1.5f);
        CHECK(Wotah::FindWaterHeight(water, 31.9f, -15.9f) == -1.5f);
        CHECK(Wotah::FindWaterHeight(water, 32.0f, 16.0f) == -1.5f);
        CHECK_FALSE(Wotah::FindWaterHeight(water, 32.1f, 0.0f).has_value());
        CHECK_FALSE(Wotah::FindWaterHeight(water, 0.0f, -16.1f).has_value());

        constexpr float NoNumber = std::numeric_limits<float>::quiet_NaN();
        CHECK_FALSE(Wotah::FindWaterHeight(water, NoNumber, 0.0f).has_value());
        CHECK_FALSE(Wotah::FindWaterHeight(water, 0.0f, NoNumber).has_value());

        // No area, no water.
        CHECK_FALSE(Wotah::FindWaterHeight(Wotah::WaterArea{}, 0.1f, 0.0f).has_value());
    }

    TEST_CASE("the water's grid: flat at its level, facing up, covering the area in as many squares as asked")
    {
        const Wotah::WaterArea water{{64.0f, 32.0f}, 2.0f};
        constexpr uint32_t     Cells = 16;
        const Wotah::MeshData  mesh  = Wotah::MakeWaterMesh(water, Cells);

        REQUIRE(mesh.Vertices.size() == (Cells + 1) * (Cells + 1));
        CHECK(mesh.Indices.size() == Cells * Cells * 6);

        float lowestX  = 0.0f;
        float highestX = 0.0f;
        float lowestZ  = 0.0f;
        float highestZ = 0.0f;
        for (const Wotah::MeshVertex& vertex : mesh.Vertices)
        {
            CHECK(vertex.Position.y == 2.0f);
            CHECK(vertex.Normal == glm::vec3(0.0f, 1.0f, 0.0f));
            lowestX  = std::min(lowestX, vertex.Position.x);
            highestX = std::max(highestX, vertex.Position.x);
            lowestZ  = std::min(lowestZ, vertex.Position.z);
            highestZ = std::max(highestZ, vertex.Position.z);
        }
        CHECK(lowestX == -32.0f);
        CHECK(highestX == 32.0f);
        CHECK(lowestZ == -16.0f);
        CHECK(highestZ == 16.0f);
    }

    TEST_CASE("a water area without size or without cells makes no grid")
    {
        CHECK(Wotah::MakeWaterMesh(Wotah::WaterArea{}, 16).Vertices.empty());
        CHECK(Wotah::MakeWaterMesh(Wotah::WaterArea{{8.0f, 8.0f}, 0.0f}, 0).Vertices.empty());
    }
}
