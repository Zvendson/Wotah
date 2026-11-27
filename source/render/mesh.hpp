/**
 * @file mesh.hpp
 * A shape on the GPU: MeshData uploaded into a vertex and an index buffer, drawn with one call.
 *
 *     Wotah::Mesh cube;
 *     if (cube.Create(device, Wotah::MakeCube()))
 *     {
 *         cube.Draw(commandBuffer); // with a pipeline using MeshVertexBindings / MeshVertexAttributes bound
 *     }
 *     cube.Destroy();
 */
#pragma once

#include "render/buffer.hpp"

#include <vulkan/vulkan_core.h>

#include <cstdint>

namespace Wotah
{
    class Device;
    struct MeshData;
    struct MeshRange;

    /**
     * A vertex and an index buffer in GPU-only memory. The destructor runs Destroy, so nothing leaks when Create fails
     * part way. The GPU must be done with the mesh before it is destroyed.
     */
    class Mesh final
    {
    public:
        /**
         * Checks the data (at least one triangle, whole triangles, every index naming a vertex) and uploads it,
         * replacing a mesh created before; false (and logged) otherwise. Waits until the upload is done, so it is meant
         * for loading. The device must outlive the mesh.
         */
        [[nodiscard]] bool Create(const Device& device, const MeshData& data);

        /** Frees both buffers; safe to call more than once or after a failed Create. */
        void Destroy();

        /** Binds the buffers and draws every triangle into the command buffer; nothing before Create. */
        void Draw(VkCommandBuffer commandBuffer) const;

        /**
         * Binds the buffers and draws the triangles of a part; nothing before Create, and nothing (logged) for a part
         * that is not whole triangles inside the mesh.
         */
        void Draw(VkCommandBuffer commandBuffer, const MeshRange& part) const;

        /** How many indices Draw draws: three per triangle; 0 before Create and after Destroy. */
        [[nodiscard]] uint32_t GetIndexCount() const noexcept { return m_indexCount; }

    private:
        Buffer   m_vertices;
        Buffer   m_indices;
        uint32_t m_indexCount = 0;
    };
} // namespace Wotah
