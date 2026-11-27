/**
 * @file mesh.cpp
 * Checking, uploading and drawing mesh data.
 */
#include "render/mesh.hpp"

#include "core/logger.hpp"
#include "render/mesh_data.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <span>

namespace
{
    /** The index type matching MeshData::Indices. */
    constexpr VkIndexType MeshIndexType = VK_INDEX_TYPE_UINT32;

    /** Whether the data can be drawn as it is; every problem is logged. */
    bool IsDrawable(const Wotah::MeshData& data)
    {
        if (data.Vertices.empty() || data.Indices.empty())
        {
            Wotah::Logger::Error("A mesh needs vertices and at least one triangle");
            return false;
        }
        if (data.Indices.size() % 3 != 0)
        {
            Wotah::Logger::Error("A mesh needs three indices per triangle, not {} indices", data.Indices.size());
            return false;
        }
        if (data.Indices.size() > std::numeric_limits<uint32_t>::max())
        {
            Wotah::Logger::Error("A mesh can draw at most 2^32 - 1 indices, not {}", data.Indices.size());
            return false;
        }

        // An index past the last vertex would make the GPU read memory that is not the mesh's.
        const uint32_t highest = *std::ranges::max_element(data.Indices);
        if (highest >= data.Vertices.size())
        {
            Wotah::Logger::Error("A mesh index names vertex {}, but there are only {}", highest, data.Vertices.size());
            return false;
        }
        return true;
    }
} // namespace

bool Wotah::Mesh::Create(const Device& device, const MeshData& data)
{
    Destroy(); // a second Create replaces the first mesh instead of leaking it
    if (!IsDrawable(data))
    {
        return false;
    }

    const std::span<const std::byte> vertexBytes = std::as_bytes(std::span(data.Vertices));
    const std::span<const std::byte> indexBytes  = std::as_bytes(std::span(data.Indices));
    if (!m_vertices.Create(device, vertexBytes.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, BufferMemory::GpuOnly) ||
        !m_vertices.Upload(device, vertexBytes) ||
        !m_indices.Create(device, indexBytes.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, BufferMemory::GpuOnly) ||
        !m_indices.Upload(device, indexBytes))
    {
        return false;
    }
    m_indexCount = static_cast<uint32_t>(data.Indices.size()); // checked by IsDrawable
    return true;
}

void Wotah::Mesh::Destroy()
{
    m_indices.Destroy();
    m_vertices.Destroy();
    m_indexCount = 0;
}

void Wotah::Mesh::Draw(VkCommandBuffer commandBuffer) const
{
    MeshRange everything;
    everything.IndexCount = m_indexCount;
    Draw(commandBuffer, everything);
}

void Wotah::Mesh::Draw(VkCommandBuffer commandBuffer, const MeshRange& part) const
{
    if (m_indexCount == 0)
    {
        return; // nothing uploaded: drawing would bind null buffers
    }
    // Inside the mesh and whole triangles, compared without adding up past 32 bits.
    if (part.FirstIndex > m_indexCount || part.IndexCount > m_indexCount - part.FirstIndex || part.IndexCount % 3 != 0)
    {
        Logger::Error("A mesh part of {} indices from {} is not whole triangles inside the mesh's {}", part.IndexCount,
                      part.FirstIndex, m_indexCount);
        return;
    }
    const VkBuffer     vertexBuffer = m_vertices.GetHandle();
    const VkDeviceSize vertexOffset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &vertexOffset);
    vkCmdBindIndexBuffer(commandBuffer, m_indices.GetHandle(), 0, MeshIndexType);
    vkCmdDrawIndexed(commandBuffer, part.IndexCount, 1, part.FirstIndex, 0, 0);
}
