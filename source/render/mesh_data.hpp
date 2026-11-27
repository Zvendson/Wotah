/**
 * @file mesh_data.hpp
 * Shapes as plain vertex and index lists, built on the CPU: a cube, a flat grid (a water surface), a sphere (a ball).
 * Every triangle is counter-clockwise when seen from its front (outside), so back faces can be culled.
 *
 *     const Wotah::MeshData ball = Wotah::MakeSphere(0.25f, 32, 16);
 *     Wotah::Mesh mesh;
 *     mesh.Create(device, ball);
 */
#pragma once

#include <glm/vec3.hpp>
#include <vulkan/vulkan_core.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Wotah
{
    /** One corner of a triangle. */
    struct MeshVertex
    {
        /** Where it is, in the mesh's own space. */
        glm::vec3 Position{0.0f};
        /** The outward direction of the surface there, length 1. */
        glm::vec3 Normal{0.0f};
    };

    /** One vertex buffer, one MeshVertex per vertex; for PipelineSettings::VertexBindings. */
    inline constexpr std::array<VkVertexInputBindingDescription, 1> MeshVertexBindings{
        {{0, sizeof(MeshVertex), VK_VERTEX_INPUT_RATE_VERTEX}}};

    /** Position at shader location 0, Normal at location 1; for PipelineSettings::VertexAttributes. */
    inline constexpr std::array<VkVertexInputAttributeDescription, 2> MeshVertexAttributes{{
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, Position)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, Normal)},
    }};

    /** A part of a mesh's triangles: IndexCount indices from FirstIndex on, three per triangle. */
    struct MeshRange
    {
        uint32_t FirstIndex = 0;
        uint32_t IndexCount = 0;
    };

    /** A shape: its vertices, and three indices into them per triangle. */
    struct MeshData
    {
        std::vector<MeshVertex> Vertices;
        std::vector<uint32_t>   Indices;
    };

    /**
     * A cube of size 1 around the origin: 4 vertices per face, each with the face's normal so the faces are lit flat,
     * and 2 triangles per face.
     */
    [[nodiscard]] MeshData MakeCube();

    /**
     * A flat grid in the x-z plane around the origin, facing up (+y): width along x, depth along z, split into columns
     * x rows squares of 2 triangles each. Empty (and logged) for a size that is not above 0, or for 0 columns or rows
     * or more vertices than 32-bit indices can number.
     */
    [[nodiscard]] MeshData MakeGrid(float width, float depth, uint32_t columns, uint32_t rows);

    /**
     * A sphere around the origin: segments around the up axis (at least 3), rings from top to bottom (at least 2).
     * Smooth: every normal points straight out. Empty (and logged) for a radius that is not above 0, too few segments
     * or rings, or more vertices than 32-bit indices can number.
     */
    [[nodiscard]] MeshData MakeSphere(float radius, uint32_t segments, uint32_t rings);
} // namespace Wotah
