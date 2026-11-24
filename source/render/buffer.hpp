/**
 * @file buffer.hpp
 * A Vulkan buffer with its GPU memory from the Vulkan Memory Allocator: vertex, index and uniform buffers, and buffers
 * to read results back. Where the memory lives decides how data gets in and out:
 *
 *     Wotah::Buffer vertices;  // a mesh: lives on the GPU, filled once
 *     vertices.Create(device, sizeof(data), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, Wotah::BufferMemory::GpuOnly);
 *     vertices.Upload(device, std::as_bytes(std::span(data)));
 *
 *     Wotah::Buffer waves;     // a uniform: the CPU writes it every frame
 *     waves.Create(device, sizeof(WaveSettings), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, Wotah::BufferMemory::CpuToGpu);
 *     waves.Write(std::as_bytes(std::span(&settings, 1)));
 */
#pragma once

#include "render/vulkan_handles.hpp"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <span>

namespace Wotah
{
    class Device;

    /** Where a buffer's memory lives, which decides how the CPU reaches it. */
    enum class BufferMemory
    {
        /** On the GPU only, the fastest to draw from (meshes); filled with Upload through a staging buffer. */
        GpuOnly,
        /** Written by the CPU, often every frame, and read by the GPU (uniforms); filled with Write, stays mapped. */
        CpuToGpu,
        /** Written by the GPU and read back by the CPU (screenshots, results); read with Read, stays mapped. */
        GpuToCpu,
    };

    /**
     * A buffer and its memory. The destructor runs Destroy, so nothing leaks when Create fails part way. The GPU must
     * be done with the buffer before it is destroyed.
     */
    class Buffer final
    {
    public:
        Buffer() = default;
        ~Buffer();

        Buffer(const Buffer&)            = delete;
        Buffer& operator=(const Buffer&) = delete;

        /**
         * Creates a buffer of size bytes for the given uses (VK_BUFFER_USAGE_*); GpuOnly buffers also accept copies,
         * so Upload works on them. False (and logged) for a size of 0 or when Vulkan refuses. The device must outlive
         * the buffer.
         */
        [[nodiscard]] bool Create(const Device& device, VkDeviceSize size, VkBufferUsageFlags usage,
                                  BufferMemory memory);

        /** Frees the buffer and its memory; safe to call more than once or after a failed Create. */
        void Destroy();

        /**
         * Copies data into the buffer at offset bytes; for CpuToGpu and GpuToCpu buffers, visible to the GPU's next
         * submitted work. False (and logged) for a GpuOnly buffer or data that does not fit.
         */
        [[nodiscard]] bool Write(std::span<const std::byte> data, VkDeviceSize offset = 0);

        /**
         * Copies the buffer's bytes from offset into destination, after the GPU's writes have finished (wait for them
         * first). False (and logged) for a GpuOnly buffer or a range outside the buffer.
         */
        [[nodiscard]] bool Read(std::span<std::byte> destination, VkDeviceSize offset = 0) const;

        /**
         * Fills the buffer at offset with data, whatever its memory: mapped buffers are written directly, GpuOnly ones
         * through a temporary staging buffer and a copy on the device's queue. Waits until the copy is done, so it is
         * meant for loading, not for every frame. False (and logged) when the data does not fit or Vulkan fails.
         */
        [[nodiscard]] bool Upload(const Device& device, std::span<const std::byte> data, VkDeviceSize offset = 0);

        /** The buffer, for binding and copying. */
        [[nodiscard]] VkBuffer GetHandle() const noexcept { return m_buffer; }

        /** The size in bytes it was created with; 0 before Create and after Destroy. */
        [[nodiscard]] VkDeviceSize GetSize() const noexcept { return m_size; }

        /** Where its memory lives. */
        [[nodiscard]] BufferMemory GetMemory() const noexcept { return m_memory; }

    private:
        /** Whether bytes from offset fit into the buffer; logs the reason when not. */
        [[nodiscard]] bool IsInside(VkDeviceSize offset, std::size_t byteCount) const;

        /** The allocator the memory came from; not owned. */
        VmaAllocator m_allocator = nullptr;

        VkBuffer      m_buffer     = VK_NULL_HANDLE;
        VmaAllocation m_allocation = nullptr;
        VkDeviceSize  m_size       = 0;
        BufferMemory  m_memory     = BufferMemory::GpuOnly;

        /** The memory's address for the CPU; null for GpuOnly buffers. */
        std::byte* m_mapped = nullptr;
    };
} // namespace Wotah
