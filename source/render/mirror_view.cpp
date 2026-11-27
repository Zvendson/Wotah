/**
 * @file mirror_view.cpp
 * The mirrored view's matrix, and the mirror's images, samplers, set and renderings.
 */
#include "render/mirror_view.hpp"

#include "render/camera.hpp"
#include "render/device.hpp"
#include "render/vulkan_utils.hpp"

#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <glm/vec4.hpp>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
    /** The layout the mirror's depth is in while the water's shaders read it (nothing writes it then). */
    constexpr VkImageLayout MirrorDepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    /** How far above the surface the camera must be for a mirrored view: a centimetre. */
    constexpr float MirrorClearance = 0.01f;

    /** The two bindings, read by fragment shaders: the colors, then the depth. */
    constexpr std::array<Wotah::DescriptorBinding, 2> MirrorBindings{{
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT},
    }};

    /** Both images are drawn into and then read by shaders. */
    constexpr VkImageUsageFlags ColorUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    constexpr VkImageUsageFlags DepthUsage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    /** Depth cleared to the far plane (1.0 in Vulkan's 0..1 range): where it stays 1, only the sky was drawn. */
    constexpr float FarDepth = 1.0f;

    /** Before drawing: the old content is not needed, but the last frame's shaders must be done reading it. */
    constexpr Wotah::LayoutChange ColorToTarget{
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        0,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
    };
    constexpr Wotah::LayoutChange DepthToTarget{
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        0,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT,
    };

    /** After drawing: every write finishes before fragment shaders read the images. */
    constexpr Wotah::LayoutChange ColorToReadable{
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
    };
    constexpr Wotah::LayoutChange DepthToReadable{
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        MirrorDepthLayout,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT,
    };

    /**
     * The reflection in the horizontal plane at height: y becomes 2 height - y, x and z stay (column-major: the second
     * column flips y, the fourth moves it by 2 height).
     */
    glm::mat4 MakeReflection(float height)
    {
        glm::mat4 reflection(1.0f);
        reflection[1][1] = -1.0f;
        reflection[3][1] = 2.0f * height;
        return reflection;
    }

    /** -1, 0 or 1 by the sign of value. */
    float GetSign(float value)
    {
        return static_cast<float>((value > 0.0f) - (value < 0.0f));
    }

    /** MirrorScale of a frame's side, at least one pixel. */
    uint32_t ScaleSide(uint32_t side)
    {
        return std::max(1u, static_cast<uint32_t>(std::lround(static_cast<float>(side) * Wotah::MirrorScale)));
    }
} // namespace

std::optional<glm::mat4> Wotah::MakeMirrorViewProjection(const Camera& camera, float surfaceHeight)
{
    if (!(camera.GetPosition().y >= surfaceHeight + MirrorClearance))
    {
        return std::nullopt;
    }

    // The world mirrored in the surface, then seen through the camera: what the camera's mirror image sees.
    const glm::mat4 view       = camera.GetView() * MakeReflection(surfaceHeight);
    glm::mat4       projection = camera.GetProjection();

    // The surface as a plane in the mirrored view's space, its positive side what lies above it in the world
    // (y - surfaceHeight >= 0); planes turn with the inverse transpose of what turns points.
    const glm::vec4 surface = glm::transpose(glm::inverse(view)) * glm::vec4(0.0f, 1.0f, 0.0f, -surfaceHeight);

    // Lengyel's oblique near plane, for a depth range of 0 to 1: the corner of the view's box opposite the plane
    // (in clip space by the plane's signs there, then back in view space), and the plane scaled so the far plane
    // passes through that corner; it becomes the projection's depth row, so depth 0 lies in the surface.
    const glm::vec4 inClip = glm::transpose(glm::inverse(projection)) * surface;
    const glm::vec4 corner = glm::inverse(projection) * glm::vec4(GetSign(inClip.x), GetSign(inClip.y), 1.0f, 1.0f);
    const float     reach  = glm::dot(surface, corner);
    if (!(reach > 0.0f))
    {
        return std::nullopt; // the view's box lies wholly below the surface: nothing to mirror
    }
    const glm::vec4 depthRow = surface / reach;
    for (int column = 0; column < 4; ++column)
    {
        projection[column][2] = depthRow[column];
    }
    return projection * view;
}

Wotah::MirrorView::~MirrorView()
{
    Destroy();
}

bool Wotah::MirrorView::Create(const Device& device, VkExtent2D frameExtent, VkFormat colorFormat, VkFormat depthFormat)
{
    Destroy(); // a second Create replaces the first instead of leaking it
    m_device                = device.GetHandle();
    const VkExtent2D extent = {ScaleSide(frameExtent.width), ScaleSide(frameExtent.height)};
    m_colorSampler          = CreateClampedSampler(m_device, VK_FILTER_LINEAR, "create the mirror's color sampler");
    m_depthSampler          = CreateClampedSampler(m_device, VK_FILTER_NEAREST, "create the mirror's depth sampler");
    if (m_colorSampler == VK_NULL_HANDLE || m_depthSampler == VK_NULL_HANDLE ||
        !m_color.Create(device, extent, colorFormat, ColorUsage) ||
        !m_depth.Create(device, extent, depthFormat, DepthUsage) || !m_set.Create(device, MirrorBindings))
    {
        return false;
    }
    m_set.WriteImage(MirrorColorBinding, m_color.GetView(), m_colorSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    m_set.WriteImage(MirrorDepthBinding, m_depth.GetView(), m_depthSampler, MirrorDepthLayout);
    return true;
}

void Wotah::MirrorView::Destroy()
{
    m_set.Destroy();
    m_depth.Destroy();
    m_color.Destroy();
    for (VkSampler* sampler : {&m_colorSampler, &m_depthSampler})
    {
        if (*sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(m_device, *sampler, nullptr);
            *sampler = VK_NULL_HANDLE;
        }
    }
    m_device = VK_NULL_HANDLE;
}

void Wotah::MirrorView::Begin(VkCommandBuffer commandBuffer) const
{
    RecordLayoutChange(commandBuffer, m_color.GetHandle(), ColorToTarget);
    RecordLayoutChange(commandBuffer, m_depth.GetHandle(), DepthToTarget);

    DrawTarget color;
    color.View        = m_color.GetView();
    color.Layout      = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.Clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    DrawTarget depth; // kept: the water reads where only sky shows
    depth.View                     = m_depth.GetView();
    depth.Layout                   = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.Clear.depthStencil.depth = FarDepth;
    RecordBeginDrawing(commandBuffer, m_color.GetExtent(), color, depth);
}

void Wotah::MirrorView::End(VkCommandBuffer commandBuffer) const
{
    vkCmdEndRendering(commandBuffer);
    RecordLayoutChange(commandBuffer, m_color.GetHandle(), ColorToReadable);
    RecordLayoutChange(commandBuffer, m_depth.GetHandle(), DepthToReadable);
}

void Wotah::MirrorView::Bind(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, uint32_t setIndex) const
{
    m_set.Bind(commandBuffer, pipelineLayout, setIndex);
}
