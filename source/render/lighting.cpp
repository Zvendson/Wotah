/**
 * @file lighting.cpp
 * Packs the scene's lights into the uniform buffer shaders/lighting.glsl reads, and sets up its descriptor set; places
 * lights in the sky and works out how much sunlight crosses the air.
 */
#include "render/lighting.hpp"

#include "core/logger.hpp"
#include "render/camera.hpp"
#include "render/device.hpp"
#include "render/vulkan_utils.hpp"

#include <glm/common.hpp>
#include <glm/exponential.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec4.hpp>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <span>

namespace
{
    /** Below this squared length a direction counts as zero. */
    constexpr float NearlyZeroSquared = 1.0e-12f;

    /**
     * How much the air dims red, green and blue light crossing it once straight up from sea level (its optical depth):
     * the air's own scattering, about 0.05, 0.10 and 0.22 at 650, 550 and 450 nm (Bucholtz 1995, "Rayleigh-scattering
     * calculations for the terrestrial atmosphere"). Haze, which dims every color more, is left out: a very clear day,
     * so a sun on the horizon still gives a red glow (with haze it went black).
     */
    constexpr glm::vec3 AirDimming{0.05f, 0.10f, 0.22f};

    /**
     * Kasten and Young's fit of how many times more air light crosses at an elevation than straight up (1989, "Revised
     * optical air mass tables and approximation formula"): 1 / (sin h + a (h + b)^c), h in degrees. Unlike 1 / sin h,
     * it stays right down to the horizon (about 38 there).
     */
    constexpr float AirMassScale        = 0.50572f;
    constexpr float AirMassShiftDegrees = 6.07995f;
    constexpr float AirMassPower        = -1.6364f;

    /** The highest a sun stands: straight overhead. */
    constexpr float Overhead = std::numbers::pi_v<float> / 2.0f;

    /**
     * One directional light as the shader reads it (std140: vec4s and a mat4, all on 16-byte steps). Matches
     * DirectionalLight in shaders/lighting.glsl.
     */
    struct GpuDirectionalLight
    {
        /** xyz: towards the light, length 1 (the opposite of DirectionalLight::Direction); w unused. */
        glm::vec4 TowardsLight{0.0f};
        /** rgb: color times intensity; a unused. */
        glm::vec4 Radiance{0.0f};
        /** x: 1 when the light casts shadows (its maps are the layers GetShadowLayer(index, cascade)), else 0. */
        glm::vec4 Shadow{0.0f};
        /** World space to each cascade's shadow map (MakeShadowViewProjection), nearest first. */
        std::array<glm::mat4, Wotah::ShadowCascadeCount> CascadeViews{};
    };

    /** The whole uniform buffer (std140); matches the SceneLights block in shaders/lighting.glsl. */
    struct GpuSceneLights
    {
        /** rgb: the ambient color; a unused. */
        glm::vec4 Ambient{0.0f};
        /** rgb: the light the ground throws back up (SceneLights::GroundBounce); a unused. */
        glm::vec4 GroundBounce{0.0f};
        /** rgb: the color far things fade into (SceneLights::HazeColor); a unused. */
        glm::vec4 HazeColor{0.0f};
        /** x: how many directional lights are used; yzw unused, so the next member starts 16-byte aligned. */
        std::array<uint32_t, 4> Count{};
        /** Where each cascade ends, along the camera's view (MakeCascadeEnds); 0 everywhere without shadows. */
        glm::vec4 CascadeEnds{0.0f};
        /** How many world units one map pixel covers in each cascade. */
        glm::vec4 CascadeTexels{0.0f};
        /** x: how much of a cascade's far end blends into the next (CascadeBlendShare); yzw unused. */
        glm::vec4                                                    CascadeBlend{0.0f};
        std::array<GpuDirectionalLight, Wotah::MaxDirectionalLights> Directional{};
    };

    // std140 places vec4s and arrays of vec4-made structs on 16-byte steps with no gaps (GLSL spec, 7.6.2.2), which
    // these plain structs of vec4-sized members reproduce.
    static_assert(Wotah::ShadowCascadeCount == 4, "CascadeEnds and CascadeTexels hold one cascade per vec4 component");
    static_assert(sizeof(GpuDirectionalLight) == 48 + 64 * Wotah::ShadowCascadeCount);
    static_assert(offsetof(GpuDirectionalLight, CascadeViews) == 48);
    static_assert(offsetof(GpuSceneLights, Directional) == 112);
    static_assert(sizeof(GpuSceneLights) == 112 + sizeof(GpuDirectionalLight) * Wotah::MaxDirectionalLights);

    /**
     * The share of a cascade at its far end where it blends into the next one, so the change of map pixel size shows
     * no line; the next cascade starts that much earlier to cover it. The last cascade fades its shadows out there.
     * Must match the shader, which reads it from the buffer.
     */
    constexpr float CascadeBlendShare = 0.1f;

    /** Each cascade's stretch of view, nearest first; all empty when the scene has no shadows or the camera none. */
    using CascadeAreas = std::array<std::optional<Wotah::ShadowArea>, Wotah::ShadowCascadeCount>;

