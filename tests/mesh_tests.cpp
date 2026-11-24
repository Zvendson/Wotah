/**
 * @file mesh_tests.cpp
 * Mesh on a real GPU: uploading the shapes, refusing data that cannot be drawn, replacing and freeing. Drawing itself
 * is checked by the cube picture in gpu_tests.cpp.
 */
#include "render/mesh.hpp"
#include "render/mesh_data.hpp"
#include "test_utils.hpp"

#include <doctest/doctest.h>

using Wotah::Testing::HiddenGpu;

TEST_SUITE("gpu")
{
    TEST_CASE("meshes upload the cube, a grid and a sphere")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());
        {
            Wotah::Mesh neverCreated; // destroyed without Create: safe
        }

        Wotah::Mesh cube;
        CHECK(cube.Create(gpu.Device, Wotah::MakeCube()));
        CHECK(cube.GetIndexCount() == 36);

        Wotah::Mesh grid;
        CHECK(grid.Create(gpu.Device, Wotah::MakeGrid(10.0f, 10.0f, 256, 256))); // over 65535 vertices: 32-bit indices
        CHECK(grid.GetIndexCount() == 256 * 256 * 6);

        Wotah::Mesh sphere;
        CHECK(sphere.Create(gpu.Device, Wotah::MakeSphere(0.25f, 32, 16)));
        CHECK(sphere.GetIndexCount() == 32 * (2 * 16 - 2) * 3);

        // A second Create replaces the first.
        CHECK(sphere.Create(gpu.Device, Wotah::MakeCube()));
        CHECK(sphere.GetIndexCount() == 36);

        cube.Destroy();
        cube.Destroy(); // twice is safe
        CHECK(cube.GetIndexCount() == 0);
    }

    TEST_CASE("data that cannot be drawn is refused")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());
        Wotah::Mesh mesh;

        CHECK_FALSE(mesh.Create(gpu.Device, Wotah::MeshData{})); // nothing

        Wotah::MeshData broken = Wotah::MakeCube();
        broken.Indices.pop_back(); // not whole triangles
        CHECK_FALSE(mesh.Create(gpu.Device, broken));

        broken            = Wotah::MakeCube();
        broken.Indices[5] = 24; // past the last vertex
        CHECK_FALSE(mesh.Create(gpu.Device, broken));

        CHECK(mesh.GetIndexCount() == 0);
    }
}
