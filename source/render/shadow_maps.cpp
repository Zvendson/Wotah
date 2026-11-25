/**
 * @file shadow_maps.cpp
 * The shadow maps' image, views and sampler, drawing into a layer, and the cascades: where they end, the stretch of
 * view each shows, and a light's view of it.
 */
#include "render/shadow_maps.hpp"

#include "core/logger.hpp"
#include "render/camera.hpp"
#include "render/device.hpp"
#include "render/vulkan_utils.hpp"

#include <glm/geometric.hpp>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>

namespace
{
    /**
     * The maps' depth format. Vulkan requires D16_UNORM to work as a depth attachment and as a sampled image on every
     * GPU (Vulkan spec, "Required Format Support"); 16 bits over the area's depth (about 92 units for the program's
     * ground) still tell apart 1.4 mm, far finer than a map pixel.
     */
    constexpr VkFormat ShadowFormat = VK_FORMAT_D16_UNORM;

    /** Depth where nothing was drawn: the far end of the area, so everything in it counts as lit. */
    constexpr float NothingDrawn = 1.0f;

    /** Below this squared length a light direction counts as zero. */
    constexpr float NearlyZeroSquared = 1.0e-12f;

    /** Above this |y| a light falls nearly straight up or down, and its map's up direction is north instead of y. */
    constexpr float NearlyVertical = 0.99f;

    /**
     * How much the cascade ends follow growing steps (each cascade a fixed factor longer) rather than even ones: 0.8,
     * the weight Zhang et al. 2006 found to work well, gives short near cascades without starving the far ones.
     */
    constexpr float GrowingStepWeight = 0.8f;

    /**
     * A cascade's radius is rounded up to this step (1/16 unit), so float noise in the camera's numbers never changes
     * the map's pixel size from one frame to the next.
     */
    constexpr float RadiusStep = 1.0f / 16.0f;

    /** A layout change of some of the maps' layers. */
    struct LayerChange
    {
        uint32_t             FirstLayer   = 0;
        uint32_t             LayerCount   = 1;
        VkImageLayout        From         = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageLayout        To           = VK_IMAGE_LAYOUT_UNDEFINED;
        VkPipelineStageFlags SourceStage  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkAccessFlags        SourceAccess = 0;
        VkPipelineStageFlags TargetStage  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        VkAccessFlags        TargetAccess = 0;
    };

    /** Records a layout change of the layers in change. */
    void ChangeLayers(VkCommandBuffer commandBuffer, VkImage image, const LayerChange& change)
    {
        VkImageMemoryBarrier barrier =
            Wotah::MakeVulkanInfo<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
        barrier.srcAccessMask       = change.SourceAccess;
        barrier.dstAccessMask       = change.TargetAccess;
        barrier.oldLayout           = change.From;
        barrier.newLayout           = change.To;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = image;
        barrier.subresourceRange    = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, change.FirstLayer, change.LayerCount};
        vkCmdPipelineBarrier(commandBuffer, change.SourceStage, change.TargetStage, 0, 0, nullptr, 0, nullptr, 1,
                             &barrier);
    }

    /** Whether every part of a vector is a number other than infinity. */
    bool IsFinite(const glm::vec3& vector)
    {
        return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
    }
} // namespace

std::optional<Wotah::CascadeEnds> Wotah::MakeCascadeEnds(float nearest, float reach)
{
    if (!(nearest > 0.0f) || !(reach > nearest) || !std::isfinite(reach))
    {
        return std::nullopt;
    }
    CascadeEnds ends{};
    for (uint32_t cascade = 0; cascade < ShadowCascadeCount; ++cascade)
    {
        const float share   = static_cast<float>(cascade + 1) / static_cast<float>(ShadowCascadeCount);
        const float even    = nearest + (reach - nearest) * share;
        const float growing = nearest * std::pow(reach / nearest, share);
        ends[cascade]       = GrowingStepWeight * growing + (1.0f - GrowingStepWeight) * even;
    }
    ends.back() = reach; // exact, whatever the rounding
    return ends;
}

std::optional<Wotah::ShadowArea> Wotah::MakeCascadeArea(const Camera& camera, float from, float to)
{
    if (!(from >= 0.0f) || !(to > from) || !std::isfinite(to))
    {
        return std::nullopt;
    }

    // The stretch's corners lie at from and to along the view, k times as far to the side (k: the slope of the view's
    // corner rays). The sphere through all of them has its middle on the view's axis where both ends are equally far:
    // (d - from)^2 + (k from)^2 = (to - d)^2 + (k to)^2 gives d = (from + to) / 2 * (1 + k^2); past the far end the
    // far corners alone set the size.
    const float halfHeight   = std::tan(camera.GetFieldOfView() * 0.5f);
    const float halfWidth    = halfHeight * camera.GetAspectRatio();
    const float slopeSquared = halfHeight * halfHeight + halfWidth * halfWidth;
    const float middle       = std::min((from + to) * 0.5f * (1.0f + slopeSquared), to);
    const float radius       = std::sqrt((to - middle) * (to - middle) + to * to * slopeSquared);

    const glm::vec3 forward = glm::normalize(camera.GetTarget() - camera.GetPosition());
    ShadowArea      area;
    area.Center = camera.GetPosition() + forward * middle;
    area.Radius = std::ceil(radius / RadiusStep) * RadiusStep;
    return area;
}

