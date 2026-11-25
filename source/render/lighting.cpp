/**
 * @file lighting.cpp
 * Packs the scene's lights into the uniform buffer shaders/lighting.glsl reads, and sets up its descriptor set.
 */
#include "render/lighting.hpp"

#include "core/logger.hpp"
#include "render/device.hpp"
#include "render/vulkan_utils.hpp"

#include <glm/geometric.hpp>
#include <glm/vec4.hpp>
#include <vulkan/vulkan.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace
{
    /** Below this squared length a direction counts as zero. */
    constexpr float NearlyZeroSquared = 1.0e-12f;

    /**
     * One directional light as the shader reads it (std140: every member a 16-byte vec4). Matches DirectionalLight in
     * shaders/lighting.glsl.
     */
    struct GpuDirectionalLight
    {
        /** xyz: towards the light, length 1 (the opposite of DirectionalLight::Direction); w unused. */
        glm::vec4 TowardsLight{0.0f};
        /** rgb: color times intensity; a unused. */
        glm::vec4 Radiance{0.0f};
    };

    /** The whole uniform buffer (std140); matches the SceneLights block in shaders/lighting.glsl. */
    struct GpuSceneLights
    {
        /** rgb: the ambient color; a unused. */
        glm::vec4 Ambient{0.0f};
        /** x: how many directional lights are used; yzw unused, so the array starts 16-byte aligned. */
        std::array<uint32_t, 4>                                      Count{};
        std::array<GpuDirectionalLight, Wotah::MaxDirectionalLights> Directional{};
    };

    // std140 places vec4s and arrays of vec4-made structs on 16-byte steps with no gaps (GLSL spec, 7.6.2.2), which
    // these plain structs of vec4-sized members reproduce.
    static_assert(sizeof(GpuDirectionalLight) == 32);
    static_assert(offsetof(GpuSceneLights, Directional) == 32);
    static_assert(sizeof(GpuSceneLights) == 32 + 32 * Wotah::MaxDirectionalLights);

    /** The lights packed for the GPU; empty (and logged) when they do not fit or a direction is missing. */
    std::optional<GpuSceneLights> Pack(const Wotah::SceneLights& lights)
    {
        if (lights.DirectionalLights.size() > Wotah::MaxDirectionalLights)
        {
            Wotah::Logger::Error("A scene has at most {} directional lights, not {}", Wotah::MaxDirectionalLights,
                                 lights.DirectionalLights.size());
            return std::nullopt;
        }

        GpuSceneLights packed;
        packed.Ambient  = glm::vec4(lights.AmbientColor, 0.0f);
        packed.Count[0] = static_cast<uint32_t>(lights.DirectionalLights.size()); // checked above
        for (std::size_t index = 0; index < lights.DirectionalLights.size(); ++index)
        {
            const Wotah::DirectionalLight& light = lights.DirectionalLights[index];
            if (!(glm::dot(light.Direction, light.Direction) >= NearlyZeroSquared)) // also refuses NaN
            {
                Wotah::Logger::Error("Directional light {} has no direction", index);
                return std::nullopt;
            }
            packed.Directional[index].TowardsLight = glm::vec4(-glm::normalize(light.Direction), 0.0f);
            packed.Directional[index].Radiance     = glm::vec4(light.Color * light.Intensity, 0.0f);
        }
        return packed;
    }
} // namespace

glm::vec3 Wotah::MakeLightDirection(float azimuth, float elevation)
{
    // Towards the light: north is -z and east +x, so the compass angle turns from -z towards +x; then tilt up.
    const glm::vec3 towardsLight{std::sin(azimuth) * std::cos(elevation), std::sin(elevation),
                                 -std::cos(azimuth) * std::cos(elevation)};
    return -towardsLight;
}

Wotah::Lighting::~Lighting()
{
    Destroy();
}

bool Wotah::Lighting::Create(const Device& device)
{
    Destroy(); // a second Create replaces the first instead of leaking it
    m_device = device.GetHandle();

    if (!m_buffer.Create(device, sizeof(GpuSceneLights), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, BufferMemory::CpuToGpu))
    {
        return false;
    }

    // One uniform buffer at binding 0, read by the fragment shaders.
    VkDescriptorSetLayoutBinding binding{};
    binding.binding         = 0;
    binding.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo =
        Wotah::MakeVulkanInfo<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings    = &binding;
    if (!Succeeded(vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_setLayout),
                   "create the lights' descriptor set layout"))
    {
        return false;
    }

    // A pool with room for exactly this one set.
    VkDescriptorPoolSize poolSize{};
    poolSize.type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = 1;
    VkDescriptorPoolCreateInfo poolInfo =
        Wotah::MakeVulkanInfo<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
    poolInfo.maxSets       = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes    = &poolSize;
    if (!Succeeded(vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_pool), "create the lights' descriptor pool"))
    {
        return false;
    }

    VkDescriptorSetAllocateInfo setInfo =
        Wotah::MakeVulkanInfo<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    setInfo.descriptorPool     = m_pool;
    setInfo.descriptorSetCount = 1;
    setInfo.pSetLayouts        = &m_setLayout;
    if (!Succeeded(vkAllocateDescriptorSets(m_device, &setInfo, &m_set), "allocate the lights' descriptor set"))
    {
        return false;
    }

    // The set points at the buffer once; afterwards only the buffer's bytes change.
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer          = m_buffer.GetHandle();
    bufferInfo.range           = sizeof(GpuSceneLights);
    VkWriteDescriptorSet write = Wotah::MakeVulkanInfo<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
    write.dstSet               = m_set;
    write.dstBinding           = 0;
    write.descriptorCount      = 1;
    write.descriptorType       = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo          = &bufferInfo;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);

    return Update(SceneLights{}); // defined content before the first frame
}

void Wotah::Lighting::Destroy()
{
    if (m_device != VK_NULL_HANDLE)
    {
        // The pool frees the set; both accept VK_NULL_HANDLE (Vulkan spec).
        vkDestroyDescriptorPool(m_device, m_pool, nullptr);
        vkDestroyDescriptorSetLayout(m_device, m_setLayout, nullptr);
    }
    m_buffer.Destroy();
    m_device    = VK_NULL_HANDLE;
    m_setLayout = VK_NULL_HANDLE;
    m_pool      = VK_NULL_HANDLE;
    m_set       = VK_NULL_HANDLE;
}

bool Wotah::Lighting::Update(const SceneLights& lights)
{
    const std::optional<GpuSceneLights> packed = Pack(lights);
    if (!packed)
    {
        return false;
    }
    return m_buffer.Write(std::as_bytes(std::span(&*packed, 1)));
}

void Wotah::Lighting::Bind(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout) const
{
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &m_set, 0, nullptr);
}