    /** The descriptor set's bindings: the light buffer, and every light's shadow map. */
    constexpr uint32_t LightBufferBinding = 0;
    constexpr uint32_t ShadowMapsBinding  = 1;

    /** Whether a light gives no light at all, so its shadows would not show. */
    bool IsDark(const glm::vec3& radiance)
    {
        return !(radiance.r > 0.0f || radiance.g > 0.0f || radiance.b > 0.0f);
    }

    /**
     * The cascades the camera needs: where each ends (written into packed) and the stretch each shows. Each cascade
     * after the first starts where the one before begins blending, so the blend has both maps.
     */
    CascadeAreas MakeCascades(const Wotah::SceneLights& lights, const Wotah::Camera& camera, GpuSceneLights& packed)
    {
        CascadeAreas areas{};
        if (!(lights.Shadows.Radius > 0.0f))
        {
            return areas;
        }
        const float                             reach = std::min(lights.ShadowReach, camera.GetFarDistance());
        const std::optional<Wotah::CascadeEnds> ends  = Wotah::MakeCascadeEnds(camera.GetNearDistance(), reach);
        if (!ends)
        {
            return areas;
        }
        float from          = camera.GetNearDistance();
        float previousEnd   = 0.0f; // the shader measures each blend from here: 0 before the first cascade
        packed.CascadeBlend = {CascadeBlendShare, 0.0f, 0.0f, 0.0f};
        for (uint32_t cascade = 0; cascade < Wotah::ShadowCascadeCount; ++cascade)
        {
            const float end = (*ends)[cascade];
            areas[cascade]  = Wotah::MakeCascadeArea(camera, from, end);
            if (!areas[cascade])
            {
                return CascadeAreas{};
            }
            packed.CascadeEnds[static_cast<glm::length_t>(cascade)]   = end;
            packed.CascadeTexels[static_cast<glm::length_t>(cascade)] = Wotah::GetShadowTexelSize(*areas[cascade]);
            from                                                      = end - CascadeBlendShare * (end - previousEnd);
            previousEnd                                               = end;
        }
        return areas;
    }

