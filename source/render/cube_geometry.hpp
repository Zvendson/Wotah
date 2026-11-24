/**
 * @file cube_geometry.hpp
 * The example cube as vertex and index data: size 1 around the origin, 4 vertices per face (each with the face's
 * normal, so faces are lit flat) and 2 triangles per face, counter-clockwise when seen from outside.
 *
 *     vertexBuffer.Upload(device, std::as_bytes(std::span(Wotah::CubeGeometry::Vertices)));
 *     settings.VertexBindings   = Wotah::CubeGeometry::Bindings;
 *     settings.VertexAttributes = Wotah::CubeGeometry::Attributes;
 *     vkCmdDrawIndexed(commands, Wotah::CubeGeometry::IndexCount, 1, 0, 0, 0);
 */
#pragma once

#include <glm/vec3.hpp>
#include <vulkan/vulkan_core.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace Wotah::CubeGeometry
{
    /** One corner of a face; matches the inputs of shaders/cube.vert. */
    struct Vertex
    {
        /** Where it is, -0.5 .. 0.5 per axis. */
        glm::vec3 Position{0.0f};
        /** The face's outward direction. */
        glm::vec3 Normal{0.0f};
    };

    /** The 8 corners, numbered by bits: x = bit 0, y = bit 1, z = bit 2 (0 means -0.5, 1 means +0.5). */
    inline constexpr std::array<glm::vec3, 8> Corners{{
        {-0.5f, -0.5f, -0.5f},
        {0.5f, -0.5f, -0.5f},
        {-0.5f, 0.5f, -0.5f},
        {0.5f, 0.5f, -0.5f},
        {-0.5f, -0.5f, 0.5f},
        {0.5f, -0.5f, 0.5f},
        {-0.5f, 0.5f, 0.5f},
        {0.5f, 0.5f, 0.5f},
    }};

    /** A face: its normal and its 4 corners, counter-clockwise when seen from outside. */
    struct Face
    {
        glm::vec3               Normal{0.0f};
        std::array<uint16_t, 4> Corners{};
    };

    /** The 6 faces: +x, -x, +y, -y, +z, -z. */
    inline constexpr std::array<Face, 6> Faces{{
        {{1.0f, 0.0f, 0.0f}, {1, 3, 7, 5}},
        {{-1.0f, 0.0f, 0.0f}, {0, 4, 6, 2}},
        {{0.0f, 1.0f, 0.0f}, {2, 6, 7, 3}},
        {{0.0f, -1.0f, 0.0f}, {0, 1, 5, 4}},
        {{0.0f, 0.0f, 1.0f}, {4, 5, 7, 6}},
        {{0.0f, 0.0f, -1.0f}, {0, 2, 3, 1}},
    }};

    /** Vertices per face: its 4 corners. */
    inline constexpr uint32_t VerticesPerFace = 4;

    /** Indices per face: 2 triangles of 3. */
    inline constexpr uint32_t IndicesPerFace = 6;

    /** How many indices to draw. */
    inline constexpr uint32_t IndexCount = static_cast<uint32_t>(Faces.size()) * IndicesPerFace;

    /** How many vertices there are. */
    inline constexpr std::size_t VertexCount = Faces.size() * VerticesPerFace;

    /** Builds Vertices: each face's 4 corners with the face's normal. */
    constexpr std::array<Vertex, VertexCount> MakeVertices()
    {
        std::array<Vertex, VertexCount> vertices{};
        for (std::size_t face = 0; face < Faces.size(); ++face)
        {
            for (std::size_t corner = 0; corner < VerticesPerFace; ++corner)
            {
                vertices[face * VerticesPerFace + corner] = {Corners[Faces[face].Corners[corner]], Faces[face].Normal};
            }
        }
        return vertices;
    }

    /** Builds Indices: corners 0 1 2 and 0 2 3 of every face, which keeps them counter-clockwise. */
    constexpr std::array<uint16_t, IndexCount> MakeIndices()
    {
        constexpr std::array<uint16_t, IndicesPerFace> FacePattern{0, 1, 2, 0, 2, 3};
        std::array<uint16_t, IndexCount>               indices{};
        for (std::size_t face = 0; face < Faces.size(); ++face)
        {
            for (std::size_t index = 0; index < IndicesPerFace; ++index)
            {
                indices[face * IndicesPerFace + index] =
                    static_cast<uint16_t>(face * VerticesPerFace + FacePattern[index]);
            }
        }
        return indices;
    }

    /** The 24 vertices. */
    inline constexpr std::array<Vertex, VertexCount> Vertices = MakeVertices();

    /** The 36 indices. */
    inline constexpr std::array<uint16_t, IndexCount> Indices = MakeIndices();

    /** The index type of Indices, for vkCmdBindIndexBuffer. */
    inline constexpr VkIndexType IndexType = VK_INDEX_TYPE_UINT16;

    /** One vertex buffer, one Vertex per vertex. */
    inline constexpr std::array<VkVertexInputBindingDescription, 1> Bindings{
        {{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX}}};

    /** Position at location 0, Normal at location 1 (shaders/cube.vert). */
    inline constexpr std::array<VkVertexInputAttributeDescription, 2> Attributes{{
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, Position)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, Normal)},
    }};
} // namespace Wotah::CubeGeometry
