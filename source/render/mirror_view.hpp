/**
 * @file mirror_view.hpp
 * The picture a flat mirror (the sea's surface) shows: the scene above the surface drawn once more, seen from the
 * camera's mirror image below it (a planar reflection), into an image of its own that the water reads where it mirrors.
 * Only what lies above the surface is drawn: the mirrored view's near plane lies in the surface (oblique near-plane
 * clipping, Lengyel 2005, "Oblique View Frustum Depth Projection and Clipping"), so the sea floor never shows in the
 * mirror. The mirrored view turns the picture's handedness, so triangles that face the camera wind clockwise in it:
 * pipelines that draw into the mirror cull with VK_FRONT_FACE_CLOCKWISE.
 *
 *     const std::optional<glm::mat4> mirrored = Wotah::MakeMirrorViewProjection(camera, sea.Level);
 *     mirror.Begin(commands);                        // Renderer::BeginMirror does this for the frame's mirror
 *     if (mirrored) { sky.Draw(commands, *mirrored, ...); land.DrawMirrored(commands, *mirrored, lighting); }
 *     mirror.End(commands);                          // Renderer::BeginScene does this
 *     mirror.Bind(commands, waterPipeline.GetLayout(), 2);
 *
 * A pixel of the mirror's picture shows what the water at the same place in the camera's picture mirrors (the
 * picture's shares across and down are the same, at MirrorScale of the size).
 */
#pragma once

#include "render/descriptor_set.hpp"
#include "render/image.hpp"

#include <glm/mat4x4.hpp>
#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <optional>

namespace Wotah
{
    class Camera;
    class Device;

    /**
     * The mirror's picture's width and height as a share of the frame's: half, a quarter of the pixels. The water's
     * fine ripples blur what it mirrors more than that, by eye.
     */
    inline constexpr float MirrorScale = 0.5f;

    /** The bindings of the mirror's set: its colors, and its depth (1 where only the sky shows). */
    inline constexpr uint32_t MirrorColorBinding = 0;
    inline constexpr uint32_t MirrorDepthBinding = 1;

    /**
     * The camera's view mirrored in the horizontal plane at surfaceHeight, with the near plane in that plane, so only
     * what lies above it is drawn. Empty while the camera is not above the plane by a centimetre or more (from below
     * or on the surface there is nothing to mirror), or looks away from it entirely.
     */
    [[nodiscard]] std::optional<glm::mat4> MakeMirrorViewProjection(const Camera& camera, float surfaceHeight);

    /**
     * The mirror's color and depth images, their samplers and the descriptor set that shows them to fragment shaders.
     * The destructor runs Destroy, so nothing leaks when Create fails part way.
     */
    class MirrorView final
    {
    public:
        MirrorView() = default;
        ~MirrorView();

        MirrorView(const MirrorView&)            = delete;
        MirrorView& operator=(const MirrorView&) = delete;

        /**
         * Creates the images at MirrorScale of frameExtent (at least 1 x 1) in the frame's color and depth formats, so
         * the scene's pipelines' formats fit, with the samplers and the set; replaces what an earlier Create made.
         * False (and logged) when Vulkan refuses. The device must outlive it; not while the GPU reads the set.
         */
        [[nodiscard]] bool Create(const Device& device, VkExtent2D frameExtent, VkFormat colorFormat,
                                  VkFormat depthFormat);

        /** Frees everything Create made; safe to call more than once or after a failed Create. */
        void Destroy();

        /**
         * Begins a rendering into the mirror's images, both cleared (black, far depth), with viewport and scissor
         * covering them. Outside of other renderings; End follows before anything reads the mirror.
         */
        void Begin(VkCommandBuffer commandBuffer) const;

        /** Ends the rendering Begin began and makes both images readable by fragment shaders. */
        void End(VkCommandBuffer commandBuffer) const;

        /** Binds the set at set index setIndex of a graphics pipeline's layout. */
        void Bind(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, uint32_t setIndex) const;

        /** The set's layout, for pipelines that read the mirror; VK_NULL_HANDLE before Create. */
        [[nodiscard]] VkDescriptorSetLayout GetSetLayout() const noexcept { return m_set.GetLayout(); }

        /** The images' size. */
        [[nodiscard]] VkExtent2D GetExtent() const noexcept { return m_color.GetExtent(); }

    private:
        VkDevice      m_device = VK_NULL_HANDLE;
        Image         m_color;
        Image         m_depth;
        VkSampler     m_colorSampler = VK_NULL_HANDLE;
        VkSampler     m_depthSampler = VK_NULL_HANDLE;
        DescriptorSet m_set;
    };
} // namespace Wotah
