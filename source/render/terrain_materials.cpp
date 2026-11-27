/**
 * @file terrain_materials.cpp
 * The ground's textures: making them, filling every mip level with the compute shader, their sampler and set.
 */
#include "render/terrain_materials.hpp"

#include "core/logger.hpp"
#include "render/device.hpp"
#include "render/pipeline.hpp"
#include "render/vulkan_utils.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstdint>

namespace
{
    /** The compute shader that makes the textures (shaders/terrain_material.comp), compiled by the build. */
    constexpr uint32_t TerrainMaterialShader[] = {
#include "terrain_material.comp.inc"
    };

    /** Both textures: 8 bits per channel, written by the compute shader, read filtered by the land's shader. */
    constexpr VkFormat             TextureFormat = VK_FORMAT_R8G8B8A8_UNORM;
    constexpr VkImageUsageFlags    TextureUsage  = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    constexpr VkFormatFeatureFlags NeededFeatures =
        VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;

    /** The compute shader's groups: 8 x 8 texels of one layer (its local_size). */
    constexpr uint32_t GroupSide = 8;

    /** The most anisotropic filtering asked for: 16 samples along a slant, the most that GPUs commonly offer. */
    constexpr float MostAnisotropy = 16.0f;

    /** The compute shader's two passes per level (its push.pass): color, height and roughness first, then slopes. */
    constexpr uint32_t LookPass  = 0;
    constexpr uint32_t SlopePass = 1;

    /** What the compute shader gets per pass; matches its PushConstants block. */
    struct FillPushConstants
    {
        uint32_t Size = 0;
        uint32_t Pass = LookPass;
    };

    constexpr std::array<VkPushConstantRange, 1> FillPushConstantRanges{
        {{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(FillPushConstants)}}};

    /** While filling: both textures' level being filled, read and written by the compute shader. */
    constexpr std::array<Wotah::DescriptorBinding, 2> FillBindings{{
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
    }};

    /** Once filled: both textures, read by fragment shaders. */
    constexpr std::array<Wotah::DescriptorBinding, 2> ReadBindings{{
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT},
    }};

    /** Before filling: from nothing to written by the compute shader (every level). */
    constexpr Wotah::LayoutChange ToFilling{
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL,
        0,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
    };

    /** Once filled: every write finished before fragment shaders read the textures. */
    constexpr Wotah::LayoutChange ToReadable{
        VK_IMAGE_LAYOUT_GENERAL,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
    };

    /** A view of one mip level of an image (every layer), for the compute shader; freed when it goes. */
    class MipView final
    {
    public:
        /** Makes the view; Get is VK_NULL_HANDLE (and the reason logged) on failure. */
        MipView(VkDevice device, const Wotah::Image& image, uint32_t level)
            : m_device(device)
        {
            VkImageViewCreateInfo info =
                Wotah::MakeVulkanInfo<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
            info.image            = image.GetHandle();
            info.viewType         = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            info.format           = image.GetFormat();
            info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, level, 1, 0, image.GetLayerCount()};
            if (!Wotah::Succeeded(vkCreateImageView(m_device, &info, nullptr, &m_view), "create a mip level's view"))
            {
                m_view = VK_NULL_HANDLE;
            }
        }

        ~MipView() { vkDestroyImageView(m_device, m_view, nullptr); }

        MipView(const MipView&)            = delete;
        MipView& operator=(const MipView&) = delete;

        /** The view; VK_NULL_HANDLE when making it failed. */
        [[nodiscard]] VkImageView Get() const noexcept { return m_view; }

    private:
        VkDevice    m_device = VK_NULL_HANDLE;
        VkImageView m_view   = VK_NULL_HANDLE;
    };

    /** Lets the next dispatch read (and write) what the one before wrote. */
    void RecordComputeBarrier(VkCommandBuffer commands)
    {
        VkMemoryBarrier barrier = Wotah::MakeVulkanInfo<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
        barrier.srcAccessMask   = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask   = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                             &barrier, 0, nullptr, 0, nullptr);
    }

    /** The repeating, trilinear, anisotropic sampler the land reads the textures with; VK_NULL_HANDLE on failure. */
    VkSampler CreateGroundSampler(const Wotah::Device& device, uint32_t levelCount)
    {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(device.GetGpu(), &properties);

        VkSamplerCreateInfo info = Wotah::MakeVulkanInfo<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
        info.magFilter           = VK_FILTER_LINEAR;
        info.minFilter           = VK_FILTER_LINEAR;
        info.mipmapMode          = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        info.addressModeU        = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        info.addressModeV        = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        info.addressModeW        = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        info.anisotropyEnable    = VK_TRUE; // Device turns samplerAnisotropy on
        info.maxAnisotropy       = std::min(MostAnisotropy, properties.limits.maxSamplerAnisotropy);
        info.maxLod              = static_cast<float>(levelCount);
        VkSampler sampler        = VK_NULL_HANDLE;
        if (!Wotah::Succeeded(vkCreateSampler(device.GetHandle(), &info, nullptr, &sampler),
                              "create the ground's sampler"))
        {
            return VK_NULL_HANDLE;
        }
        return sampler;
    }
} // namespace

