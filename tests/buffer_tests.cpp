/**
 * @file buffer_tests.cpp
 * Buffer on a real GPU: refused sizes and ranges, writing and reading mapped memory, uploading to GPU-only memory and
 * reading it back after a GPU copy, and freeing.
 */
#include "render/buffer.hpp"
#include "render/device.hpp"
#include "render/vulkan_utils.hpp"
#include "test_utils.hpp"

#include <doctest/doctest.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

using Wotah::Testing::HiddenGpu;

namespace
{

    /** count bytes 0, 1, 2, ... wrapping at 256, so every position has a known value. */
    std::vector<std::byte> MakePattern(std::size_t count)
    {
        std::vector<std::byte> bytes(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            bytes[index] = static_cast<std::byte>(index % 256);
        }
        return bytes;
    }

    /** Copies size bytes from source to target on the GPU and waits for it. */
    void CopyOnGpu(const Wotah::Device& device, VkBuffer source, VkBuffer target, VkDeviceSize size)
    {
        const VkDevice          handle = device.GetHandle();
        VkCommandPoolCreateInfo poolInfo =
            Wotah::MakeVulkanInfo<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
        poolInfo.queueFamilyIndex = device.GetQueueFamily();
        VkCommandPool pool        = VK_NULL_HANDLE;
        REQUIRE(vkCreateCommandPool(handle, &poolInfo, nullptr, &pool) == VK_SUCCESS);

        VkCommandBufferAllocateInfo commandInfo =
            Wotah::MakeVulkanInfo<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
        commandInfo.commandPool        = pool;
        commandInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandInfo.commandBufferCount = 1;
        VkCommandBuffer commands       = VK_NULL_HANDLE;
        REQUIRE(vkAllocateCommandBuffers(handle, &commandInfo, &commands) == VK_SUCCESS);

        VkCommandBufferBeginInfo beginInfo =
            Wotah::MakeVulkanInfo<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
        REQUIRE(vkBeginCommandBuffer(commands, &beginInfo) == VK_SUCCESS);
        VkBufferCopy region{};
        region.size = size;
        vkCmdCopyBuffer(commands, source, target, 1, &region);
        VkMemoryBarrier toHost = Wotah::MakeVulkanInfo<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
        toHost.srcAccessMask   = VK_ACCESS_TRANSFER_WRITE_BIT;
        toHost.dstAccessMask   = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &toHost, 0,
                             nullptr, 0, nullptr);
        REQUIRE(vkEndCommandBuffer(commands) == VK_SUCCESS);

        VkSubmitInfo submit       = Wotah::MakeVulkanInfo<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
        submit.commandBufferCount = 1;
        submit.pCommandBuffers    = &commands;
        REQUIRE(vkQueueSubmit(device.GetQueue(), 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS);
        device.WaitUntilIdle();
        vkDestroyCommandPool(handle, pool, nullptr);
    }
} // namespace

TEST_SUITE("gpu")
{
    TEST_CASE("buffers refuse a size of 0 and free cleanly")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());
        {
            Wotah::Buffer neverCreated; // destroyed without Create: safe
        }

        Wotah::Buffer buffer;
        CHECK_FALSE(buffer.Create(gpu.Device, 0, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, Wotah::BufferMemory::CpuToGpu));
        CHECK(buffer.GetHandle() == VK_NULL_HANDLE);

        REQUIRE(buffer.Create(gpu.Device, 256, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, Wotah::BufferMemory::CpuToGpu));
        CHECK(buffer.GetHandle() != VK_NULL_HANDLE);
        CHECK(buffer.GetSize() == 256);
        CHECK(buffer.GetMemory() == Wotah::BufferMemory::CpuToGpu);
        buffer.Destroy();
        buffer.Destroy(); // twice is safe
        CHECK(buffer.GetHandle() == VK_NULL_HANDLE);
        CHECK(buffer.GetSize() == 0);
    }

    TEST_CASE("mapped buffers are written and read, only inside their size")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());

        for (const Wotah::BufferMemory memory : {Wotah::BufferMemory::CpuToGpu, Wotah::BufferMemory::GpuToCpu})
        {
            CAPTURE(static_cast<int>(memory));
            Wotah::Buffer buffer;
            REQUIRE(buffer.Create(gpu.Device, 1024, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, memory));

            const std::vector<std::byte> pattern = MakePattern(1024);
            CHECK(buffer.Write(pattern));
            std::vector<std::byte> back(1024);
            CHECK(buffer.Read(back));
            CHECK(back == pattern);

            // A part in the middle.
            const std::vector<std::byte> part(16, std::byte{0xAB});
            CHECK(buffer.Write(part, 100));
            std::vector<std::byte> partBack(16);
            CHECK(buffer.Read(partBack, 100));
            CHECK(partBack == part);

            // Outside the buffer: refused, nothing written.
            CHECK_FALSE(buffer.Write(part, 1020));           // 4 bytes too many
            CHECK_FALSE(buffer.Write(part, 2000));           // starts outside
            CHECK_FALSE(buffer.Write(part, UINT64_MAX - 4)); // would wrap around
            CHECK_FALSE(buffer.Read(partBack, 1020));
            CHECK(buffer.Read(back));
            CHECK(back[1020] == pattern[1020]);
        }
    }

    TEST_CASE("GPU-only buffers are filled by Upload and refuse direct access")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());
        constexpr std::size_t ByteCount = 1 << 20; // 1 MiB: a real mesh's size, well past any small-copy path

        Wotah::Buffer gpuOnly;
        REQUIRE(gpuOnly.Create(gpu.Device, ByteCount, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, Wotah::BufferMemory::GpuOnly));
        const std::vector<std::byte> pattern = MakePattern(ByteCount);
        CHECK_FALSE(gpuOnly.Write(pattern)); // not mapped: Upload only
        std::vector<std::byte> notReadable(16);
        CHECK_FALSE(gpuOnly.Read(notReadable));
        CHECK_FALSE(gpuOnly.Upload(gpu.Device, MakePattern(ByteCount + 1))); // does not fit

        REQUIRE(gpuOnly.Upload(gpu.Device, pattern));

        // A partial upload in the middle replaces just those bytes.
        const std::vector<std::byte> part(64, std::byte{0xCD});
        REQUIRE(gpuOnly.Upload(gpu.Device, part, 4096));

        // Copy it to a buffer the CPU can read, and compare.
        Wotah::Buffer readback;
        REQUIRE(
            readback.Create(gpu.Device, ByteCount, VK_BUFFER_USAGE_TRANSFER_DST_BIT, Wotah::BufferMemory::GpuToCpu));
        CopyOnGpu(gpu.Device, gpuOnly.GetHandle(), readback.GetHandle(), ByteCount);
        std::vector<std::byte> back(ByteCount);
        REQUIRE(readback.Read(back));

        std::vector<std::byte> expected = pattern;
        std::copy(part.begin(), part.end(), expected.begin() + 4096);
        CHECK(back == expected);
    }

    TEST_CASE("Upload on a mapped buffer writes it directly")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());
        Wotah::Buffer buffer;
        REQUIRE(buffer.Create(gpu.Device, 64, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, Wotah::BufferMemory::CpuToGpu));
        const std::vector<std::byte> pattern = MakePattern(64);
        CHECK(buffer.Upload(gpu.Device, pattern));
        std::vector<std::byte> back(64);
        CHECK(buffer.Read(back));
        CHECK(back == pattern);
    }
}
