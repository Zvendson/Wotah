/**
 * @file scene_depth.cpp
 * The depth image's sampler, descriptor set and layout change for reading it.
 */
#include "render/scene_depth.hpp"

#include "render/device.hpp"
#include "render/image.hpp"
#include "render/vulkan_utils.hpp"

#include <vulkan/vulkan.h>

#include <array>

namespace
{
    /** The one binding: the depth image, read by fragment shaders. */
    constexpr std::array<Wotah::DescriptorBinding, 1> DepthBindings{
        {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT}}};
    constexpr uint32_t DepthBinding = 0;
} // namespace

void Wotah::RecordDepthReadable(VkCommandBuffer commandBuffer, VkImage depth)
{
    VkImageMemoryBarrier barrier = MakeVulkanInfo<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
    barrier.srcAccessMask        = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask        = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout            = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barrier.newLayout            = ReadableDepthLayout;
    barrier.srcQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                = depth;
    barrier.subresourceRange     = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1}; // one mip level, one layer
    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
}

Wotah::SceneDepth::~SceneDepth()
{
    Destroy();
}

bool Wotah::SceneDepth::Create(const Device& device)
{
    Destroy(); // a second Create replaces the first instead of leaking it
    m_device = device.GetHandle();

    // Read texel by texel (texelFetch): no filtering, nothing outside the image.
    VkSamplerCreateInfo samplerInfo = MakeVulkanInfo<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
    samplerInfo.magFilter           = VK_FILTER_NEAREST;
    samplerInfo.minFilter           = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode          = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU        = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV        = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW        = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (!Succeeded(vkCreateSampler(m_device, &samplerInfo, nullptr, &m_sampler), "create the scene depth's sampler"))
    {
        return false;
    }
    return m_set.Create(device, DepthBindings);
}

void Wotah::SceneDepth::Destroy()
{
    m_set.Destroy();
    if (m_sampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(m_device, m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
    }
    m_device = VK_NULL_HANDLE;
}

void Wotah::SceneDepth::Point(const Image& depth)
{
    m_set.WriteImage(DepthBinding, depth.GetView(), m_sampler, ReadableDepthLayout);
}

void Wotah::SceneDepth::Bind(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, uint32_t setIndex) const
{
    m_set.Bind(commandBuffer, pipelineLayout, setIndex);
}
