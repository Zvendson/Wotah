/**
 * @file scene_behind.cpp
 * The color copy, the samplers, the descriptor set, and the recorded copy with its layout changes.
 */
#include "render/scene_behind.hpp"

#include "render/device.hpp"
#include "render/vulkan_utils.hpp"

#include <vulkan/vulkan.h>

#include <array>

namespace
{
    /** The two bindings, read by fragment shaders: the depth, then the colors. */
    constexpr std::array<Wotah::DescriptorBinding, 2> BehindBindings{{
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT},
    }};

    /** The copy's uses: written by the copy, read by shaders. */
    constexpr VkImageUsageFlags CopyUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    /** The frame, once drawn: its color writes finish before the copy reads it. */
    constexpr Wotah::LayoutChange FrameToCopySource{
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
    };

    /** The frame, copied: the drawing that follows writes it again once the copy has read it. */
    constexpr Wotah::LayoutChange FrameBackToTarget{
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_ACCESS_TRANSFER_READ_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
    };

    /** The copy, before: its old content is not needed, but the last frame's shaders must be done reading it. */
    constexpr Wotah::LayoutChange CopyToTarget{
        VK_IMAGE_LAYOUT_UNDEFINED,    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,  0,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
    };

    /** The copy, after: written before fragment shaders read it. */
    constexpr Wotah::LayoutChange CopyToReadable{
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
    };

    /** The depth: every depth write so far finishes before fragment shaders read it and later depth tests use it. */
    constexpr Wotah::LayoutChange DepthToReadable{
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        Wotah::ReadableDepthLayout,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT,
    };
} // namespace

Wotah::SceneBehind::~SceneBehind()
{
    Destroy();
}

bool Wotah::SceneBehind::Create(const Device& device, const Image& depth, VkFormat colorFormat)
{
    Destroy(); // a second Create replaces the first instead of leaking it
    m_device = device.GetHandle();

    // The depth read texel by texel (never blended between two surfaces), the colors between texels (where the water
    // bends the view).
    m_depthSampler = CreateClampedSampler(m_device, VK_FILTER_NEAREST, "create the scene depth's sampler");
    m_colorSampler = CreateClampedSampler(m_device, VK_FILTER_LINEAR, "create the scene colors' sampler");
    if (m_depthSampler == VK_NULL_HANDLE || m_colorSampler == VK_NULL_HANDLE ||
        !m_color.Create(device, depth.GetExtent(), colorFormat, CopyUsage) || !m_set.Create(device, BehindBindings))
    {
        return false;
    }
    m_set.WriteImage(SceneDepthBinding, depth.GetView(), m_depthSampler, ReadableDepthLayout);
    m_set.WriteImage(SceneColorBinding, m_color.GetView(), m_colorSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return true;
}

void Wotah::SceneBehind::Destroy()
{
    m_set.Destroy();
    m_color.Destroy();
    for (VkSampler* sampler : {&m_depthSampler, &m_colorSampler})
    {
        if (*sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(m_device, *sampler, nullptr);
            *sampler = VK_NULL_HANDLE;
        }
    }
    m_device = VK_NULL_HANDLE;
}

void Wotah::SceneBehind::Record(VkCommandBuffer commandBuffer, VkImage frame, const Image& depth) const
{
    RecordLayoutChange(commandBuffer, frame, FrameToCopySource);
    RecordLayoutChange(commandBuffer, m_color.GetHandle(), CopyToTarget);

    const VkExtent2D extent = m_color.GetExtent();
    VkImageCopy      region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.extent         = {extent.width, extent.height, 1};
    vkCmdCopyImage(commandBuffer, frame, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_color.GetHandle(),
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    RecordLayoutChange(commandBuffer, frame, FrameBackToTarget);
    RecordLayoutChange(commandBuffer, m_color.GetHandle(), CopyToReadable);
    RecordLayoutChange(commandBuffer, depth.GetHandle(), DepthToReadable);
}

void Wotah::SceneBehind::Bind(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, uint32_t setIndex) const
{
    m_set.Bind(commandBuffer, pipelineLayout, setIndex);
}