Wotah::TerrainMaterials::~TerrainMaterials()
{
    Destroy();
}

bool Wotah::TerrainMaterials::Create(const Device& device)
{
    Destroy(); // a second Create replaces the first instead of leaking it
    m_device = device.GetHandle();

    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(device.GetGpu(), TextureFormat, &formatProperties);
    if ((formatProperties.optimalTilingFeatures & NeededFeatures) != NeededFeatures)
    {
        Logger::Error("The GPU cannot write and filter the ground's textures (8-bit RGBA)");
        return false;
    }

    const VkExtent2D extent{GroundTextureSize, GroundTextureSize};
    const uint32_t   levels = GetMipLevelCountFor(extent);
    if (!m_look.Create(device, extent, TextureFormat, TextureUsage, GroundLayerCount, levels) ||
        !m_surface.Create(device, extent, TextureFormat, TextureUsage, GroundLayerCount, levels) || !Fill(device))
    {
        return false;
    }

    m_sampler = CreateGroundSampler(device, levels);
    if (m_sampler == VK_NULL_HANDLE || !m_set.Create(device, ReadBindings))
    {
        return false;
    }
    m_set.WriteImage(GroundLookBinding, m_look.GetView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    m_set.WriteImage(GroundSurfaceBinding, m_surface.GetView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return true;
}

void Wotah::TerrainMaterials::Destroy()
{
    m_set.Destroy();
    if (m_sampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(m_device, m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
    }
    m_surface.Destroy();
    m_look.Destroy();
    m_device = VK_NULL_HANDLE;
}

void Wotah::TerrainMaterials::Bind(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                                   uint32_t setIndex) const
{
    m_set.Bind(commandBuffer, pipelineLayout, setIndex);
}

bool Wotah::TerrainMaterials::Fill(const Device& device)
{
    DescriptorSet storage;
    if (!storage.Create(device, FillBindings))
    {
        return false;
    }
    const std::array<VkDescriptorSetLayout, 1> layouts{storage.GetLayout()};
    ComputePipelineSettings                    settings;
    settings.Shader               = TerrainMaterialShader;
    settings.DescriptorSetLayouts = layouts;
    settings.PushConstants        = FillPushConstantRanges;
    ComputePipeline pipeline;
    if (!pipeline.Create(device, settings))
    {
        return false;
    }

    // One level at a time, each in its own submission: the set points at that level's views until the GPU is done.
    const uint32_t levels = m_look.GetMipLevelCount();
    for (uint32_t level = 0; level < levels; ++level)
    {
        const MipView   look(m_device, m_look, level);
        const MipView   surface(m_device, m_surface, level);
        OneTimeCommands commands;
        if (look.Get() == VK_NULL_HANDLE || surface.Get() == VK_NULL_HANDLE || !commands.Begin(device))
        {
            return false;
        }
        storage.WriteStorageImage(0, look.Get());
        storage.WriteStorageImage(1, surface.Get());

        const VkCommandBuffer buffer = commands.Get();
        if (level == 0)
        {
            RecordLayoutChange(buffer, m_look.GetHandle(), ToFilling);
            RecordLayoutChange(buffer, m_surface.GetHandle(), ToFilling);
        }
        vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.GetHandle());
        storage.Bind(buffer, pipeline.GetLayout(), 0, VK_PIPELINE_BIND_POINT_COMPUTE);
        const uint32_t size   = std::max(1u, GroundTextureSize >> level);
        const uint32_t groups = (size + GroupSide - 1) / GroupSide;
        for (const uint32_t pass : {LookPass, SlopePass})
        {
            const FillPushConstants pushConstants{size, pass};
            vkCmdPushConstants(buffer, pipeline.GetLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConstants),
                               &pushConstants);
            vkCmdDispatch(buffer, groups, groups, GroundLayerCount);
            RecordComputeBarrier(buffer);
        }
        if (level + 1 == levels)
        {
            RecordLayoutChange(buffer, m_look.GetHandle(), ToReadable);
            RecordLayoutChange(buffer, m_surface.GetHandle(), ToReadable);
        }
        if (!commands.SubmitAndWait())
        {
            return false;
        }
    }
    return true;
}