    /** The lights packed for the GPU; empty (and logged) when they do not fit or a direction is missing. */
    std::optional<GpuSceneLights> Pack(const Wotah::SceneLights& lights, const Wotah::Camera& camera)
    {
        if (lights.DirectionalLights.size() > Wotah::MaxDirectionalLights)
        {
            Wotah::Logger::Error("A scene has at most {} directional lights, not {}", Wotah::MaxDirectionalLights,
                                 lights.DirectionalLights.size());
            return std::nullopt;
        }

        GpuSceneLights     packed;
        const CascadeAreas cascades = MakeCascades(lights, camera, packed);
        packed.Ambient              = glm::vec4(lights.AmbientColor, 0.0f);
        packed.GroundBounce         = glm::vec4(lights.GroundBounce, 0.0f);
        packed.HazeColor            = glm::vec4(lights.HazeColor, 0.0f);
        packed.Count[0]             = static_cast<uint32_t>(lights.DirectionalLights.size()); // checked above
        for (std::size_t index = 0; index < lights.DirectionalLights.size(); ++index)
        {
            const Wotah::DirectionalLight& light = lights.DirectionalLights[index];
            if (!(glm::dot(light.Direction, light.Direction) >= NearlyZeroSquared)) // also refuses NaN
            {
                Wotah::Logger::Error("Directional light {} has no direction", index);
                return std::nullopt;
            }
            GpuDirectionalLight& gpuLight = packed.Directional[index];
            const glm::vec3      radiance = light.Color * light.Intensity;
            gpuLight.TowardsLight         = glm::vec4(-glm::normalize(light.Direction), 0.0f);
            gpuLight.Radiance             = glm::vec4(radiance, 0.0f);

            // Every light that gives light casts its own shadows, as long as the scene says where: a view per cascade.
            bool castsShadow = !IsDark(radiance) && cascades.front().has_value();
            for (uint32_t cascade = 0; castsShadow && cascade < Wotah::ShadowCascadeCount; ++cascade)
            {
                const std::optional<glm::mat4> view =
                    Wotah::MakeShadowViewProjection(light.Direction, *cascades[cascade], lights.Shadows);
                castsShadow                    = view.has_value();
                gpuLight.CascadeViews[cascade] = view.value_or(glm::mat4(1.0f));
            }
            gpuLight.Shadow = {castsShadow ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
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

glm::vec3 Wotah::MakeSunlightThroughAir(float elevation)
{
    if (!(elevation >= 0.0f))
    {
        return glm::vec3(0.0f); // below the horizon (or not a number): no sunlight reaches the ground
    }
    const float height  = std::min(elevation, Overhead);
    const float degrees = glm::degrees(height);
    const float airMass =
        1.0f / (std::sin(height) + AirMassScale * std::pow(degrees + AirMassShiftDegrees, AirMassPower));

    // Each color keeps exp(-dimming * air) of its light (Beer-Lambert), here measured against the air straight up.
    // The fit gives a hair under 1 overhead, so the share is capped at all of it.
    return glm::min(glm::exp(-AirDimming * (airMass - 1.0f)), glm::vec3(1.0f));
}

Wotah::Lighting::~Lighting()
{
    Destroy();
}

bool Wotah::Lighting::Create(const Device& device)
{
    Destroy(); // a second Create replaces the first instead of leaking it
    m_device       = device.GetHandle();
    m_deviceObject = &device;

    // Shadow maps for one light to start with; Update makes room for more when more lights cast shadows.
    if (!m_buffer.Create(device, sizeof(GpuSceneLights), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, BufferMemory::CpuToGpu) ||
        !m_shadowMaps.Create(device, 1))
    {
        return false;
    }

    // The uniform buffer and the shadow maps (with their comparing sampler), both read by the fragment shaders.
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    bindings[0].binding         = LightBufferBinding;
    bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding         = ShadowMapsBinding;
    bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo =
        Wotah::MakeVulkanInfo<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings    = bindings.data();
    if (!Succeeded(vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_setLayout),
                   "create the lights' descriptor set layout"))
    {
        return false;
    }

    // A pool with room for exactly this one set.
    const std::array<VkDescriptorPoolSize, 2> poolSizes{{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
    }};
    VkDescriptorPoolCreateInfo                poolInfo =
        Wotah::MakeVulkanInfo<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
    poolInfo.maxSets       = 1;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes    = poolSizes.data();
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

    // The set points at the buffer once; afterwards only its bytes change. The maps are pointed at again when Update
    // makes room for more.
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer          = m_buffer.GetHandle();
    bufferInfo.range           = sizeof(GpuSceneLights);
    VkWriteDescriptorSet write = Wotah::MakeVulkanInfo<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
    write.dstSet               = m_set;
    write.dstBinding           = LightBufferBinding;
    write.descriptorCount      = 1;
    write.descriptorType       = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo          = &bufferInfo;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
    WriteShadowMapsDescriptor();

    return Update(SceneLights{}, Camera{}); // defined content before the first frame
}

void Wotah::Lighting::WriteShadowMapsDescriptor()
{
    VkDescriptorImageInfo mapsInfo{};
    mapsInfo.sampler           = m_shadowMaps.GetSampler();
    mapsInfo.imageView         = m_shadowMaps.GetArrayView();
    mapsInfo.imageLayout       = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // ShadowMaps keeps them so outside EndLayer
    VkWriteDescriptorSet write = Wotah::MakeVulkanInfo<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
    write.dstSet               = m_set;
    write.dstBinding           = ShadowMapsBinding;
    write.descriptorCount      = 1;
    write.descriptorType       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo           = &mapsInfo;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
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
    m_shadowMaps.Destroy();
    m_shadowViewProjections.fill(std::nullopt);
    m_device       = VK_NULL_HANDLE;
    m_deviceObject = nullptr;
    m_setLayout    = VK_NULL_HANDLE;
    m_pool         = VK_NULL_HANDLE;
    m_set          = VK_NULL_HANDLE;
}

bool Wotah::Lighting::Update(const SceneLights& lights, const Camera& camera)
{
    const std::optional<GpuSceneLights> packed = Pack(lights, camera);
    if (!packed)
    {
        return false;
    }

    // Kept for drawing the shadow maps: the same views the shaders get.
    uint32_t lightsWithShadows = 0; // up to the last light that casts shadows, as their maps sit by light index
    for (uint32_t index = 0; index < MaxDirectionalLights; ++index)
    {
        const GpuDirectionalLight& light       = packed->Directional[index];
        const bool                 castsShadow = index < packed->Count[0] && light.Shadow.x > 0.0f;
        if (castsShadow)
        {
            lightsWithShadows = index + 1;
        }
        for (uint32_t cascade = 0; cascade < ShadowCascadeCount; ++cascade)
        {
            m_shadowViewProjections[GetShadowLayer(index, cascade)] =
                castsShadow ? std::optional(light.CascadeViews[cascade]) : std::nullopt;
        }
    }

    // More lights cast shadows than there are maps for: make room. The GPU is done with the last frame (see above), so
    // the old maps can go and the set can point at the new ones.
    if (lightsWithShadows > m_shadowMaps.GetLightCapacity())
    {
        if (!m_shadowMaps.Create(*m_deviceObject, lightsWithShadows))
        {
            m_shadowViewProjections.fill(std::nullopt);
            return false;
        }
        WriteShadowMapsDescriptor();
    }
    return m_buffer.Write(std::as_bytes(std::span(&*packed, 1)));
}

std::optional<glm::mat4> Wotah::Lighting::FindShadowViewProjection(uint32_t index, uint32_t cascade) const
{
    if (index >= MaxDirectionalLights || cascade >= ShadowCascadeCount)
    {
        return std::nullopt;
    }
    return m_shadowViewProjections[GetShadowLayer(index, cascade)];
}

void Wotah::Lighting::Bind(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout) const
{
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &m_set, 0, nullptr);
}
