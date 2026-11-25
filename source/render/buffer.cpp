/**
 * @file buffer.cpp
 * Buffers on top of the Vulkan Memory Allocator: creating them in the right memory, writing and reading mapped ones,
 * and uploading to GPU-only ones through a staging buffer.
 */
#include "render/buffer.hpp"

#include "core/logger.hpp"
#include "render/device.hpp"
#include "render/vulkan_utils.hpp"

#include <vulkan/vulkan.h>

#include <vk_mem_alloc.h>

#include <cstdint>
#include <cstring>

namespace
{
    /** How VMA should place the memory: the CPU writes in order, reads at random, or never touches it. */
    VmaAllocationCreateFlags GetAllocationFlags(Wotah::BufferMemory memory)
    {
        switch (memory)
        {
            case Wotah::BufferMemory::GpuOnly:
                return 0; // VMA_MEMORY_USAGE_AUTO then prefers memory only the GPU sees
            case Wotah::BufferMemory::CpuToGpu:
                return VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            case Wotah::BufferMemory::GpuToCpu:
                return VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        }
        return 0;
    }

    /**
     * Copies from source to target (offset bytes in) on one-time commands and waits for it. A barrier at the end makes
     * the copied bytes visible to everything the queue runs afterwards, so the buffer can be drawn from right away.
     */
    bool CopyAndWait(const Wotah::Device& device, VkBuffer source, VkBuffer target, const VkBufferCopy& region)
    {
        Wotah::OneTimeCommands commands;
        if (!commands.Begin(device))
        {
            return false;
        }
        vkCmdCopyBuffer(commands.Get(), source, target, 1, &region);

        VkMemoryBarrier visible = Wotah::MakeVulkanInfo<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
        visible.srcAccessMask   = VK_ACCESS_TRANSFER_WRITE_BIT;
        visible.dstAccessMask   = VK_ACCESS_MEMORY_READ_BIT;
        vkCmdPipelineBarrier(commands.Get(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1,
                             &visible, 0, nullptr, 0, nullptr);
        return commands.SubmitAndWait();
    }
} // namespace

Wotah::Buffer::~Buffer()
{
    Destroy();
}

bool Wotah::Buffer::Create(const Device& device, VkDeviceSize size, VkBufferUsageFlags usage, BufferMemory memory)
{
    Destroy(); // a second Create replaces the first buffer instead of leaking it
    if (size == 0)
    {
        Logger::Error("A buffer needs a size above 0");
        return false;
    }
    m_allocator = device.GetAllocator();
    m_memory    = memory;

    VkBufferCreateInfo bufferInfo = Wotah::MakeVulkanInfo<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
    bufferInfo.size               = size;
    bufferInfo.usage              = memory == BufferMemory::GpuOnly ? usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT : usage;
    bufferInfo.sharingMode        = VK_SHARING_MODE_EXCLUSIVE; // one queue does everything

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocationInfo.flags = GetAllocationFlags(memory);

    VmaAllocationInfo placed{};
    if (!Succeeded(vmaCreateBuffer(m_allocator, &bufferInfo, &allocationInfo, &m_buffer, &m_allocation, &placed),
                   "create a buffer"))
    {
        return false;
    }
    m_size   = size;
    m_mapped = static_cast<std::byte*>(placed.pMappedData); // null for GpuOnly
    return true;
}

void Wotah::Buffer::Destroy()
{
    // Frees the buffer and its memory together; both may be null after a failed Create (VMA docs, vmaDestroyBuffer).
    if (m_allocator != nullptr)
    {
        vmaDestroyBuffer(m_allocator, m_buffer, m_allocation);
    }
    m_allocator  = nullptr;
    m_buffer     = VK_NULL_HANDLE;
    m_allocation = nullptr;
    m_size       = 0;
    m_memory     = BufferMemory::GpuOnly;
    m_mapped     = nullptr;
}

bool Wotah::Buffer::Write(std::span<const std::byte> data, VkDeviceSize offset)
{
    if (m_mapped == nullptr)
    {
        Logger::Error("Only a CpuToGpu or GpuToCpu buffer can be written directly; use Upload");
        return false;
    }
    if (!IsInside(offset, data.size()))
    {
        return false;
    }
    std::memcpy(m_mapped + offset, data.data(), data.size());

    // Memory that is not coherent needs a flush before the GPU sees the bytes; on coherent memory this does nothing.
    return Succeeded(vmaFlushAllocation(m_allocator, m_allocation, offset, data.size()), "flush a buffer write");
}

bool Wotah::Buffer::Read(std::span<std::byte> destination, VkDeviceSize offset) const
{
    if (m_mapped == nullptr)
    {
        Logger::Error("Only a CpuToGpu or GpuToCpu buffer can be read");
        return false;
    }
    if (!IsInside(offset, destination.size()))
    {
        return false;
    }

    // Memory that is not coherent needs an invalidate before the CPU sees the GPU's bytes.
    if (!Succeeded(vmaInvalidateAllocation(m_allocator, m_allocation, offset, destination.size()),
                   "invalidate a buffer read"))
    {
        return false;
    }
    std::memcpy(destination.data(), m_mapped + offset, destination.size());
    return true;
}

bool Wotah::Buffer::Upload(const Device& device, std::span<const std::byte> data, VkDeviceSize offset)
{
    if (m_mapped != nullptr)
    {
        return Write(data, offset);
    }
    if (!IsInside(offset, data.size()))
    {
        return false;
    }
    if (data.empty())
    {
        return true;
    }

    // GPU-only memory: the bytes go into a CPU-visible staging buffer first, and the GPU copies them over.
    Buffer staging;
    if (!staging.Create(device, data.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, BufferMemory::CpuToGpu) ||
        !staging.Write(data))
    {
        return false;
    }
    VkBufferCopy region{};
    region.dstOffset = offset;
    region.size      = data.size();
    return CopyAndWait(device, staging.GetHandle(), m_buffer, region);
}

bool Wotah::Buffer::IsInside(VkDeviceSize offset, std::size_t byteCount) const
{
    // Written so a huge offset or count cannot wrap around: offset <= size first, then the room left.
    if (offset > m_size || byteCount > m_size - offset)
    {
        Logger::Error("{} bytes at offset {} do not fit into a buffer of {} bytes", byteCount, offset, m_size);
        return false;
    }
    return true;
}
