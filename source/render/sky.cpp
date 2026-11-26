/**
 * @file sky.cpp
 * The sky's pipeline, and what it hands its shaders: the inverse camera, the sun, the moon and the clock.
 */
#include "render/sky.hpp"

#include "render/lighting.hpp"

#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <glm/vec4.hpp>
#include <vulkan/vulkan.h>

#include <array>
#include <cmath>
#include <cstdint>

namespace
{
    /** The sky's shaders (shaders/fullscreen.vert, shaders/sky.frag), compiled to SPIR-V by the build. */
    constexpr uint32_t FullscreenVertexShader[] = {
#include "fullscreen.vert.inc"
    };
    constexpr uint32_t SkyFragmentShader[] = {
#include "sky.frag.inc"
    };

    /** The three vertices of the one triangle that covers the screen (shaders/fullscreen.vert). */
    constexpr uint32_t ScreenTriangleVertices = 3;

    /** What sky.frag gets; matches its PushConstants block. */
    struct SkyPushConstants
    {
        /** Clip space back to world space: undoes the camera, so each pixel knows its direction. */
        glm::mat4 InverseViewProjection{1.0f};
        /** xyz: towards the sun, length 1; w: the sky's clock in seconds, 0 up to SkyLoopSeconds. */
        glm::vec4 TowardsSun{0.0f, 1.0f, 0.0f, 0.0f};
        /** rgb: the sun's light as it reaches the scene, color times intensity. */
        glm::vec4 SunLight{1.0f};
        /** xyz: towards the moon, length 1. */
        glm::vec4 TowardsMoon{0.0f, -1.0f, 0.0f, 0.0f};
        /** rgb: the moon's disk, tint times brightness; without any, the shader draws no moon. */
        glm::vec4 MoonLight{0.0f};
    };

    // Every GPU offers at least 128 bytes of push constants (Vulkan spec, "Required Limits", maxPushConstantsSize).
    static_assert(sizeof(SkyPushConstants) <= 128, "the push constants must fit the 128 bytes every GPU offers");

    /** The one push constant range, read by the fragment shader. */
    constexpr std::array<VkPushConstantRange, 1> SkyPushConstantRanges{
        {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SkyPushConstants)}}};

    /** Below this squared length a direction counts as zero. */
    constexpr float NearlyZeroSquared = 1.0e-12f;

    /** Towards a light, length 1, or fallback for a light without a direction. */
    glm::vec4 MakeTowards(const Wotah::DirectionalLight& light, const glm::vec4& fallback)
    {
        if (glm::dot(light.Direction, light.Direction) < NearlyZeroSquared)
        {
            return fallback;
        }
        return glm::vec4(-glm::normalize(light.Direction), 0.0f);
    }

    /** The sky's clock: seconds wrapped into 0 up to SkyLoopSeconds; 0 for a value that is not a number. */
    float WrapSkySeconds(float seconds)
    {
        if (!std::isfinite(seconds))
        {
            return 0.0f;
        }
        const float wrapped = std::fmod(seconds, Wotah::SkyLoopSeconds);
        return wrapped < 0.0f ? wrapped + Wotah::SkyLoopSeconds : wrapped;
    }
} // namespace

bool Wotah::Sky::Create(const Device& device, VkFormat colorFormat, VkFormat depthFormat)
{
    // The frame has a depth image, so the pipeline names its format, but the sky neither tests nor writes depth: it is
    // infinitely far away, and everything drawn after it lies in front.
    PipelineSettings settings;
    settings.VertexShader   = FullscreenVertexShader;
    settings.FragmentShader = SkyFragmentShader;
    settings.ColorFormat    = colorFormat;
    settings.DepthFormat    = depthFormat;
    settings.PushConstants  = SkyPushConstantRanges;
    return m_pipeline.Create(device, settings);
}

void Wotah::Sky::Destroy()
{
    m_pipeline.Destroy();
}

void Wotah::Sky::Draw(VkCommandBuffer commandBuffer, const glm::mat4& viewProjection, const DirectionalLight& sun,
                      const DirectionalLight& moon, float seconds) const
{
    // A light without a direction keeps its place: the sun up, the moon down.
    SkyPushConstants pushConstants;
    pushConstants.InverseViewProjection = glm::inverse(viewProjection);
    pushConstants.TowardsSun            = MakeTowards(sun, pushConstants.TowardsSun);
    pushConstants.TowardsSun.w          = WrapSkySeconds(seconds);
    pushConstants.TowardsMoon           = MakeTowards(moon, pushConstants.TowardsMoon);

    // The shader makes the disk far brighter than this light and lets it build up towards white, so a day sun shows
    // white-hot and a low sun's dim, red light an orange-red disk; a switched-off sun shows none. The moon's disk is
    // drawn from its own tint and brightness.
    pushConstants.SunLight  = glm::vec4(sun.Color * sun.Intensity, 1.0f);
    pushConstants.MoonLight = glm::vec4(moon.Color * moon.Intensity, 1.0f);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.GetHandle());
    vkCmdPushConstants(commandBuffer, m_pipeline.GetLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConstants),
                       &pushConstants);
    vkCmdDraw(commandBuffer, ScreenTriangleVertices, 1, 0, 0);
}
