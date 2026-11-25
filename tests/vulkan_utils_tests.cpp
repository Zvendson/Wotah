/**
 * @file vulkan_utils_tests.cpp
 * The Vulkan helpers in render/vulkan_utils.hpp that need no GPU.
 */
#include "render/vulkan_utils.hpp"

#include <doctest/doctest.h>
#include <vulkan/vulkan_core.h>

// Usable at compile time, so constant info structs can be made with it too.
static_assert(Wotah::MakeVulkanInfo<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO).sType ==
              VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);

TEST_SUITE("vulkan utils")
{
    TEST_CASE("a Vulkan info struct: its type set, every other field zero")
    {
        const VkBufferCreateInfo buffer =
            Wotah::MakeVulkanInfo<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
        CHECK(buffer.sType == VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
        CHECK(buffer.pNext == nullptr);
        CHECK(buffer.flags == 0);
        CHECK(buffer.size == 0);
        CHECK(buffer.usage == 0);
        CHECK(buffer.sharingMode == VK_SHARING_MODE_EXCLUSIVE); // 0
        CHECK(buffer.queueFamilyIndexCount == 0);
        CHECK(buffer.pQueueFamilyIndices == nullptr);

        const VkRenderingAttachmentInfo attachment =
            Wotah::MakeVulkanInfo<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
        CHECK(attachment.sType == VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
        CHECK(attachment.imageView == VK_NULL_HANDLE);
        CHECK(attachment.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD); // 0
        CHECK(attachment.clearValue.color.float32[0] == 0.0f);
    }
}
