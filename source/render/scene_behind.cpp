/**
 * @file scene_behind.cpp
 * The depth and color copies, the samplers, the descriptor set, and the recorded copies with their layout changes.
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

    /** The frame's depth (every format the program draws with is depth only, image.cpp): every depth write finishes
     * first. */
    constexpr Wotah::LayoutChange DepthToCopySource{
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT,
    };

    /** The frame's depth, copied: the drawing that follows tests and writes it again once the copy has read it. */
    constexpr Wotah::LayoutChange DepthBackToTarget{
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_ACCESS_TRANSFER_READ_BIT,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT,
    };

    /** The depth's copy, before and after, as the color's. */
    constexpr Wotah::LayoutChange DepthCopyToTarget{
        VK_IMAGE_LAYOUT_UNDEFINED,    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,  0,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT,
    };
    constexpr Wotah::LayoutChange DepthCopyToReadable{
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT,
    };

    /** Copies a whole image of extent, one aspect of it, from source to target (in transfer layouts). */
    void RecordCopy(VkCommandBuffer commandBuffer, VkImage source, VkImage target, VkExtent2D extent,
                    VkImageAspectFlags aspect)
    {
        VkImageCopy region{};
        region.srcSubresource = {aspect, 0, 0, 1};
        region.dstSubresource = {aspect, 0, 0, 1};
        region.extent         = {extent.width, extent.height, 1};
        vkCmdCopyImage(commandBuffer, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    }
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
        !m_color.Create(device, depth.GetExtent(), colorFormat, CopyUsage) ||
        !m_depth.Create(device, depth.GetExtent(), depth.GetFormat(), CopyUsage) ||
        !m_set.Create(device, BehindBindings))
    {
        return false;
    }
    m_set.WriteImage(SceneDepthBinding, m_depth.GetView(), m_depthSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    m_set.WriteImage(SceneColorBinding, m_color.GetView(), m_colorSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return true;
}

void Wotah::SceneBehind::Destroy()
{
    m_set.Destroy();
    m_depth.Destroy();
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
    RecordLayoutChange(commandBuffer, depth.GetHandle(), DepthToCopySource);
    RecordLayoutChange(commandBuffer, m_depth.GetHandle(), DepthCopyToTarget);

    const VkExtent2D extent = m_color.GetExtent();
    RecordCopy(commandBuffer, frame, m_color.GetHandle(), extent, VK_IMAGE_ASPECT_COLOR_BIT);
    RecordCopy(commandBuffer, depth.GetHandle(), m_depth.GetHandle(), extent, VK_IMAGE_ASPECT_DEPTH_BIT);

    RecordLayoutChange(commandBuffer, frame, FrameBackToTarget);
    RecordLayoutChange(commandBuffer, m_color.GetHandle(), CopyToReadable);
    RecordLayoutChange(commandBuffer, depth.GetHandle(), DepthBackToTarget);
    RecordLayoutChange(commandBuffer, m_depth.GetHandle(), DepthCopyToReadable);
}

void Wotah::SceneBehind::Bind(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, uint32_t setIndex) const
{
    m_set.Bind(commandBuffer, pipelineLayout, setIndex);
}