std::optional<glm::mat4> Wotah::MakeShadowViewProjection(const glm::vec3& direction, const ShadowArea& shown,
                                                         const ShadowArea& casters)
{
    // The negated comparisons also refuse NaN.
    if (!(shown.Radius > 0.0f) || !std::isfinite(shown.Radius) || !IsFinite(shown.Center) || !(casters.Radius > 0.0f) ||
        !std::isfinite(casters.Radius) || !IsFinite(casters.Center) ||
        !(glm::dot(direction, direction) >= NearlyZeroSquared) || !IsFinite(direction))
    {
        return std::nullopt;
    }

    // The map's axes: right and up across it, along the light into it. They depend only on the light's direction,
    // so for a light that stands still, map pixels lie at the same places in the world in every frame.
    const glm::vec3 along = glm::normalize(direction);
    const glm::vec3 reference =
        std::abs(along.y) > NearlyVertical ? glm::vec3(0.0f, 0.0f, -1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 right = glm::normalize(glm::cross(along, reference));
    const glm::vec3 up    = glm::cross(right, along);

    // Across: the shown sphere, its middle moved onto the map pixel grid, so following the camera moves the map in
    // whole pixels and its shadows do not shimmer.
    const float pixel   = GetShadowTexelSize(shown);
    const float centerX = std::floor(glm::dot(shown.Center, right) / pixel) * pixel;
    const float centerY = std::floor(glm::dot(shown.Center, up) / pixel) * pixel;

    // Along: from wherever the casters' sphere or the shown one starts to wherever either ends, so everything that can
    // throw a shadow into the shown part is on the map, even when it stands outside that part.
    const float shownDepth   = glm::dot(shown.Center, along);
    const float castersDepth = glm::dot(casters.Center, along);
    const float nearest      = std::min(shownDepth - shown.Radius, castersDepth - casters.Radius);
    const float farthest     = std::max(shownDepth + shown.Radius, castersDepth + casters.Radius);
    const float depthRange   = farthest - nearest;

    // Written out instead of lookAt and ortho, so the snapped middle is exact: each row maps a world point to one of
    // the map's coordinates (glm is column-major: matrix[column][row]).
    glm::mat4 toMap(0.0f);
    for (int axis = 0; axis < 3; ++axis)
    {
        toMap[axis][0] = right[axis] / shown.Radius;
        toMap[axis][1] = up[axis] / shown.Radius;
        toMap[axis][2] = along[axis] / depthRange;
    }
    toMap[3][0] = -centerX / shown.Radius;
    toMap[3][1] = -centerY / shown.Radius;
    toMap[3][2] = -nearest / depthRange;
    toMap[3][3] = 1.0f;
    return toMap;
}

float Wotah::GetShadowTexelSize(const ShadowArea& area) noexcept
{
    return 2.0f * area.Radius / static_cast<float>(ShadowMapSize);
}

Wotah::ShadowMaps::~ShadowMaps()
{
    Destroy();
}

bool Wotah::ShadowMaps::Create(const Device& device, uint32_t lightCount)
{
    Destroy(); // a second Create replaces the first instead of leaking it
    m_device = device.GetHandle();

    // ---- The image: the cascades of every light, and a view of each layer to draw into ----
    const uint32_t layerCount = std::max(lightCount, 1u) * ShadowCascadeCount;
    if (!m_image.Create(device, {ShadowMapSize, ShadowMapSize}, ShadowFormat,
                        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                            VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                        layerCount))
    {
        return false;
    }
    m_layerViews.reserve(layerCount);
    for (uint32_t layer = 0; layer < layerCount; ++layer)
    {
        VkImageViewCreateInfo viewInfo =
            Wotah::MakeVulkanInfo<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
        viewInfo.image            = m_image.GetHandle();
        viewInfo.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format           = ShadowFormat;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, layer, 1};
        VkImageView view          = VK_NULL_HANDLE;
        if (!Succeeded(vkCreateImageView(m_device, &viewInfo, nullptr, &view), "create a shadow map view"))
        {
            return false;
        }
        m_layerViews.push_back(view);
    }

    // ---- The sampler: compares instead of returning the depth ----
    // Linear filtering compares the four nearest map pixels and blends the answers, which softens the shadow's edge;
    // Vulkan does not require it for depth formats, so without it each pixel answers alone.
    VkFormatProperties properties;
    vkGetPhysicalDeviceFormatProperties(device.GetGpu(), ShadowFormat, &properties);
    const bool canBlend = (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
    if (!canBlend)
    {
        Logger::Warning("The GPU cannot blend shadow map pixels; shadow edges are a little harder");
    }
    const VkFilter      filter      = canBlend ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    VkSamplerCreateInfo samplerInfo = Wotah::MakeVulkanInfo<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
    samplerInfo.magFilter           = filter;
    samplerInfo.minFilter           = filter;
    samplerInfo.mipmapMode          = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    // Outside the map reads the border: white, the far end, so whatever lies outside the area is lit.
    samplerInfo.addressModeU  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.borderColor   = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.compareEnable = VK_TRUE;
    samplerInfo.compareOp     = VK_COMPARE_OP_LESS_OR_EQUAL; // lit where the surface is not behind the map's depth
    samplerInfo.maxLod        = 0.0f;
    if (!Succeeded(vkCreateSampler(m_device, &samplerInfo, nullptr, &m_sampler), "create the shadow map sampler"))
    {
        return false;
    }

    // ---- First content: every layer "nothing casts a shadow", ready for the shaders ----
    OneTimeCommands setup;
    if (!setup.Begin(device))
    {
        return false;
    }
    LayerChange toClear;
    toClear.LayerCount   = layerCount;
    toClear.To           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toClear.TargetStage  = VK_PIPELINE_STAGE_TRANSFER_BIT;
    toClear.TargetAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
    ChangeLayers(setup.Get(), m_image.GetHandle(), toClear);

    const VkClearDepthStencilValue nothing{NothingDrawn, 0};
    const VkImageSubresourceRange  allLayers{VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, layerCount};
    vkCmdClearDepthStencilImage(setup.Get(), m_image.GetHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &nothing, 1,
                                &allLayers);

    LayerChange toRead;
    toRead.LayerCount   = layerCount;
    toRead.From         = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toRead.To           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toRead.SourceStage  = VK_PIPELINE_STAGE_TRANSFER_BIT;
    toRead.SourceAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
    toRead.TargetStage  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    toRead.TargetAccess = VK_ACCESS_SHADER_READ_BIT;
    ChangeLayers(setup.Get(), m_image.GetHandle(), toRead);
    return setup.SubmitAndWait();
}

void Wotah::ShadowMaps::Destroy()
{
    if (m_device != VK_NULL_HANDLE)
    {
        // Views before the image they show; both destroy calls accept VK_NULL_HANDLE (Vulkan spec).
        for (VkImageView view : m_layerViews)
        {
            vkDestroyImageView(m_device, view, nullptr);
        }
        vkDestroySampler(m_device, m_sampler, nullptr);
    }
    m_layerViews.clear();
    m_image.Destroy();
    m_device  = VK_NULL_HANDLE;
    m_sampler = VK_NULL_HANDLE;
}

uint32_t Wotah::ShadowMaps::GetLightCapacity() const noexcept
{
    return m_image.GetLayerCount() / ShadowCascadeCount;
}

bool Wotah::ShadowMaps::BeginLayer(VkCommandBuffer commandBuffer, uint32_t layer) const
{
    if (layer >= m_layerViews.size())
    {
        Logger::Error("There is no shadow map layer {}; there are {}", layer, m_layerViews.size());
        return false;
    }

    // The old content is not needed (it is cleared), but the last frame's shaders must be done reading it.
    LayerChange toDraw;
    toDraw.FirstLayer   = layer;
    toDraw.To           = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    toDraw.SourceStage  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    toDraw.TargetStage  = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    toDraw.TargetAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    ChangeLayers(commandBuffer, m_image.GetHandle(), toDraw);

    VkRenderingAttachmentInfo depth =
        Wotah::MakeVulkanInfo<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
    depth.imageView                     = m_layerViews[layer];
    depth.imageLayout                   = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.loadOp                        = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp                       = VK_ATTACHMENT_STORE_OP_STORE; // the scene reads it afterwards
    depth.clearValue.depthStencil.depth = NothingDrawn;

    const VkExtent2D extent{ShadowMapSize, ShadowMapSize};
    VkRenderingInfo  rendering = Wotah::MakeVulkanInfo<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
    rendering.renderArea       = {{0, 0}, extent};
    rendering.layerCount       = 1;
    rendering.pDepthAttachment = &depth;
    vkCmdBeginRendering(commandBuffer, &rendering);

    const VkViewport viewport{0.0f, 0.0f, static_cast<float>(ShadowMapSize), static_cast<float>(ShadowMapSize),
                              0.0f, 1.0f};
    const VkRect2D   scissor{{0, 0}, extent};
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    return true;
}

void Wotah::ShadowMaps::EndLayer(VkCommandBuffer commandBuffer, uint32_t layer) const
{
    vkCmdEndRendering(commandBuffer);

    // The depths written must be done and visible before the scene's fragment shaders compare against them.
    LayerChange toRead;
    toRead.FirstLayer   = layer;
    toRead.From         = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    toRead.To           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toRead.SourceStage  = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    toRead.SourceAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    toRead.TargetStage  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    toRead.TargetAccess = VK_ACCESS_SHADER_READ_BIT;
    ChangeLayers(commandBuffer, m_image.GetHandle(), toRead);
}
