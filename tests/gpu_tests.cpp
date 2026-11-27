/**
 * @file gpu_tests.cpp
 * Tests that need a Vulkan 1.3 GPU: the window, device, swapchain, pipelines with every option, frames through the
 * renderer, depth images, shadows, the example scene (cube and ball) drawn offscreen and read back, and the UI drawn
 * over it. Every window
 * stays hidden, so running the tests never shows anything on screen. Skip them with --test-suite-exclude=gpu on
 * machines without a GPU.
 */
#include "platform/window.hpp"
#include "render/buffer.hpp"
#include "render/camera.hpp"
#include "render/device.hpp"
#include "render/height_field.hpp"
#include "render/image.hpp"
#include "render/lighting.hpp"
#include "render/mesh.hpp"
#include "render/mesh_data.hpp"
#include "render/mesh_pipelines.hpp"
#include "render/mirror_view.hpp"
#include "render/pipeline.hpp"
#include "render/renderer.hpp"
#include "render/scene_behind.hpp"
#include "render/scene_renderer.hpp"
#include "render/sea_state.hpp"
#include "render/shadow_maps.hpp"
#include "render/sky.hpp"
#include "render/sky_lights.hpp"
#include "render/swapchain.hpp"
#include "render/terrain_renderer.hpp"
#include "render/ui_renderer.hpp"
#include "render/vulkan_utils.hpp"
#include "render/water_renderer.hpp"
#include "scene/beach.hpp"
#include "terrain/terrain_shape.hpp"
#include "test_utils.hpp"
#include "ui/control_bar.hpp"
#include "ui/hints_window.hpp"
#include "ui/stats_window.hpp"
#include "ui/user_interface.hpp"
#include "water/sea.hpp"

#include <doctest/doctest.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <imgui.h>
#include <imgui_internal.h> // ImGui::FindWindowByName, to see where a card ended up
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <thread>
#include <vector>

using Wotah::Testing::HiddenGpu;
using Wotah::Testing::HiddenWindowSize;

namespace
{
    /**
     * The program's shaders (shaders/cube.frag, ball.frag, ...), as the program builds them; mesh.vert and shadow.vert
     * come with render/mesh_pipelines.
     */
    constexpr uint32_t CubeFragmentShader[] = {
#include "cube.frag.inc"
    };
    constexpr uint32_t BallFragmentShader[] = {
#include "ball.frag.inc"
    };

    /**
     * The test shaders (tests/shaders/): a plain ground for the light, shadow and haze tests; the land's height probe;
     * and water_test.*, for vertex input, push constants and a uniform buffer.
     */
    constexpr uint32_t GroundTestFragmentShader[] = {
#include "ground_test.frag.inc"
    };
    constexpr uint32_t TerrainProbeFragmentShader[] = {
#include "terrain_probe.frag.inc"
    };
    constexpr uint32_t SeaProbeFragmentShader[] = {
#include "sea_probe.frag.inc"
    };

    /** What sea_probe.frag gets: which number of the waves to write (x). */
    struct SeaProbePushConstants
    {
        std::array<uint32_t, 4> Part{};
    };

    constexpr std::array<VkPushConstantRange, 1> SeaProbeRanges{
        {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SeaProbePushConstants)}}};
    constexpr uint32_t WaterTestVertexShader[] = {
#include "water_test.vert.inc"
    };
    constexpr uint32_t WaterTestFragmentShader[] = {
#include "water_test.frag.inc"
    };

    /** The tests' plain ground (ground_test.frag): 64 x 64 units, flat at height 0, a height point every half unit. */
    constexpr float    GroundSize  = 64.0f;
    constexpr uint32_t GroundCells = 128;

    /** The tests' sea: 3 units above that ground, so a water test sees the ground 3 units down. */
    constexpr Wotah::Sea TestSea = Wotah::Testing::MakeStillSea(3.0f);

    /** Where the land's shadow casters can stand, as in the program (app.cpp, ShadowCasterRadius). */
    constexpr float LandShadowReach = 150.0f;

    /** Where terrain_probe.frag's grid of places starts and how far apart they are (it uses the same). */
    constexpr glm::vec2 ProbeCorner{-150.0f, -150.0f};
    constexpr float     ProbeSpacing = 0.5f;

    /**
     * A scene of shapes to test drawing with, as the program had it before its water: a cube and a ball standing on the
     * ground, and how finely the ball is cut.
     */
    constexpr glm::vec3 CubePosition{0.0f, 0.5f, 0.0f};
    constexpr float     BallRadius = 0.5f;
    constexpr glm::vec3 BallPosition{-1.3f, BallRadius, -0.9f};
    constexpr uint32_t  BallSegments = 48;
    constexpr uint32_t  BallRings    = 24;

    /** Where the program's shadow casters can stand (app.cpp): the whole ground and room above it. */
    constexpr Wotah::ShadowArea GroundShadows{glm::vec3(0.0f), 1.41421356f * GroundSize * 0.5f + 2.0f};

    /**
     * What an offscreen picture shows: its lights, which parts are in it (the plain ground, or the program's land of
     * a shape), where the ball lies, the draw order, and the UI over it all when Ui is set (not owned; its last ended
     * UI frame is drawn). IsLandProbe draws terrain_probe.frag over the whole picture instead of the scene.
     * IsSkyInMirrorOnly draws the sky in the sea's mirror only, not behind the scene: what the water lets through from
     * behind it shows then.
     */
    struct OffscreenScene
    {
        Wotah::SceneLights  Lights;
        bool                HasSky            = false;
        bool                IsSkyInMirrorOnly = false;
        bool                HasGround         = false;
        bool                HasLand           = false;
        bool                IsLandProbe       = false;
        int                 SeaProbePart      = -1;
        Wotah::TerrainShape Land              = Wotah::MakeFlatTerrainShape(0.0f);
        bool                HasShapes         = true;
        bool                HasWater          = false;
        Wotah::Sea          Sea               = TestSea;
        bool                IsBallFirst       = false;
        bool                IsGroundLast      = false;
        glm::vec3           BallPlace         = BallPosition;
        float               SkySeconds        = 0.0f;
        Wotah::UiRenderer*  Ui                = nullptr;

        /** The moon's disk for the sky (Sky::Draw, SkyLights::MoonDisk): none unless a test sets it. */
        Wotah::DirectionalLight Moon{{0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, 0.0f};
    };

    /** The offscreen pictures' size and color format (DrawSceneOffscreen): square, with room for the UI's cards. */
    constexpr VkExtent2D OffscreenSize{600, 600};
    constexpr VkFormat   OffscreenColorFormat = VK_FORMAT_R8G8B8A8_UNORM;

    /**
     * The scene on the GPU: the lights, the sky, the plain ground, the program's land and sea, both shapes, and the
     * pipelines that draw them.
     */
    struct Scene
    {
        Wotah::Lighting        Lighting;
        Wotah::SeaState        SeaState;
        Wotah::Sky             Sky;
        Wotah::MeshPipelines   GroundLook;
        Wotah::MeshPipelines   CubeLook;
        Wotah::MeshPipelines   BallLook;
        Wotah::Pipeline        ShadowPipeline;
        Wotah::Pipeline        LandProbePipeline;
        Wotah::Pipeline        SeaProbePipeline;
        Wotah::Mesh            Ground;
        Wotah::Mesh            Cube;
        Wotah::Mesh            Ball;
        Wotah::TerrainRenderer Land;
        Wotah::WaterRenderer   Sea;

        /**
         * Builds everything for frames of these formats: the lights as given, the land of landShape, the sea reading
         * the solid scene behind and the mirror.
         */
        [[nodiscard]] bool Create(const Wotah::Device& device, VkFormat colorFormat, VkFormat depthFormat,
                                  const Wotah::SceneLights& lights, const Wotah::SceneBehind& behind,
                                  const Wotah::MirrorView&   mirror,
                                  const Wotah::TerrainShape& landShape = Wotah::MakeFlatTerrainShape(0.0f))
        {
            if (!Lighting.Create(device) || !Lighting.Update(lights, Wotah::Camera{}) || !SeaState.Create(device))
            {
                return false;
            }
            if (!Sky.Create(device, colorFormat, depthFormat) ||
                !ShadowPipeline.Create(device, Wotah::MakeMeshShadowSettings(Lighting.GetShadowMaps().GetFormat())) ||
                !GroundLook.Create(device, GroundTestFragmentShader, colorFormat, depthFormat, Lighting) ||
                !Ground.Create(device, Wotah::MakeTerrainMesh(Wotah::MakeFlatHeightField(GroundSize, GroundSize,
                                                                                         GroundCells, GroundCells))) ||
                !CubeLook.Create(device, CubeFragmentShader, colorFormat, depthFormat, Lighting) ||
                !BallLook.Create(device, BallFragmentShader, colorFormat, depthFormat, Lighting) ||
                !Cube.Create(device, Wotah::MakeCube()) ||
                !Ball.Create(device, Wotah::MakeSphere(BallRadius, BallSegments, BallRings)) ||
                !Land.Create(device, colorFormat, depthFormat, Lighting, SeaState) || !Land.SetShape(landShape))
            {
                return false;
            }
            SeaState.SetFloor(Land.GetShapeBuffer());
            if (!Sea.Create(device, colorFormat, depthFormat, Lighting, behind, mirror, SeaState))
            {
                return false;
            }

            // The probe covers the picture, reads the land's shape and writes a color per pixel; no depth test.
            const std::array<VkDescriptorSetLayout, 2> probeLayouts{Lighting.GetSetLayout(), Land.GetShapeSetLayout()};
            Wotah::PipelineSettings                    probe =
                Wotah::MakeFullscreenSettings(TerrainProbeFragmentShader, colorFormat, depthFormat);
            probe.DescriptorSetLayouts = probeLayouts;

            // The sea's probe the same way, reading the sea's set (its waves and floor) at set 3, after the water's
            // other sets.
            const std::array<VkDescriptorSetLayout, 4> seaProbeLayouts{Lighting.GetSetLayout(), behind.GetSetLayout(),
                                                                       mirror.GetSetLayout(), SeaState.GetSetLayout()};
            Wotah::PipelineSettings                    seaProbe =
                Wotah::MakeFullscreenSettings(SeaProbeFragmentShader, colorFormat, depthFormat);
            seaProbe.DescriptorSetLayouts = seaProbeLayouts;
            seaProbe.PushConstants        = SeaProbeRanges;
            return LandProbePipeline.Create(device, probe) && SeaProbePipeline.Create(device, seaProbe);
        }

        /**
         * Moves land and sea around the viewer and puts the sea's waves on the GPU, as the scene renderer does every
         * frame; the sea's floor must be the land's shape, if it has one.
         */
        void FollowViewer(const glm::vec3& viewer, const Wotah::Sea& sea)
        {
            Land.FollowViewer(viewer);
            Sea.FollowViewer(viewer);
            REQUIRE(SeaState.Update(sea));
        }

        /**
         * Records the solid scene: the sky (its sun from the first light, its moon from what.Moon), then land, ground
         * and shapes as the scene renderer does, in any order the options pick (the depth test must make the order not
         * matter). The land probe instead, if asked for.
         */
        void Record(VkCommandBuffer commands, const glm::mat4& viewProjection, const OffscreenScene& what) const
        {
            if (what.IsLandProbe)
            {
                vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, LandProbePipeline.GetHandle());
                Land.BindShape(commands, LandProbePipeline.GetLayout());
                vkCmdDraw(commands, 3, 1, 0, 0); // fullscreen.vert's one triangle
                return;
            }
            if (what.SeaProbePart >= 0)
            {
                vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, SeaProbePipeline.GetHandle());
                SeaState.Bind(commands, SeaProbePipeline.GetLayout());
                SeaProbePushConstants part;
                part.Part[0] = static_cast<uint32_t>(what.SeaProbePart);
                vkCmdPushConstants(commands, SeaProbePipeline.GetLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                   sizeof(part), &part);
                vkCmdDraw(commands, 3, 1, 0, 0);
                return;
            }
            if (what.HasSky && !what.Lights.DirectionalLights.empty())
            {
                Sky.Draw(commands, viewProjection, what.Lights.DirectionalLights.front(), what.Moon, what.SkySeconds);
            }
            if (what.HasLand)
            {
                Land.Draw(commands, viewProjection, Lighting, SeaState);
            }
            if (what.HasGround && !what.IsGroundLast)
            {
                RecordGround(commands, viewProjection);
            }
            if (what.HasShapes)
            {
                RecordShapes(commands, viewProjection, what);
            }
            if (what.HasGround && what.IsGroundLast)
            {
                RecordGround(commands, viewProjection);
            }
        }

        /** Records what lies over the solid scene, as the scene renderer does after BeginSeeThrough: the sea. */
        void RecordSeeThrough(VkCommandBuffer commands, const glm::mat4& viewProjection, const OffscreenScene& what,
                              const Wotah::SceneBehind& behind, const Wotah::MirrorView& mirror) const
        {
            if (what.HasWater)
            {
                Sea.Draw(commands, viewProjection, Lighting, behind, mirror, SeaState, what.Sea);
            }
        }

        /**
         * Records what the sea mirrors into the mirror's picture, as the scene renderer does after BeginMirror: the sky
         * without the sun's disk, the land and the shapes, through the camera's mirrored view; nothing without water
         * or while the camera is not above it.
         */
        void RecordMirror(VkCommandBuffer commands, const Wotah::Camera& camera, const OffscreenScene& what) const
        {
            const std::optional<glm::mat4> mirrored = Wotah::MakeMirrorViewProjection(camera, what.Sea.Level);
            if (!what.HasWater || !mirrored)
            {
                return;
            }
            if ((what.HasSky || what.IsSkyInMirrorOnly) && !what.Lights.DirectionalLights.empty())
            {
                Wotah::DirectionalLight sunless = what.Lights.DirectionalLights.front();
                sunless.Intensity               = 0.0f;
                Sky.Draw(commands, *mirrored, sunless, what.Moon, what.SkySeconds);
            }
            if (what.HasLand)
            {
                Land.DrawMirrored(commands, *mirrored, Lighting, SeaState);
            }
            if (what.HasShapes)
            {
                Wotah::RecordMeshes(commands, *mirrored, Lighting, GetShapes(what), true);
            }
        }

        /**
         * Records every shadow map of every light that casts shadows, as the scene renderer does: ground, land and
         * shapes, as far as the options include them. Before the scene, outside its drawing.
         */
        void RecordShadows(VkCommandBuffer commands, const OffscreenScene& what) const
        {
            const Wotah::ShadowMaps& maps = Lighting.GetShadowMaps();
            for (uint32_t layer = 0; layer < Wotah::MaxDirectionalLights * Wotah::ShadowCascadeCount; ++layer)
            {
                const std::optional<glm::mat4> lightView = Lighting.FindShadowViewProjection(
                    layer / Wotah::ShadowCascadeCount, layer % Wotah::ShadowCascadeCount);
                if (!lightView || !maps.BeginLayer(commands, layer))
                {
                    continue;
                }
                if (what.HasGround)
                {
                    Wotah::RecordMeshShadows(commands, ShadowPipeline, *lightView, GetGround());
                }
                if (what.HasShapes)
                {
                    Wotah::RecordMeshShadows(commands, ShadowPipeline, *lightView, GetShapes(what));
                }
                if (what.HasLand)
                {
                    Land.DrawShadow(commands, *lightView, LandShadowReach);
                }
                maps.EndLayer(commands, layer);
            }
        }

        /** The plain ground as a mesh object: where its heights say, no transform. */
        [[nodiscard]] std::array<Wotah::MeshObject, 1> GetGround() const
        {
            return {{{&GroundLook, &Ground, glm::mat4(1.0f)}}};
        }

        /** Cube and ball as mesh objects, in the order the options pick. */
        [[nodiscard]] std::array<Wotah::MeshObject, 2> GetShapes(const OffscreenScene& what) const
        {
            const Wotah::MeshObject cube{&CubeLook, &Cube, glm::translate(glm::mat4(1.0f), CubePosition)};
            const Wotah::MeshObject ball{&BallLook, &Ball, glm::translate(glm::mat4(1.0f), what.BallPlace)};
            return what.IsBallFirst ? std::array{ball, cube} : std::array{cube, ball};
        }

        /** Records the plain ground. */
        void RecordGround(VkCommandBuffer commands, const glm::mat4& viewProjection) const
        {
            Wotah::RecordMeshes(commands, viewProjection, Lighting, GetGround(), false);
        }

        /** Records cube and ball, in the order the options pick. */
        void RecordShapes(VkCommandBuffer commands, const glm::mat4& viewProjection, const OffscreenScene& what) const
        {
            Wotah::RecordMeshes(commands, viewProjection, Lighting, GetShapes(what), false);
        }
    };

    /**
     * The light the scene had before lights became data: ambient 0.25 and one white light of 0.75 falling from up,
     * a bit right and front. Through the light buffer it must give exactly the colors it gave as fixed shader code.
     */
    Wotah::SceneLights MakeOldLight()
    {
        Wotah::DirectionalLight light;
        light.Direction = {-0.4f, -1.0f, -0.25f};
        light.Intensity = 0.75f;
        Wotah::SceneLights lights;
        lights.AmbientColor      = glm::vec3(0.25f);
        lights.DirectionalLights = {light};
        return lights;
    }

    /** The view the drawing tests look at the shapes from: the program's start view before its water. */
    Wotah::Camera MakeStartCamera()
    {
        Wotah::Camera camera;
        camera.SetPosition({2.2f, 1.5f, 3.0f});
        camera.SetTarget({-0.2f, 0.75f, 0.0f});
        (void)camera.SetClipDistances(0.1f, 10.0f); // valid values
        return camera;
    }

    /**
     * The view depth (distance along the camera's view) a depth image's texel holds, from its bytes in the depth format
     * (FindDepthFormat's: 32-bit float, 24 bits in 32, or 16 bits), as glm's perspective with depth 0 to 1 wrote it.
     */
    float ReadViewDepth(const std::byte* texel, VkFormat format, const Wotah::Camera& camera)
    {
        float depth = 1.0f;
        if (format == VK_FORMAT_D32_SFLOAT)
        {
            std::memcpy(&depth, texel, sizeof(depth));
        }
        else if (format == VK_FORMAT_X8_D24_UNORM_PACK32)
        {
            uint32_t bits = 0;
            std::memcpy(&bits, texel, sizeof(bits));
            depth = static_cast<float>(bits & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu);
        }
        else
        {
            uint16_t bits = 0;
            std::memcpy(&bits, texel, sizeof(bits));
            depth = static_cast<float>(bits) / 65535.0f;
        }
        const float near = camera.GetNearDistance();
        const float far  = camera.GetFarDistance();
        return near * far / (far - depth * (far - near));
    }

    /**
     * Draws the program's scene (cube and ball, with a depth image) through the camera into a hidden 600 x 600 RGBA8
     * image, exactly with the program's settings, and returns its pixels (4 bytes each, rows top to bottom). With
     * viewDepths, also every pixel's view depth once all is drawn (the water's own, as it writes its depth).
     */
    std::vector<uint8_t> DrawSceneOffscreen(const Wotah::Device& device, const Wotah::Camera& camera,
                                            const OffscreenScene& what, std::vector<float>* viewDepths = nullptr)
    {
        constexpr VkExtent2D Size        = OffscreenSize;
        constexpr VkFormat   ColorFormat = OffscreenColorFormat;

        // ---- The images to draw into, and a buffer the CPU can read the picture from ----
        Wotah::Image color;
        REQUIRE(color.Create(device, Size, ColorFormat,
                             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT));
        const std::optional<VkFormat> depthFormat = Wotah::FindDepthFormat(device);
        REQUIRE(depthFormat.has_value());
        Wotah::Image depth;
        REQUIRE(depth.Create(device, Size, *depthFormat,
                             VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT));
        Wotah::SceneBehind behind;
        REQUIRE(behind.Create(device, depth, ColorFormat));
        Wotah::MirrorView mirror;
        REQUIRE(mirror.Create(device, Size, ColorFormat, *depthFormat));

        constexpr VkDeviceSize ByteCount = VkDeviceSize{Size.width} * Size.height * 4;
        Wotah::Buffer          readback;
        REQUIRE(readback.Create(device, ByteCount, VK_BUFFER_USAGE_TRANSFER_DST_BIT, Wotah::BufferMemory::GpuToCpu));

        Scene scene;
        REQUIRE(scene.Create(device, ColorFormat, *depthFormat, what.Lights, behind, mirror, what.Land));
        REQUIRE(scene.Lighting.Update(what.Lights, camera)); // the shadow cascades follow the camera
        scene.FollowViewer(camera.GetPosition(), what.Sea);

        // ---- Record: clear, draw, copy to the buffer ----
        Wotah::OneTimeCommands recording;
        REQUIRE(recording.Begin(device));
        const VkCommandBuffer commands = recording.Get();
        scene.RecordShadows(commands, what); // the lights' shadow maps first, as the program does
        mirror.Begin(commands);              // then what the sea mirrors
        scene.RecordMirror(commands, camera, what);
        mirror.End(commands);

        Wotah::LayoutChange colorToTarget;
        colorToTarget.To           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorToTarget.TargetStage  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        colorToTarget.TargetAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        Wotah::RecordLayoutChange(commands, color.GetHandle(), colorToTarget);
        Wotah::LayoutChange depthToTarget;
        depthToTarget.To = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthToTarget.TargetStage =
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        depthToTarget.TargetAccess =
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        depthToTarget.Aspect = depth.GetAspect();
        Wotah::RecordLayoutChange(commands, depth.GetHandle(), depthToTarget);

        // The solid scene as the renderer draws it: cleared to the program's background and the far plane, the depth
        // kept for the water.
        Wotah::DrawTarget colorTarget;
        colorTarget.View        = color.GetView();
        colorTarget.Layout      = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorTarget.Clear.color = {{0.1f, 0.1f, 0.1f, 1.0f}};
        Wotah::DrawTarget depthTarget;
        depthTarget.View                     = depth.GetView();
        depthTarget.Layout                   = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthTarget.Clear.depthStencil.depth = 1.0f;
        Wotah::RecordBeginDrawing(commands, Size, colorTarget, depthTarget);
        scene.Record(commands, camera.GetViewProjection(), what);

        // The see-through part, as Renderer::BeginSeeThrough makes it: the same images, the depth and the colors drawn
        // so far copied for the water to read, the depth tested and written on.
        vkCmdEndRendering(commands);
        behind.Record(commands, color.GetHandle(), depth);
        colorTarget.IsCleared = false;
        depthTarget.IsCleared = false;
        depthTarget.IsKept    = viewDepths != nullptr;
        Wotah::RecordBeginDrawing(commands, Size, colorTarget, depthTarget);
        scene.RecordSeeThrough(commands, camera.GetViewProjection(), what, behind, mirror);
        if (what.Ui != nullptr)
        {
            REQUIRE(what.Ui->Draw(commands)); // last, over everything, as app.cpp does
        }
        vkCmdEndRendering(commands);

        Wotah::LayoutChange colorToCopy;
        colorToCopy.From         = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorToCopy.To           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        colorToCopy.SourceStage  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        colorToCopy.SourceAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        colorToCopy.TargetStage  = VK_PIPELINE_STAGE_TRANSFER_BIT;
        colorToCopy.TargetAccess = VK_ACCESS_TRANSFER_READ_BIT;
        Wotah::RecordLayoutChange(commands, color.GetHandle(), colorToCopy);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent      = {Size.width, Size.height, 1};
        vkCmdCopyImageToBuffer(commands, color.GetHandle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.GetHandle(),
                               1, &region);

        // The depth too, if asked for: after everything drawn into it.
        const VkDeviceSize depthTexelBytes = *depthFormat == VK_FORMAT_D16_UNORM ? 2 : 4;
        const VkDeviceSize depthBytes      = VkDeviceSize{Size.width} * Size.height * depthTexelBytes;
        Wotah::Buffer      depthReadback;
        if (viewDepths != nullptr)
        {
            REQUIRE(depthReadback.Create(device, depthBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                         Wotah::BufferMemory::GpuToCpu));
            Wotah::LayoutChange depthToCopy;
            depthToCopy.From         = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthToCopy.To           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            depthToCopy.SourceStage  = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            depthToCopy.SourceAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            depthToCopy.TargetStage  = VK_PIPELINE_STAGE_TRANSFER_BIT;
            depthToCopy.TargetAccess = VK_ACCESS_TRANSFER_READ_BIT;
            depthToCopy.Aspect       = depth.GetAspect();
            Wotah::RecordLayoutChange(commands, depth.GetHandle(), depthToCopy);
            VkBufferImageCopy depthRegion{};
            depthRegion.imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
            depthRegion.imageExtent      = {Size.width, Size.height, 1};
            vkCmdCopyImageToBuffer(commands, depth.GetHandle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   depthReadback.GetHandle(), 1, &depthRegion);
        }

        VkMemoryBarrier toHost = Wotah::MakeVulkanInfo<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
        toHost.srcAccessMask   = VK_ACCESS_TRANSFER_WRITE_BIT;
        toHost.dstAccessMask   = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &toHost, 0,
                             nullptr, 0, nullptr);
        REQUIRE(recording.SubmitAndWait());

        std::vector<uint8_t> pixels(static_cast<std::size_t>(ByteCount));
        REQUIRE(readback.Read(std::as_writable_bytes(std::span(pixels))));
        if (viewDepths != nullptr)
        {
            std::vector<std::byte> texels(static_cast<std::size_t>(depthBytes));
            REQUIRE(depthReadback.Read(texels));
            viewDepths->resize(static_cast<std::size_t>(Size.width) * Size.height);
            for (std::size_t index = 0; index < viewDepths->size(); ++index)
            {
                (*viewDepths)[index] = ReadViewDepth(texels.data() + index * depthTexelBytes, *depthFormat, camera);
            }
        }
        return pixels;
    }

    /** How many pixels are within 2 of an RGB color per channel (GPUs may round the last bit differently). */
    std::size_t CountPixels(const std::vector<uint8_t>& pixels, uint8_t red, uint8_t green, uint8_t blue)
    {
        constexpr int Slack = 2;
        std::size_t   count = 0;
        for (std::size_t index = 0; index + 3 < pixels.size(); index += 4)
        {
            if (std::abs(pixels[index] - red) <= Slack && std::abs(pixels[index + 1] - green) <= Slack &&
                std::abs(pixels[index + 2] - blue) <= Slack)
            {
                ++count;
            }
        }
        return count;
    }

    /** The RGB of the pixel in the middle of a 600 x 600 picture. */
    std::array<int, 3> GetCenterColor(const std::vector<uint8_t>& pixels)
    {
        const std::size_t center = (300 * 600 + 300) * 4;
        return {pixels[center], pixels[center + 1], pixels[center + 2]};
    }

    /** A 0..1 color channel as the 0..255 value an RGBA8 image stores. */
    int ToPixel(float channel)
    {
        return static_cast<int>(channel * 255.0f + 0.5f);
    }

    /** Whether a color is within 3 of an expected 0..1 RGB per channel (rounding may differ by a step or two). */
    bool IsColorNear(const std::array<int, 3>& color, float red, float green, float blue)
    {
        constexpr int Slack = 3;
        return std::abs(color[0] - ToPixel(red)) <= Slack && std::abs(color[1] - ToPixel(green)) <= Slack &&
               std::abs(color[2] - ToPixel(blue)) <= Slack;
    }

    /** A camera at the origin looking towards a direction, for checking what the sky shows there. */
    Wotah::Camera MakeCameraLookingAt(const glm::vec3& direction)
    {
        Wotah::Camera camera;
        camera.SetPosition({0.0f, 0.0f, 0.0f});
        camera.SetTarget(direction);
        return camera;
    }

    /**
     * The camera the frame loops look through, shaped like the frame: above the tests' sea, looking down at it, so the
     * mirror's picture is drawn and the water is seen from above.
     */
    Wotah::Camera MakeFrameCamera(VkExtent2D extent)
    {
        Wotah::Camera camera;
        camera.SetPosition({0.0f, TestSea.Level + 3.0f, 5.0f});
        camera.SetTarget({0.0f, TestSea.Level, 0.0f});
        (void)camera.SetAspectRatio(static_cast<float>(extent.width) / static_cast<float>(extent.height)); // never 0
        return camera;
    }

    /**
     * Draws frames as app.cpp's loop does: polls the window, makes the swapchain again when the window was resized or
     * the swapchain is out of date, then draws the scene. True when every frame was drawn.
     */
    bool DrawFramesLikeApp(Wotah::Testing::HiddenGpu& gpu, Wotah::Swapchain& swapchain, Wotah::Renderer& renderer,
                           const Scene& scene, const OffscreenScene& what, int count)
    {
        for (int frame = 0; frame < count; ++frame)
        {
            gpu.Window.PollEvents();
            if ((gpu.Window.TakeResize() || swapchain.IsOutOfDate()) &&
                !renderer.RecreateSwapchain(gpu.Device, gpu.Window))
            {
                return false;
            }
            if (renderer.BeginFrame() != Wotah::FrameStart::Ready)
            {
                return false;
            }
            const Wotah::Camera camera = MakeFrameCamera(swapchain.GetExtent());
            scene.RecordShadows(renderer.GetCommandBuffer(), what);
            renderer.BeginMirror();
            scene.RecordMirror(renderer.GetCommandBuffer(), camera, what);
            renderer.BeginScene();
            scene.Record(renderer.GetCommandBuffer(), camera.GetViewProjection(), what);
            renderer.BeginSeeThrough();
            scene.RecordSeeThrough(renderer.GetCommandBuffer(), camera.GetViewProjection(), what,
                                   renderer.GetSceneBehind(), renderer.GetMirror());
            if (!renderer.EndFrame())
            {
                return false;
            }
        }
        gpu.Device.WaitUntilIdle();
        return true;
    }

    /**
     * A drawing thread as app.cpp's: waits while the window has no area, makes the swapchain again when the window was
     * resized or the swapchain is out of date, draws the scene and counts the frames, until stop is requested. failed
     * tells whether Vulkan failed on the way.
     */
    void DrawUntilStopped(const std::stop_token& stop, Wotah::Testing::HiddenGpu& gpu, Wotah::Swapchain& swapchain,
                          Wotah::Renderer& renderer, const Scene& scene, std::atomic<int>& frames,
                          std::atomic<bool>& failed)
    {
        // The sea too, so its reading of the depth image is drawn through every resize, as in the program.
        OffscreenScene what;
        what.Lights    = MakeOldLight();
        what.HasSky    = true;
        what.HasGround = true;
        what.HasWater  = true;
        while (!stop.stop_requested() && gpu.Window.WaitForDrawableArea(stop))
        {
            if ((gpu.Window.TakeResize() || swapchain.IsOutOfDate()) &&
                !renderer.RecreateSwapchain(gpu.Device, gpu.Window))
            {
                failed = true;
                break;
            }
            const Wotah::FrameStart start = renderer.BeginFrame();
            if (start == Wotah::FrameStart::Skip)
            {
                continue;
            }
            const Wotah::Camera camera = MakeFrameCamera(swapchain.GetExtent());
            if (start == Wotah::FrameStart::Ready)
            {
                scene.RecordShadows(renderer.GetCommandBuffer(), what);
                renderer.BeginMirror();
                scene.RecordMirror(renderer.GetCommandBuffer(), camera, what);
                renderer.BeginScene();
                scene.Record(renderer.GetCommandBuffer(), camera.GetViewProjection(), what);
                renderer.BeginSeeThrough();
                scene.RecordSeeThrough(renderer.GetCommandBuffer(), camera.GetViewProjection(), what,
                                       renderer.GetSceneBehind(), renderer.GetMirror());
            }
            if (start == Wotah::FrameStart::Failed || !renderer.EndFrame())
            {
                failed = true;
                break;
            }
            ++frames;
        }
        gpu.Device.WaitUntilIdle();
    }

    /**
     * Handles the window's events, as the main thread does, until the drawing thread has drawn at least target frames;
     * false when that took longer than a few seconds.
     */
    bool HandleEventsUntilDrawn(Wotah::Window& window, const std::atomic<int>& frames, int target)
    {
        constexpr std::chrono::seconds TimeLimit{10}; // far more than a few frames take, even with vsync
        const auto                     deadline = std::chrono::steady_clock::now() + TimeLimit;
        while (frames < target)
        {
            if (std::chrono::steady_clock::now() > deadline)
            {
                return false;
            }
            window.PollEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }

    /** Wakes the window's WaitEvents again and again until stop is requested. */
    void WakeUntilStopped(const std::stop_token& stop, Wotah::Window& window)
    {
        while (!stop.stop_requested())
        {
            window.Wake();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    /**
     * The RGB the picture (600 x 600, from DrawSceneOffscreen) shows where camera sees the world point; the point must
     * be in view.
     */
    std::array<int, 3> GetColorAt(const std::vector<uint8_t>& pixels, const Wotah::Camera& camera,
                                  const glm::vec3& point)
    {
        constexpr int   PictureSize = 600;
        const glm::vec4 clip        = camera.GetViewProjection() * glm::vec4(point, 1.0f);
        const int       x = static_cast<int>((clip.x / clip.w * 0.5f + 0.5f) * static_cast<float>(PictureSize));
        const int       y = static_cast<int>((clip.y / clip.w * 0.5f + 0.5f) * static_cast<float>(PictureSize));
        REQUIRE(x >= 0);
        REQUIRE(x < PictureSize);
        REQUIRE(y >= 0);
        REQUIRE(y < PictureSize);
        const std::size_t index = (static_cast<std::size_t>(y) * PictureSize + static_cast<std::size_t>(x)) * 4;
        return {pixels[index], pixels[index + 1], pixels[index + 2]};
    }

    /** A camera above the ground south of the cube, looking down at it at a slant: ground on every side shows. */
    Wotah::Camera MakeShadowCamera()
    {
        Wotah::Camera camera;
        camera.SetPosition({0.0f, 6.0f, 4.0f});
        camera.SetTarget({0.0f, 0.0f, 0.0f});
        (void)camera.SetClipDistances(0.1f, 100.0f); // valid values
        return camera;
    }

    /** A white light falling at 45 degrees along the x axis: towards +x (from the west) or -x (from the east). */
    Wotah::DirectionalLight MakeSideLight(float towardsX, float intensity)
    {
        Wotah::DirectionalLight light;
        light.Direction = {towardsX, -1.0f, 0.0f};
        light.Intensity = intensity;
        return light;
    }

    /** How many pixels of two pictures differ by more than slack in any channel. */
    std::size_t CountDifferentPixels(const std::vector<uint8_t>& first, const std::vector<uint8_t>& second, int slack)
    {
        std::size_t count = 0;
        for (std::size_t index = 0; index + 3 < first.size() && index + 3 < second.size(); index += 4)
        {
            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                if (std::abs(first[index + channel] - second[index + channel]) > slack)
                {
                    ++count;
                    break;
                }
            }
        }
        return count;
    }

    /** The RGB of the pixel at (x, y), counted from the top-left corner, in a picture from DrawSceneOffscreen. */
    std::array<int, 3> GetPixel(const std::vector<uint8_t>& pixels, uint32_t x, uint32_t y)
    {
        REQUIRE(x < OffscreenSize.width);
        REQUIRE(y < OffscreenSize.height);
        const std::size_t index = (std::size_t{y} * OffscreenSize.width + x) * 4;
        return {pixels[index], pixels[index + 1], pixels[index + 2]};
    }

    /** A part of a picture from DrawSceneOffscreen, in pixels from the top left; Right and Bottom lie just outside. */
    struct PixelArea
    {
        uint32_t Left   = 0;
        uint32_t Top    = 0;
        uint32_t Right  = 0;
        uint32_t Bottom = 0;
    };

    /** How many pixels inside area are light in every channel, as the UI's text is and nothing else in its panels. */
    std::size_t CountLightPixels(const std::vector<uint8_t>& pixels, const PixelArea& area)
    {
        REQUIRE(area.Right <= OffscreenSize.width);
        REQUIRE(area.Bottom <= OffscreenSize.height);
        constexpr uint8_t LightLevel = 0xB0;
        std::size_t       count      = 0;
        for (uint32_t y = area.Top; y < area.Bottom; ++y)
        {
            for (uint32_t x = area.Left; x < area.Right; ++x)
            {
                const std::size_t index = (std::size_t{y} * OffscreenSize.width + x) * 4;
                if (pixels[index] > LightLevel && pixels[index + 1] > LightLevel && pixels[index + 2] > LightLevel)
                {
                    ++count;
                }
            }
        }
        return count;
    }

    /**
     * The box around every pixel brighter than 250 in all three channels: a sun's disk, but not the glare around it,
     * which stays below that over a sky. Right and Bottom lie just outside; all 0 when there is none.
     */
    PixelArea FindSunDisk(const std::vector<uint8_t>& pixels)
    {
        constexpr int DiskLevel = 250;
        PixelArea     disk{OffscreenSize.width, OffscreenSize.height, 0, 0};
        for (uint32_t y = 0; y < OffscreenSize.height; ++y)
        {
            for (uint32_t x = 0; x < OffscreenSize.width; ++x)
            {
                if (std::ranges::min(GetPixel(pixels, x, y)) > DiskLevel)
                {
                    disk.Left   = std::min(disk.Left, x);
                    disk.Top    = std::min(disk.Top, y);
                    disk.Right  = std::max(disk.Right, x + 1);
                    disk.Bottom = std::max(disk.Bottom, y + 1);
                }
            }
        }
        return disk.Right == 0 ? PixelArea{} : disk;
    }

    /** Where a world point lands on an offscreen picture seen through camera: its column and row. */
    glm::uvec2 FindPixelOf(const Wotah::Camera& camera, const glm::vec3& point)
    {
        const glm::vec4 clip      = camera.GetViewProjection() * glm::vec4(point, 1.0f);
        const glm::vec2 onPicture = glm::vec2(clip) / clip.w * 0.5f + 0.5f;
        return glm::uvec2(onPicture *
                          glm::vec2(static_cast<float>(OffscreenSize.width), static_cast<float>(OffscreenSize.height)));
    }

    /** A picture's pixel at (column, row) as terrain_probe.frag wrote it: a float's 32 bits, red the lowest byte. */
    float ReadProbedHeight(const std::vector<uint8_t>& pixels, uint32_t column, uint32_t row)
    {
        const std::size_t index = (std::size_t{row} * OffscreenSize.width + column) * 4;
        const uint32_t    bits  = uint32_t{pixels[index]} | (uint32_t{pixels[index + 1]} << 8) |
                                  (uint32_t{pixels[index + 2]} << 16) | (uint32_t{pixels[index + 3]} << 24);
        return std::bit_cast<float>(bits);
    }

    /** A camera at a place looking at a target, seeing as far as the land and sea reach (as the beach's start camera).
     */
    Wotah::Camera MakeLandCamera(const glm::vec3& position, const glm::vec3& target)
    {
        Wotah::Camera camera;
        camera.SetPosition(position);
        camera.SetTarget(target);
        (void)camera.SetClipDistances(0.1f, 2000.0f); // valid values
        return camera;
    }

    /** The day light for land pictures: the sun in the south 45 degrees up, as a sky disk too, some sky light. */
    Wotah::SceneLights MakeDayLight()
    {
        Wotah::DirectionalLight sun;
        sun.Direction = Wotah::MakeLightDirection(glm::radians(180.0f), glm::radians(45.0f));
        sun.Color     = {1.0f, 0.95f, 0.85f};
        sun.Intensity = 0.9f;
        Wotah::SceneLights lights;
        lights.AmbientColor      = {0.2f, 0.24f, 0.32f};
        lights.DirectionalLights = {sun};
        lights.SkySun            = sun;
        lights.Shadows           = {glm::vec3(0.0f), LandShadowReach};
        return lights;
    }

    /** One UI frame of the program: the stats window, the hints and the control bar, laid out and drawn at size. */
    void ShowUiFrame(Wotah::UserInterface& ui, const Wotah::WindowSize& size)
    {
        constexpr float   FrameSeconds = 1.0f / 60.0f;
        Wotah::FrameStats stats;
        stats.PictureWidth  = size.Width;
        stats.PictureHeight = size.Height;
        ui.BeginFrame(size, size, FrameSeconds);
        Wotah::ShowStatsWindow(stats);
        Wotah::ShowHintsWindow();
        (void)Wotah::ShowControlBar(false, true); // nothing is clicked in these tests
        ui.EndFrame();
    }

    /** How many pixels are clearly blue: the ball, never the orange cube or the gray background. */
    std::size_t CountBluePixels(const std::vector<uint8_t>& pixels)
    {
        constexpr int ClearMargin = 40;
        std::size_t   count       = 0;
        for (std::size_t index = 0; index + 3 < pixels.size(); index += 4)
        {
            if (pixels[index + 2] > pixels[index] + ClearMargin)
            {
                ++count;
            }
        }
        return count;
    }
} // namespace

TEST_SUITE("gpu")
{
    TEST_CASE("the window opens hidden and gives Vulkan what it needs")
    {
        Wotah::Window window;
        CHECK_FALSE(window.Create({0, 10}, "refused"));

        REQUIRE(window.Create(HiddenWindowSize, "wotah tests (hidden)"));
        CHECK_FALSE(window.GetRequiredInstanceExtensions().empty());
        CHECK(window.GetFramebufferSize().Width == HiddenWindowSize.Width);
        CHECK(window.GetFramebufferSize().Height == HiddenWindowSize.Height);
        CHECK(window.GetSize().Width == HiddenWindowSize.Width); // the tests' screens are not scaled
        CHECK(window.GetSize().Height == HiddenWindowSize.Height);
        window.PollEvents(); // nothing waiting: returns right away
        CHECK_FALSE(window.ShouldClose());

        window.Destroy();
        window.Destroy(); // twice is safe
    }

    TEST_CASE("the device sets every handle and frees them all")
    {
        {
            Wotah::Device neverCreated; // destroyed without Create: safe
        }
        HiddenGpu gpu;
        REQUIRE(gpu.Create());
        const Wotah::Device& device = gpu.Device;
        CHECK(device.GetInstance() != VK_NULL_HANDLE);
        CHECK(device.GetSurface() != VK_NULL_HANDLE);
        CHECK(device.GetGpu() != VK_NULL_HANDLE);
        CHECK(device.GetHandle() != VK_NULL_HANDLE);
        CHECK(device.GetQueue() != VK_NULL_HANDLE);

        VkFenceCreateInfo fenceInfo = Wotah::MakeVulkanInfo<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
        VkFence           fence     = VK_NULL_HANDLE;
        CHECK(vkCreateFence(device.GetHandle(), &fenceInfo, nullptr, &fence) == VK_SUCCESS);
        vkDestroyFence(device.GetHandle(), fence, nullptr);
        device.WaitUntilIdle();

        gpu.Device.Destroy();
        gpu.Device.Destroy();
        CHECK(gpu.Device.GetHandle() == VK_NULL_HANDLE);
        CHECK(gpu.Device.GetInstance() == VK_NULL_HANDLE);
        gpu.Device.WaitUntilIdle(); // after Destroy: safe
    }

    TEST_CASE("the swapchain has the window's size, its images and views")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());
        {
            Wotah::Swapchain neverCreated;
        }
        Wotah::Swapchain swapchain;
        REQUIRE(swapchain.Create(gpu.Device, gpu.Window));
        CHECK(swapchain.GetExtent().width == HiddenWindowSize.Width);
        CHECK(swapchain.GetExtent().height == HiddenWindowSize.Height);
        CHECK(swapchain.GetFormat() != VK_FORMAT_UNDEFINED);

        const uint32_t count = swapchain.GetImageCount();
        CHECK(count >= 2);
        for (uint32_t index = 0; index < count; ++index)
        {
            CHECK(swapchain.GetImage(index) != VK_NULL_HANDLE);
            CHECK(swapchain.GetView(index) != VK_NULL_HANDLE);
        }
        CHECK(swapchain.GetImage(count) == VK_NULL_HANDLE); // outside: empty, not undefined
        CHECK(swapchain.GetView(count) == VK_NULL_HANDLE);

        // Take one image; an empty submit waits on the semaphore so it is used up cleanly.
        const VkDevice        device = gpu.Device.GetHandle();
        VkSemaphoreCreateInfo semaphoreInfo =
            Wotah::MakeVulkanInfo<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        REQUIRE(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailable) == VK_SUCCESS);
        CHECK(swapchain.AcquireNextImage(imageAvailable).has_value());
        const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        VkSubmitInfo               submit    = Wotah::MakeVulkanInfo<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
        submit.waitSemaphoreCount            = 1;
        submit.pWaitSemaphores               = &imageAvailable;
        submit.pWaitDstStageMask             = &waitStage;
        CHECK(vkQueueSubmit(gpu.Device.GetQueue(), 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS);
        gpu.Device.WaitUntilIdle();
        vkDestroySemaphore(device, imageAvailable, nullptr);

        swapchain.Destroy();
        swapchain.Destroy();
        CHECK(swapchain.GetImageCount() == 0);
    }

    TEST_CASE("pipelines build with every option and refuse settings that do not fit")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());
        const VkFormat                colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
        const std::optional<VkFormat> depthFormat = Wotah::FindDepthFormat(gpu.Device);
        REQUIRE(depthFormat.has_value());

        Wotah::Pipeline cube;
        Wotah::Lighting lighting;
        REQUIRE(lighting.Create(gpu.Device));
        const std::array<VkDescriptorSetLayout, 1> lightLayouts{lighting.GetSetLayout()};
        CHECK(cube.Create(gpu.Device,
                          Wotah::MakeMeshSettings(CubeFragmentShader, colorFormat, *depthFormat, lightLayouts)));
        CHECK(cube.GetHandle() != VK_NULL_HANDLE);
        CHECK(cube.GetLayout() != VK_NULL_HANDLE);

        // Vertex buffer layout, depth, blending, push constants and a uniform buffer, as water will need them.
        struct Vertex
        {
            float Position[3] = {};
            float Normal[3]   = {};
        };

        VkDescriptorSetLayoutBinding waveBinding{};
        waveBinding.binding         = 0;
        waveBinding.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        waveBinding.descriptorCount = 1;
        waveBinding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;
        VkDescriptorSetLayoutCreateInfo setInfo =
            Wotah::MakeVulkanInfo<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
        setInfo.bindingCount            = 1;
        setInfo.pBindings               = &waveBinding;
        VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
        REQUIRE(vkCreateDescriptorSetLayout(gpu.Device.GetHandle(), &setInfo, nullptr, &setLayout) == VK_SUCCESS);

        const std::array<VkVertexInputBindingDescription, 1> bindings{
            {{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX}}};
        const std::array<VkVertexInputAttributeDescription, 2> attributes{{
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, Position)},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, Normal)},
        }};
        constexpr uint32_t PushBytes = sizeof(glm::mat4) + sizeof(float); // tests/shaders/water_test.vert
        const std::array<VkPushConstantRange, 1>   pushConstants{{{VK_SHADER_STAGE_VERTEX_BIT, 0, PushBytes}}};
        const std::array<VkDescriptorSetLayout, 1> setLayouts{setLayout};

        Wotah::PipelineSettings water;
        water.VertexShader         = WaterTestVertexShader;
        water.FragmentShader       = WaterTestFragmentShader;
        water.ColorFormat          = colorFormat;
        water.DepthFormat          = *depthFormat;
        water.VertexBindings       = bindings;
        water.VertexAttributes     = attributes;
        water.CullMode             = VK_CULL_MODE_BACK_BIT;
        water.DepthTest            = true;
        water.DepthWrite           = true;
        water.Blend                = Wotah::BlendMode::Alpha;
        water.PushConstants        = pushConstants;
        water.DescriptorSetLayouts = setLayouts;

        SUBCASE("vertex input, depth, alpha blending, push constants and a descriptor set")
        {
            Wotah::Pipeline pipeline;
            CHECK(pipeline.Create(gpu.Device, water));
        }
        SUBCASE("additive blending, a triangle strip, clockwise, no depth writes")
        {
            Wotah::PipelineSettings additive = water;
            additive.Blend                   = Wotah::BlendMode::Additive;
            additive.Topology                = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
            additive.FrontFace               = VK_FRONT_FACE_CLOCKWISE;
            additive.DepthWrite              = false;
            additive.DepthCompare            = VK_COMPARE_OP_LESS_OR_EQUAL;
            Wotah::Pipeline pipeline;
            CHECK(pipeline.Create(gpu.Device, additive));
        }
        SUBCASE("a second and third set: the sea's and the land's pipelines")
        {
            Wotah::Image depth;
            REQUIRE(depth.Create(gpu.Device, {64, 64}, *depthFormat,
                                 VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT));
            Wotah::SceneBehind behind;
            REQUIRE(behind.Create(gpu.Device, depth, colorFormat));
            Wotah::MirrorView mirror;
            REQUIRE(mirror.Create(gpu.Device, {64, 64}, colorFormat, *depthFormat));
            CHECK(mirror.GetExtent().width == 32); // MirrorScale of the frame
            Wotah::SeaState seaState;
            REQUIRE(seaState.Create(gpu.Device));
            CHECK(seaState.GetSetLayout() != VK_NULL_HANDLE);
            Wotah::TerrainRenderer land;
            CHECK(land.Create(gpu.Device, colorFormat, *depthFormat, lighting, seaState));
            CHECK(land.GetShapeSetLayout() != VK_NULL_HANDLE);
            seaState.SetFloor(land.GetShapeBuffer()); // the land's shape is the sea's floor
            Wotah::WaterRenderer sea;
            CHECK(sea.Create(gpu.Device, colorFormat, *depthFormat, lighting, behind, mirror, seaState));
            CHECK(land.SetShape(Wotah::TerrainShape{}));
            Wotah::TerrainShape broken;
            broken.HillWavelength = 0.0f;
            CHECK_FALSE(land.SetShape(broken)); // refused, logged, the last shape stays
        }
        SUBCASE("depth only, with a depth bias: the shadow pipeline")
        {
            Wotah::PipelineSettings depthOnly = water;
            depthOnly.FragmentShader          = {};
            depthOnly.ColorFormat             = VK_FORMAT_UNDEFINED;
            depthOnly.DepthBiasConstant       = 2.0f;
            depthOnly.DepthBiasSlope          = 2.0f;
            Wotah::Pipeline pipeline;
            CHECK(pipeline.Create(gpu.Device, depthOnly));
            Wotah::Pipeline shadows;
            CHECK(shadows.Create(gpu.Device, Wotah::MakeMeshShadowSettings(lighting.GetShadowMaps().GetFormat())));
        }
        SUBCASE("settings that do not fit are refused")
        {
            Wotah::PipelineSettings noShaders;
            noShaders.ColorFormat                    = colorFormat;
            Wotah::PipelineSettings noDepthFormat    = water;
            noDepthFormat.DepthFormat                = VK_FORMAT_UNDEFINED;
            Wotah::PipelineSettings noColorFormat    = water; // a fragment shader with nothing to write color into
            noColorFormat.ColorFormat                = VK_FORMAT_UNDEFINED;
            Wotah::PipelineSettings noFragmentShader = water; // color to write, but nothing that writes it
            noFragmentShader.FragmentShader          = {};
            Wotah::PipelineSettings drawsIntoNothing = water; // depth only, but without a depth format
            drawsIntoNothing.FragmentShader          = {};
            drawsIntoNothing.ColorFormat             = VK_FORMAT_UNDEFINED;
            drawsIntoNothing.DepthFormat             = VK_FORMAT_UNDEFINED;
            drawsIntoNothing.DepthTest               = false;
            drawsIntoNothing.DepthWrite              = false;

            Wotah::Pipeline pipeline;
            CHECK_FALSE(pipeline.Create(gpu.Device, noShaders));
            CHECK_FALSE(pipeline.Create(gpu.Device, noDepthFormat));
            CHECK_FALSE(pipeline.Create(gpu.Device, noColorFormat));
            CHECK_FALSE(pipeline.Create(gpu.Device, noFragmentShader));
            CHECK_FALSE(pipeline.Create(gpu.Device, drawsIntoNothing));
            CHECK(pipeline.GetHandle() == VK_NULL_HANDLE);
        }
        SUBCASE("destroying twice clears the handles")
        {
            Wotah::Pipeline pipeline;
            CHECK(pipeline.Create(gpu.Device, water));
            pipeline.Destroy();
            pipeline.Destroy();
            CHECK(pipeline.GetHandle() == VK_NULL_HANDLE);
            CHECK(pipeline.GetLayout() == VK_NULL_HANDLE);
        }

        vkDestroyDescriptorSetLayout(gpu.Device.GetHandle(), setLayout, nullptr);
    }

    TEST_CASE("the renderer draws frames of the scene, with its depth image")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());
        Wotah::Swapchain swapchain;
        REQUIRE(swapchain.Create(gpu.Device, gpu.Window));
        {
            Wotah::Renderer neverCreated;
        }
        Wotah::Renderer renderer;
        REQUIRE(renderer.Create(gpu.Device, swapchain));
        CHECK(renderer.GetDepthFormat() != VK_FORMAT_UNDEFINED);
        Scene scene;
        REQUIRE(scene.Create(gpu.Device, swapchain.GetFormat(), renderer.GetDepthFormat(), MakeOldLight(),
                             renderer.GetSceneBehind(), renderer.GetMirror()));

        Wotah::Camera camera;
        (void)camera.SetAspectRatio(static_cast<float>(HiddenWindowSize.Width) /
                                    static_cast<float>(HiddenWindowSize.Height)); // valid, so this cannot fail
        constexpr int FrameCount = 30;
        int           drawn      = 0;
        for (int frame = 0; frame < FrameCount; ++frame)
        {
            gpu.Window.PollEvents();
            const Wotah::FrameStart start = renderer.BeginFrame();
            REQUIRE(start != Wotah::FrameStart::Failed);
            if (start == Wotah::FrameStart::Skip)
            {
                continue;
            }
            OffscreenScene what;
            what.Lights      = MakeOldLight();
            what.HasSky      = true;
            what.IsBallFirst = frame % 2 == 0;
            scene.RecordShadows(renderer.GetCommandBuffer(), what);
            renderer.BeginScene();
            scene.Record(renderer.GetCommandBuffer(), camera.GetViewProjection(), what);
            REQUIRE(renderer.EndFrame());
            ++drawn;
        }
        // A frame that draws nothing and never calls BeginScene: EndFrame begins it, so the image is cleared and shown.
        if (renderer.BeginFrame() == Wotah::FrameStart::Ready)
        {
            CHECK(renderer.EndFrame());
        }
        gpu.Device.WaitUntilIdle();
        CHECK(drawn == FrameCount);

        renderer.Destroy();
        renderer.Destroy();
        CHECK(renderer.GetCommandBuffer() == VK_NULL_HANDLE);
    }

    TEST_CASE("the scene renderer draws the beach as the program does: over and under the water, through a resize")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());
        Wotah::Swapchain swapchain;
        REQUIRE(swapchain.Create(gpu.Device, gpu.Window));
        Wotah::Renderer renderer;
        REQUIRE(renderer.Create(gpu.Device, swapchain));
        {
            Wotah::SceneRenderer neverCreated; // destroyed without Create: safe
        }
        Wotah::SceneRenderer scene;
        REQUIRE(scene.Create(gpu.Device, swapchain.GetFormat(), renderer, Wotah::BeachLand));
        Wotah::MeshPipelines ballLook;
        REQUIRE(ballLook.Create(gpu.Device, BallFragmentShader, swapchain.GetFormat(), renderer.GetDepthFormat(),
                                scene.GetLighting()));
        Wotah::Mesh ball;
        REQUIRE(ball.Create(gpu.Device, Wotah::MakeSphere(Wotah::BeachBallRadius, BallSegments, BallRings)));

        // A low afternoon sun, as the sky gives it; the ball lying in the shallow water.
        Wotah::SkyLightSettings afternoon;
        afternoon.TimeOfDayHours = 16.5f;
        Wotah::SceneLights lights;
        Wotah::PutSkyLights(Wotah::MakeSkyLights(afternoon), lights);
        const float     floor = Wotah::GetTerrainHeight(Wotah::BeachLand, {-16.0f, 0.0f});
        const glm::mat4 place =
            glm::translate(glm::mat4(1.0f), glm::vec3(-16.0f, floor + Wotah::BeachBallRadius, 0.0f));
        const std::array<Wotah::MeshObject, 1> objects{{{&ballLook, &ball, place}}};

        // The beach's sea with waves, moving on a little every frame.
        Wotah::Sea sea = Wotah::BeachSea;
        sea.Waves      = Wotah::MakeSeaWaves(0.5f, {1.0f, 0.0f});

        // Every other frame from under the water, which adds the underwater pass and leaves the mirror empty.
        const Wotah::Camera above      = Wotah::MakeBeachCamera();
        const Wotah::Camera under      = MakeLandCamera({-16.0f, floor + 0.4f, 4.0f}, {-16.0f, floor + 0.3f, 0.0f});
        constexpr int       FrameCount = 12;
        int                 drawn      = 0;
        for (int frame = 0; frame < FrameCount; ++frame)
        {
            if (frame == FrameCount / 2)
            {
                gpu.Window.SetSize({800, 300}); // the renderer's images are made again; the scene keeps reading them
            }
            gpu.Window.PollEvents();
            if (gpu.Window.TakeResize() || swapchain.IsOutOfDate())
            {
                REQUIRE(renderer.RecreateSwapchain(gpu.Device, gpu.Window));
            }
            const Wotah::FrameStart start = renderer.BeginFrame();
            REQUIRE(start != Wotah::FrameStart::Failed);
            if (start == Wotah::FrameStart::Skip)
            {
                continue;
            }
            Wotah::Camera    camera = frame % 2 == 0 ? above : under;
            const VkExtent2D extent = swapchain.GetExtent();
            (void)camera.SetAspectRatio(static_cast<float>(extent.width) / static_cast<float>(extent.height));
            lights.Shadows = {camera.GetPosition(), LandShadowReach};
            sea.Seconds    = 0.1 * frame;
            scene.Draw(renderer, camera, lights, sea, objects);
            REQUIRE(renderer.EndFrame());
            ++drawn;
        }
        gpu.Device.WaitUntilIdle();
        CHECK(drawn == FrameCount);
        CHECK(swapchain.GetExtent().width == 800);

        scene.Destroy();
        scene.Destroy(); // twice is safe
    }

    TEST_CASE(
        "the scene looks right: the lit cube with black edges in front of the blue ball, through the light buffer")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());

        const Wotah::Camera camera = MakeStartCamera();
        OffscreenScene      what;
        what.Lights                       = MakeOldLight();
        const std::vector<uint8_t> pixels = DrawSceneOffscreen(gpu.Device, camera, what);

        // One white light plus ambient 0.25, as the light was before it came from the light buffer, gives the very same
        // colors as before: orange * (0.25 + 0.75 * light); the top gets the most light, the front the least.
        CHECK(CountPixels(pixels, 0xED, 0x76, 0x00) > 3000);   // top (seen flat from the nearly level start view)
        CHECK(CountPixels(pixels, 0x85, 0x42, 0x00) > 10000);  // right (+x)
        CHECK(CountPixels(pixels, 0x6B, 0x35, 0x00) > 10000);  // front (+z)
        CHECK(CountPixels(pixels, 0x00, 0x00, 0x00) > 500);    // the black edges
        CHECK(CountPixels(pixels, 0x1A, 0x1A, 0x1A) > 200000); // the background around them
        CHECK(CountBluePixels(pixels) > 1000);                 // the ball, partly behind the cube

        // The center of the picture is the cube, not the background.
        const std::size_t center = (300 * 600 + 300) * 4;
        CHECK(pixels[center] > 0x40);

        // The depth image decides what is in front: drawing the ball first gives the very same picture.
        what.IsBallFirst = true;
        CHECK(DrawSceneOffscreen(gpu.Device, camera, what) == pixels);
    }

    TEST_CASE("colored lights add up per face, and a light at intensity 0 is off")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());

        // No ambient light; red light falls on the right face (+x), green light on the top, nothing on the front.
        Wotah::DirectionalLight red;
        red.Direction = {-1.0f, 0.0f, 0.0f};
        red.Color     = {1.0f, 0.0f, 0.0f};
        Wotah::DirectionalLight green;
        green.Direction = {0.0f, -1.0f, 0.0f};
        green.Color     = {0.0f, 1.0f, 0.0f};
        Wotah::DirectionalLight off; // white, but switched off
        off.Direction = {0.0f, 0.0f, -1.0f};
        off.Intensity = 0.0f;
        Wotah::SceneLights lights;
        lights.AmbientColor      = glm::vec3(0.0f);
        lights.DirectionalLights = {red, green, off};

        OffscreenScene what;
        what.Lights                       = lights;
        const std::vector<uint8_t> pixels = DrawSceneOffscreen(gpu.Device, MakeStartCamera(), what);

        // The orange cube (1, 0.5, 0): red light leaves (1, 0, 0), green light (0, 0.5, 0), no light black.
        CHECK(CountPixels(pixels, 0xFF, 0x00, 0x00) > 10000); // right face
        CHECK(CountPixels(pixels, 0x00, 0x80, 0x00) > 3000);  // top face (seen flat)
        CHECK(CountPixels(pixels, 0x00, 0x00, 0x00) > 25000); // front face and the edges
    }

    TEST_CASE("light bounced up from the ground: half of it on upright sides, none on the top")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());

        // Only the ground's bounce, no sky light and no sun, so every face shows how much of it it gets.
        Wotah::SceneLights lights;
        lights.AmbientColor = glm::vec3(0.0f);
        lights.GroundBounce = glm::vec3(0.8f);
        OffscreenScene what;
        what.Lights                       = lights;
        const std::vector<uint8_t> pixels = DrawSceneOffscreen(gpu.Device, MakeStartCamera(), what);

        // The orange cube (1, 0.5, 0): both upright faces in view get half of 0.8, (0.4, 0.2, 0); the top faces up and
        // sees no ground, so it stays black, with the edges.
        CHECK(CountPixels(pixels, 0x66, 0x33, 0x00) > 20000); // right and front face: 0.4 and 0.2 of 255
        CHECK(CountPixels(pixels, 0x00, 0x00, 0x00) > 3000);  // top face (seen flat) and edges
    }

    TEST_CASE("the light buffer takes up to the maximum of directional lights and refuses bad ones")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());
        {
            Wotah::Lighting neverCreated; // destroyed without Create: safe
        }
        Wotah::Lighting lighting;
        REQUIRE(lighting.Create(gpu.Device));
        CHECK(lighting.GetSetLayout() != VK_NULL_HANDLE);

        Wotah::SceneLights lights;
        lights.DirectionalLights.resize(Wotah::MaxDirectionalLights);
        CHECK(lighting.Update(lights, Wotah::Camera{}));

        lights.DirectionalLights.resize(Wotah::MaxDirectionalLights + 1);
        CHECK_FALSE(lighting.Update(lights, Wotah::Camera{})); // one too many

        lights.DirectionalLights.resize(1);
        lights.DirectionalLights[0].Direction = glm::vec3(0.0f);
        CHECK_FALSE(lighting.Update(lights, Wotah::Camera{})); // no direction
        lights.DirectionalLights[0].Direction = glm::vec3(std::numeric_limits<float>::quiet_NaN());
        CHECK_FALSE(lighting.Update(lights, Wotah::Camera{}));

        lighting.Destroy();
        lighting.Destroy(); // twice is safe
        CHECK(lighting.GetSetLayout() == VK_NULL_HANDLE);
    }

    TEST_CASE("the sky: deep blue overhead, pale at the horizon, haze below, the sun where its light comes from")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());

        // The program's sun: east, 45 degrees up, warm white.
        Wotah::DirectionalLight sun;
        sun.Direction = Wotah::MakeLightDirection(glm::radians(90.0f), glm::radians(45.0f));
        sun.Color     = {1.0f, 0.95f, 0.85f};
        OffscreenScene sky;
        sky.Lights.DirectionalLights = {sun};
        sky.HasSky                   = true;
        sky.HasShapes                = false;

        // Straight at the horizon, away from the sun (north): shaders/sky.frag's HorizonColor.
        const std::array<int, 3> horizon =
            GetCenterColor(DrawSceneOffscreen(gpu.Device, MakeCameraLookingAt({0.0f, 0.0f, -1.0f}), sky));
        CHECK(IsColorNear(horizon, 0.62f, 0.78f, 0.94f));

        // High up, away from the sun: deep blue, darker and bluer than at the horizon.
        const std::array<int, 3> high =
            GetCenterColor(DrawSceneOffscreen(gpu.Device, MakeCameraLookingAt({-0.2f, 1.0f, 0.0f}), sky));
        CHECK(high[2] > high[0] + 80);
        CHECK(high[0] < horizon[0]);

        // Far down: the blue-gray haze, BelowColor.
        const std::array<int, 3> below =
            GetCenterColor(DrawSceneOffscreen(gpu.Device, MakeCameraLookingAt({0.05f, -1.0f, 0.0f}), sky));
        CHECK(IsColorNear(below, 0.30f, 0.36f, 0.42f));

        // Straight at the sun: its disk, far brighter than its warm white light, so white-hot.
        const std::array<int, 3> disk =
            GetCenterColor(DrawSceneOffscreen(gpu.Device, MakeCameraLookingAt(-sun.Direction), sky));
        CHECK(IsColorNear(disk, 1.0f, 1.0f, 1.0f));

        // The sky glows around the sun: 3 degrees beside it, far brighter than at the same height 60 degrees away.
        const glm::vec3          besideSun = -Wotah::MakeLightDirection(glm::radians(93.0f), glm::radians(45.0f));
        const glm::vec3          awaySun   = -Wotah::MakeLightDirection(glm::radians(150.0f), glm::radians(45.0f));
        const std::array<int, 3> beside =
            GetCenterColor(DrawSceneOffscreen(gpu.Device, MakeCameraLookingAt(besideSun), sky));
        const std::array<int, 3> away =
            GetCenterColor(DrawSceneOffscreen(gpu.Device, MakeCameraLookingAt(awaySun), sky));
        CHECK(beside[0] + beside[1] + beside[2] > away[0] + away[1] + away[2] + 60);

        // A low sun's dim, red light shows an orange disk: red at full, green well below it, no blue (the sky behind is
        // hidden), never a black one.
        sky.Lights.DirectionalLights.front().Color = {0.5f, 0.1f, 0.0f};
        const std::array<int, 3> redDisk =
            GetCenterColor(DrawSceneOffscreen(gpu.Device, MakeCameraLookingAt(-sun.Direction), sky));
        CHECK(redDisk[0] > 230);
        CHECK(redDisk[1] > 100);
        CHECK(redDisk[1] < 200);
        CHECK(redDisk[2] < 10);

        // A switched-off sun shows no disk at all: the blue sky, not a black hole in it.
        sky.Lights.DirectionalLights.front().Intensity = 0.0f;
        const std::array<int, 3> noSun =
            GetCenterColor(DrawSceneOffscreen(gpu.Device, MakeCameraLookingAt(-sun.Direction), sky));
        CHECK(noSun[2] > noSun[0] + 40);
        CHECK(noSun[2] > 150);
    }

    TEST_CASE("a low sun: a sunset sky, orange on its side and mauve away from it, the sun round and whole as it sets")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());

        // A sun in the east, 2 degrees up, switched off: only the sky shows, no disk and no glow.
        Wotah::DirectionalLight sun;
        sun.Direction = Wotah::MakeLightDirection(glm::radians(90.0f), glm::radians(2.0f));
        sun.Intensity = 0.0f;
        OffscreenScene sky;
        sky.Lights.DirectionalLights = {sun};
        sky.HasSky                   = true;
        sky.HasShapes                = false;

        // On the horizon under the sun (east): orange. Opposite it (west): dusky mauve, red and blue above green.
        const std::array<int, 3> sunSide =
            GetCenterColor(DrawSceneOffscreen(gpu.Device, MakeCameraLookingAt({1.0f, 0.0f, 0.0f}), sky));
        CHECK(sunSide[0] > sunSide[2] + 100);
        const std::array<int, 3> farSide =
            GetCenterColor(DrawSceneOffscreen(gpu.Device, MakeCameraLookingAt({-1.0f, 0.0f, 0.0f}), sky));
        CHECK(farSide[0] > farSide[1]);
        CHECK(farSide[2] > farSide[1]);

        // Lit, 2 degrees up, the whole disk shows, white-hot.
        sky.Lights.DirectionalLights.front().Intensity = 1.0f;
        const std::array<int, 3> up =
            GetCenterColor(DrawSceneOffscreen(gpu.Device, MakeCameraLookingAt(-sun.Direction), sky));
        CHECK(IsColorNear(up, 1.0f, 1.0f, 1.0f));

        // With its middle on the horizon it is still round: half its radius below the middle (6 of its 12.6 pixels at
        // the camera's 45 degrees over 600 pixels), under the horizon, the disk is as bright as just as far above.
        sky.Lights.DirectionalLights.front().Direction = Wotah::MakeLightDirection(glm::radians(90.0f), 0.0f);
        const std::vector<uint8_t> onHorizon =
            DrawSceneOffscreen(gpu.Device, MakeCameraLookingAt({1.0f, 0.0f, 0.0f}), sky);
        constexpr uint32_t       Middle     = OffscreenSize.width / 2;
        constexpr uint32_t       HalfInDisk = 6;
        const std::array<int, 3> above      = GetPixel(onHorizon, Middle, Middle - HalfInDisk);
        const std::array<int, 3> below      = GetPixel(onHorizon, Middle, Middle + HalfInDisk);
        CHECK(below[0] > 245);
        CHECK(std::abs(below[1] - above[1]) <= 4);

        // One and a half degrees below the horizon, where the ground's far edge hides it from the start camera, it is
        // still whole and white-hot: it sinks behind the ground, it does not vanish above it.
        sky.Lights.DirectionalLights.front().Direction =
            Wotah::MakeLightDirection(glm::radians(90.0f), glm::radians(-1.5f));
        const std::array<int, 3> sinking = GetCenterColor(
            DrawSceneOffscreen(gpu.Device, MakeCameraLookingAt(-sky.Lights.DirectionalLights.front().Direction), sky));
        CHECK(IsColorNear(sinking, 1.0f, 1.0f, 1.0f));

        // Six and a half degrees below it has set, its glow with it: the sky's color, far from white.
        sky.Lights.DirectionalLights.front().Direction =
            Wotah::MakeLightDirection(glm::radians(90.0f), glm::radians(-6.5f));
        const std::array<int, 3> set = GetCenterColor(
            DrawSceneOffscreen(gpu.Device, MakeCameraLookingAt(-sky.Lights.DirectionalLights.front().Direction), sky));
        CHECK(set[0] + set[1] + set[2] < 600);
    }

    TEST_CASE("the sun stays round anywhere on the picture, also near its edge where a wide camera stretches things")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());

        // The sun due east on the horizon, warm white.
        Wotah::DirectionalLight sun;
        sun.Direction = Wotah::MakeLightDirection(glm::radians(90.0f), 0.0f);
        sun.Color     = {1.0f, 0.95f, 0.85f};
        OffscreenScene sky;
        sky.Lights.DirectionalLights = {sun};
        sky.HasSky                   = true;
        sky.HasShapes                = false;

        // A 90 degree camera looking along the horizon 40 degrees left of it: the sun stands on the middle row, near
        // the right edge. A disk measured by angle would show 1.3 times wider than high there (cos 40 to cos^2 40).
        Wotah::Camera camera = MakeCameraLookingAt(-Wotah::MakeLightDirection(glm::radians(50.0f), 0.0f));
        REQUIRE(camera.SetFieldOfView(glm::radians(90.0f)));
        const PixelArea disk = FindSunDisk(DrawSceneOffscreen(gpu.Device, camera, sky));
        CAPTURE(disk.Left);
        CAPTURE(disk.Top);
        CAPTURE(disk.Right);
        CAPTURE(disk.Bottom);

        // Where it belongs: 300 + 300 tan 40 = 552 across, on the middle row; 2 * 300 tan 1 = 10.5 pixels across.
        CHECK(disk.Left > 530);
        CHECK(disk.Right < 575);
        const int width  = static_cast<int>(disk.Right - disk.Left);
        const int height = static_cast<int>(disk.Bottom - disk.Top);
        CHECK(height >= 9);
        CHECK(height <= 13);
        CHECK(std::abs(width - height) <= 1);
    }

    TEST_CASE("the sun's rays: faint, they turn slowly while the camera looks at the sun, and are gone looking away")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());

        Wotah::DirectionalLight sun;
        sun.Direction = Wotah::MakeLightDirection(glm::radians(90.0f), glm::radians(45.0f));
        sun.Color     = {1.0f, 0.95f, 0.85f};
        OffscreenScene sky;
        sky.Lights.DirectionalLights = {sun};
        sky.HasSky                   = true;
        sky.HasShapes                = false;

        // Looking at the sun, half a minute apart: the rays have turned a little (1/20 of a turn), so the glow around
        // the sun differs, but only faintly. The glow's breath is back where it was, so only the rays can differ.
        const Wotah::Camera        atSun  = MakeCameraLookingAt(-sun.Direction);
        const std::vector<uint8_t> before = DrawSceneOffscreen(gpu.Device, atSun, sky);
        sky.SkySeconds                    = 30.0f;
        const std::vector<uint8_t> after  = DrawSceneOffscreen(gpu.Device, atSun, sky);
        CHECK(CountDifferentPixels(before, after, 4) > 1000);
        CHECK(CountDifferentPixels(before, after, 40) == 0);

        // Looking 40 degrees to the side of it, along the horizon under it (with a wide camera, so the sun is still in
        // the picture), the rays are gone: nothing moves.
        Wotah::Camera away = MakeCameraLookingAt(-Wotah::MakeLightDirection(glm::radians(50.0f), 0.0f));
        REQUIRE(away.SetFieldOfView(glm::radians(90.0f)));
        sky.Lights.DirectionalLights.front().Direction = Wotah::MakeLightDirection(glm::radians(90.0f), 0.0f);
        sky.SkySeconds                                 = 0.0f;
        const std::vector<uint8_t> awayBefore          = DrawSceneOffscreen(gpu.Device, away, sky);
        sky.SkySeconds                                 = 30.0f;
        const std::vector<uint8_t> awayAfter           = DrawSceneOffscreen(gpu.Device, away, sky);
        CHECK(CountDifferentPixels(awayBefore, awayAfter, 1) == 0);
    }

    TEST_CASE("the night sky: deep blue with a few stars, the full moon a bright disk with darker seas, no sun")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());

        // The sun 30 degrees below the horizon in the west, giving no light; the full moon 30 degrees up in the east,
        // white, at its full brightness.
        Wotah::DirectionalLight sun;
        sun.Direction = Wotah::MakeLightDirection(glm::radians(270.0f), glm::radians(-30.0f));
        sun.Intensity = 0.0f;
        OffscreenScene sky;
        sky.Lights.DirectionalLights = {sun};
        sky.Moon.Direction           = Wotah::MakeLightDirection(glm::radians(90.0f), glm::radians(30.0f));
        sky.Moon.Color               = {1.0f, 0.97f, 0.92f};
        sky.Moon.Intensity           = 1.0f;
        sky.HasSky                   = true;
        sky.HasShapes                = false;

        // Straight at the moon: a bright, nearly white disk, about as red as blue. Just above its middle lies a sea
        // (Mare Vaporum), darker.
        const std::vector<uint8_t> atMoon =
            DrawSceneOffscreen(gpu.Device, MakeCameraLookingAt(-sky.Moon.Direction), sky);
        const std::array<int, 3> face = GetCenterColor(atMoon);
        CHECK(face[0] > 150);
        CHECK(std::abs(face[0] - face[2]) < 30);
        int darkest = 255;
        for (uint32_t y = OffscreenSize.height / 2 - 10; y <= OffscreenSize.height / 2 + 10; ++y)
        {
            darkest = std::min(darkest, GetPixel(atMoon, OffscreenSize.width / 2, y)[0]);
        }
        CHECK(darkest < face[0] - 15);

        // Its halo: the sky 2 degrees from its middle is clearly lighter than 20 degrees away.
        const glm::vec3          nearMoon = -Wotah::MakeLightDirection(glm::radians(90.0f), glm::radians(32.0f));
        const glm::vec3          farMoon  = -Wotah::MakeLightDirection(glm::radians(90.0f), glm::radians(50.0f));
        const std::array<int, 3> halo =
            GetCenterColor(DrawSceneOffscreen(gpu.Device, MakeCameraLookingAt(nearMoon), sky));
        const std::array<int, 3> open =
            GetCenterColor(DrawSceneOffscreen(gpu.Device, MakeCameraLookingAt(farMoon), sky));
        CHECK(halo[0] + halo[1] + halo[2] > open[0] + open[1] + open[2] + 30);

        // High up in the north, away from the moon: a dark sky, bluer than red under the moon, with a few small light
        // points, not many.
        const std::vector<uint8_t> up = DrawSceneOffscreen(gpu.Device, MakeCameraLookingAt({0.0f, 1.0f, -0.6f}), sky);
        std::size_t                stars = 0;
        std::size_t                dark  = 0;
        for (std::size_t index = 0; index + 3 < up.size(); index += 4)
        {
            const int least = std::min({up[index], up[index + 1], up[index + 2]});
            stars += least > 80 ? 1 : 0;
            dark += up[index] + up[index + 1] + up[index + 2] < 45 ? 1 : 0;
        }
        const std::size_t pixelCount = up.size() / 4;
        CHECK(stars > 20);
        CHECK(stars < pixelCount / 100);
        CHECK(dark > pixelCount * 9 / 10);
        const std::array<int, 3> moonlit = GetCenterColor(up);
        CHECK(moonlit[2] > moonlit[0]);

        // A moon with no brightness shows no disk: the dark sky where it stood.
        sky.Moon.Intensity = 0.0f;
        const std::array<int, 3> noMoon =
            GetCenterColor(DrawSceneOffscreen(gpu.Device, MakeCameraLookingAt(-sky.Moon.Direction), sky));
        CHECK(noMoon[0] < 60);
    }

    TEST_CASE("the moon by day: a pale disk over the blue sky, brighter than the sky beside it, without a halo")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());

        // The sun 45 degrees up in the east, the moon 20 degrees up in the west.
        Wotah::DirectionalLight sun;
        sun.Direction = Wotah::MakeLightDirection(glm::radians(90.0f), glm::radians(45.0f));
        sun.Color     = {1.0f, 0.95f, 0.85f};
        OffscreenScene sky;
        sky.Lights.DirectionalLights = {sun};
        sky.Moon.Direction           = Wotah::MakeLightDirection(glm::radians(270.0f), glm::radians(20.0f));
        sky.Moon.Intensity           = 1.0f;
        sky.HasSky                   = true;
        sky.HasShapes                = false;

        const std::array<int, 3> face =
            GetCenterColor(DrawSceneOffscreen(gpu.Device, MakeCameraLookingAt(-sky.Moon.Direction), sky));
        const glm::vec3          besideMoon = -Wotah::MakeLightDirection(glm::radians(275.0f), glm::radians(20.0f));
        const std::array<int, 3> beside =
            GetCenterColor(DrawSceneOffscreen(gpu.Device, MakeCameraLookingAt(besideMoon), sky));
        CHECK(face[0] > beside[0] + 60);
        CHECK(face[0] < 250); // pale, not white-hot like the sun

        // No halo by day: the sky 2 degrees beside it is the plain sky's color, as without a moon.
        const glm::vec3          nearMoon = -Wotah::MakeLightDirection(glm::radians(272.0f), glm::radians(20.0f));
        const std::array<int, 3> withMoon =
            GetCenterColor(DrawSceneOffscreen(gpu.Device, MakeCameraLookingAt(nearMoon), sky));
        sky.Moon.Intensity = 0.0f;
        const std::array<int, 3> withoutMoon =
            GetCenterColor(DrawSceneOffscreen(gpu.Device, MakeCameraLookingAt(nearMoon), sky));
        CHECK(IsColorNear(withMoon, static_cast<float>(withoutMoon[0]) / 255.0f,
                          static_cast<float>(withoutMoon[1]) / 255.0f, static_cast<float>(withoutMoon[2]) / 255.0f));
    }

    TEST_CASE("far ground fades into the haze's color, which the lights set: pale blue by day, near black at night")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());
        OffscreenScene ground;
        ground.Lights    = MakeOldLight();
        ground.HasLand   = true; // flat land at 0, reaching far enough for the haze to take most of it
        ground.HasShapes = false;

        // Low over the land, looking at it 800 units away: mostly haze, the day's pale blue.
        const Wotah::Camera      far = MakeLandCamera({0.0f, 2.0f, 0.0f}, {0.0f, 0.0f, -800.0f});
        const std::array<int, 3> day = GetCenterColor(DrawSceneOffscreen(gpu.Device, far, ground));
        CAPTURE(day);
        CHECK(day[2] > 150);
        CHECK(day[2] > day[0]);

        // The same land in a night's haze: far darker, the blue most of all (the day haze is the bluest part).
        ground.Lights.HazeColor        = {0.02f, 0.028f, 0.055f};
        const std::array<int, 3> night = GetCenterColor(DrawSceneOffscreen(gpu.Device, far, ground));
        CAPTURE(night);
        CHECK(night[2] + 100 < day[2]);
        CHECK(night[0] + night[1] + night[2] + 200 < day[0] + day[1] + day[2]);
    }

    TEST_CASE("the water: see-through looking down, a mirror of the sky at a flat angle, the sun's reflection bright")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());

        // A sun in the east, 30 degrees up, lighting the scene and shining in the sky; grass 3 units under the water.
        Wotah::DirectionalLight sun;
        sun.Direction = Wotah::MakeLightDirection(glm::radians(90.0f), glm::radians(30.0f));
        sun.Color     = {1.0f, 0.95f, 0.85f};
        OffscreenScene scene;
        scene.Lights.DirectionalLights = {sun};
        scene.Lights.SkySun            = sun;
        scene.HasSky                   = true;
        scene.HasGround                = true;
        scene.HasShapes                = false;
        scene.HasWater                 = true;
        OffscreenScene dry             = scene;
        dry.HasWater                   = false;

        // Straight down from 3 units above the water: the floor shows through, keeping a good part of its green, and
        // tinted: the water takes red far more than blue, as real water does.
        Wotah::Camera down;
        down.SetPosition({0.0f, 6.0f, 0.0f});
        down.SetTarget({0.05f, 0.0f, 0.0f});
        const std::array<int, 3> throughWater = GetCenterColor(DrawSceneOffscreen(gpu.Device, down, scene));
        const std::array<int, 3> floorOnly    = GetCenterColor(DrawSceneOffscreen(gpu.Device, down, dry));
        CAPTURE(throughWater);
        CAPTURE(floorOnly);
        CHECK(throughWater[1] * 3 > floorOnly[1]);
        CHECK(throughWater[0] * floorOnly[2] * 2 < throughWater[2] * floorOnly[0]);

        // Low over the water looking north, 3 degrees down: mostly the sky mirrored, as seen 3 degrees up (at least
        // half of it: ripples turned towards the camera mirror less there), its blue far above its red.
        Wotah::Camera flat;
        flat.SetPosition({0.0f, 3.3f, 0.0f});
        flat.SetTarget({0.0f, 3.3f - 0.0524f, -1.0f});
        Wotah::Camera skyward;
        skyward.SetPosition({0.0f, 3.3f, 0.0f});
        skyward.SetTarget({0.0f, 3.3f + 0.0524f, -1.0f});
        const std::array<int, 3> mirrored = GetCenterColor(DrawSceneOffscreen(gpu.Device, flat, scene));
        const std::array<int, 3> sky      = GetCenterColor(DrawSceneOffscreen(gpu.Device, skyward, scene));
        CAPTURE(mirrored);
        CAPTURE(sky);
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            CHECK(mirrored[channel] * 2 >= sky[channel]);
        }
        CHECK(mirrored[2] > mirrored[0] + 30);

        // Looking at the sun's mirror image in the water, 30 degrees down towards the east: a bright spot, far brighter
        // than the water 6 degrees beside it, though the water mirrors only a twentieth there.
        Wotah::Camera atReflection;
        atReflection.SetPosition({0.0f, 6.0f, 0.0f});
        atReflection.SetTarget(glm::vec3(0.0f, 6.0f, 0.0f) +
                               -Wotah::MakeLightDirection(glm::radians(90.0f), glm::radians(-30.0f)));
        Wotah::Camera besideReflection;
        besideReflection.SetPosition({0.0f, 6.0f, 0.0f});
        besideReflection.SetTarget(glm::vec3(0.0f, 6.0f, 0.0f) +
                                   -Wotah::MakeLightDirection(glm::radians(96.0f), glm::radians(-30.0f)));
        const std::array<int, 3> glint  = GetCenterColor(DrawSceneOffscreen(gpu.Device, atReflection, scene));
        const std::array<int, 3> beside = GetCenterColor(DrawSceneOffscreen(gpu.Device, besideReflection, scene));
        CAPTURE(glint);
        CAPTURE(beside);
        CHECK(glint[0] + glint[1] + glint[2] > beside[0] + beside[1] + beside[2] + 150);

        // Half a unit over the water looking north 1 degree down: the view meets the water 29 units off, over the
        // floor, but would meet the floor's depth far past its edge. Nothing lies under the water there, so nothing may
        // show through: the picture is the same whether the sky is drawn behind the water or not (it is mirrored in
        // both).
        Wotah::Camera pastFloor;
        pastFloor.SetPosition({0.0f, 3.5f, 0.0f});
        pastFloor.SetTarget({0.0f, 3.5f - 0.0175f, -1.0f});
        OffscreenScene noSky                = scene;
        noSky.HasSky                        = false;
        noSky.IsSkyInMirrorOnly             = true;
        const std::array<int, 3> withSky    = GetCenterColor(DrawSceneOffscreen(gpu.Device, pastFloor, scene));
        const std::array<int, 3> withoutSky = GetCenterColor(DrawSceneOffscreen(gpu.Device, pastFloor, noSky));
        CAPTURE(withSky);
        CAPTURE(withoutSky);
        CHECK(withSky == withoutSky);
    }

    TEST_CASE("the land's height on the GPU is the CPU's, to float rounding, over sea, beach, dunes and hills")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());

        // terrain_probe.frag writes the GPU's height at a grid of places 300 units across around the waterline, from
        // the sea floor through the beach into the dunes and the hills' start; every seventh is compared.
        for (const uint32_t seed : {1u, 7u})
        {
            CAPTURE(seed);
            OffscreenScene probe;
            probe.IsLandProbe                 = true;
            probe.Land                        = Wotah::TerrainShape{};
            probe.Land.Seed                   = seed;
            const std::vector<uint8_t> pixels = DrawSceneOffscreen(gpu.Device, Wotah::Camera{}, probe);

            float largest = 0.0f;
            for (uint32_t row = 0; row < OffscreenSize.height; row += 7)
            {
                for (uint32_t column = 0; column < OffscreenSize.width; column += 7)
                {
                    const glm::vec2 place =
                        ProbeCorner + glm::vec2(static_cast<float>(column), static_cast<float>(row)) * ProbeSpacing;
                    const float onCpu = Wotah::GetTerrainHeight(probe.Land, place);
                    largest           = std::max(largest, std::abs(ReadProbedHeight(pixels, column, row) - onCpu));
                }
            }
            CHECK(largest < 1.0e-3f); // a millimetre: float rounding, not a different formula
        }
    }

    TEST_CASE("the sea on the GPU is the CPU's, to float rounding: wind waves, surf, swash and wet sand, sea to land")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());

        // sea_probe.frag writes each number of the sea at a grid of places 300 units across around the waterline
        // (sea_waves.glsl with every wave kept, the floor's depth felt): the same as water/sea.cpp within a millimetre,
        // on high waves at a late hour (their phases a long way round). Where the swash's front or a run-up's edge
        // falls between the two's rounding, one says covered (or wet) and the other not: a few places may differ there.
        OffscreenScene probe;
        probe.Land = Wotah::TerrainShape{};
        probe.Sea  = Wotah::Sea{probe.Land.SeaLevel, Wotah::MakeSeaWaves(1.5f, {1.0f, -0.2f}), 86400.5, probe.Land};
        for (int part = 0; part < 6; ++part)
        {
            CAPTURE(part);
            probe.SeaProbePart                = part;
            const std::vector<uint8_t> pixels = DrawSceneOffscreen(gpu.Device, Wotah::Camera{}, probe);
            int                        apart  = 0;
            int                        count  = 0;
            float                      spread = 0.0f;
            for (uint32_t row = 0; row < OffscreenSize.height; row += 7)
            {
                for (uint32_t column = 0; column < OffscreenSize.width; column += 7)
                {
                    const glm::vec2 place =
                        ProbeCorner + glm::vec2(static_cast<float>(column), static_cast<float>(row)) * ProbeSpacing;
                    float onCpu = 0.0f;
                    if (part < 3)
                    {
                        onCpu = Wotah::GetWaveOffset(probe.Sea, place)[part];
                    }
                    else if (part == 3)
                    {
                        onCpu = Wotah::GetShoalShare(probe.Sea, place);
                    }
                    else if (part == 4)
                    {
                        const float sand = Wotah::GetTerrainHeight(probe.Land, place);
                        onCpu            = Wotah::GetSurfaceHeight(probe.Sea, place, sand, 0.0f) - probe.Sea.Level;
                    }
                    else
                    {
                        float trace = 0.0f;
                        onCpu       = Wotah::GetSandWetness(probe.Sea, place, trace);
                    }
                    apart += std::abs(ReadProbedHeight(pixels, column, row) - onCpu) > 1.0e-3f ? 1 : 0;
                    ++count;
                    spread = std::max(spread, std::abs(onCpu));
                }
            }
            CAPTURE(apart);
            CAPTURE(count);
            CAPTURE(spread);
            CHECK(apart * 500 < count); // under 0.2 %
            CHECK(spread > 0.1f);       // the numbers compared are real waves, not zeros
        }
    }

    TEST_CASE("a wave's crest hides the water behind it: every pixel of the sea is the nearest water along its view")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());
        OffscreenScene sea;
        sea.Lights    = MakeDayLight();
        sea.HasWater  = true;
        sea.HasShapes = false;
        sea.Sea       = Wotah::Sea{0.0f, Wotah::MakeSeaWaves(2.0f, {1.0f, -0.2f}), 10.0, std::nullopt};

        // Low over high waves, looking along them: farther slopes lie behind nearer crests on the picture. Each pixel
        // of the middle column below the horizon must hold the depth of the first water its view meets (walked along
        // the view on the CPU's surface, water/sea.hpp), out to 60 m, where the grid still follows the waves closely.
        // Once the water wrote no depth, so whichever water was drawn last showed, the crests cut by the slopes behind
        // them.
        const glm::vec3     eye{0.0f, 2.5f, 0.0f};
        const Wotah::Camera camera = MakeLandCamera(eye, eye + glm::vec3(-1.0f, -0.08f, 0.1f));
        std::vector<float>  viewDepths;
        (void)DrawSceneOffscreen(gpu.Device, camera, sea, &viewDepths);

        const glm::mat4 toWorld  = glm::inverse(camera.GetViewProjection());
        const glm::vec3 forward  = glm::normalize(camera.GetTarget() - eye);
        const uint32_t  column   = OffscreenSize.width / 2;
        int             compared = 0;
        int             wrong    = 0;
        for (uint32_t row = 0; row < OffscreenSize.height; ++row)
        {
            const glm::vec2 onPicture{(static_cast<float>(column) + 0.5f) / static_cast<float>(OffscreenSize.width),
                                      (static_cast<float>(row) + 0.5f) / static_cast<float>(OffscreenSize.height)};
            const glm::vec4 far = toWorld * glm::vec4(onPicture * 2.0f - 1.0f, 1.0f, 1.0f);
            const glm::vec3 ray = glm::normalize(glm::vec3(far) / far.w - eye);

            // The first place along the view under the surface, to a centimetre.
            float along = -1.0f;
            for (float step = 0.1f; step < 60.0f; step += 0.05f)
            {
                const glm::vec3 point = eye + ray * step;
                if (point.y < Wotah::FindWaterHeight(sea.Sea, point.x, point.z).value_or(0.0f))
                {
                    along = step;
                    break;
                }
            }
            if (along < 0.0f)
            {
                continue; // sky, or water farther than 60 m
            }
            const float expected = along * glm::dot(ray, forward);
            const float drawn    = viewDepths[static_cast<std::size_t>(row) * OffscreenSize.width + column];
            ++compared;
            wrong += std::abs(drawn - expected) > 0.05f * expected + 0.1f ? 1 : 0;
        }
        CAPTURE(compared);
        CAPTURE(wrong);
        CHECK(compared > 100);
        CHECK(wrong * 20 < compared); // all but where the grid's straight edges cut a crest's curve
    }

    TEST_CASE("the wavy sea reaches the horizon without cracks where its levels meet")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());
        OffscreenScene sea;
        sea.Lights    = MakeOldLight();
        sea.HasWater  = true;
        sea.HasShapes = false;
        sea.Sea       = Wotah::Sea{0.0f, Wotah::MakeSeaWaves(2.0f, {1.0f, 0.3f}), 3.0, std::nullopt};

        // As for the land: straight down from 400 units, four levels' edges cross the picture, and along the water
        // from 30 units up, its lower part is water to the horizon; no pixel may show the background through a crack
        // between levels that moved their water differently.
        for (const float x : {0.0f, 1.1f, 5.3f, -9.9f})
        {
            CAPTURE(x);
            const glm::vec3            above{x, 400.0f, 0.7f * x};
            const std::vector<uint8_t> down =
                DrawSceneOffscreen(gpu.Device, MakeLandCamera(above, above - glm::vec3(-0.01f, 400.0f, 0.0f)), sea);
            CHECK(CountPixels(down, 0x1A, 0x1A, 0x1A) == 0);

            const glm::vec3            low{x, 30.0f, -60.0f + x};
            const std::vector<uint8_t> along =
                DrawSceneOffscreen(gpu.Device, MakeLandCamera(low, low + glm::vec3(0.0f, -0.15f, -1.0f)), sea);
            const std::vector<uint8_t> lowerPart(along.begin() + (along.size() * 6 / 10 / 4) * 4, along.end());
            CHECK(CountPixels(lowerPart, 0x1A, 0x1A, 0x1A) == 0);
        }
    }

    TEST_CASE("the land reaches the horizon without cracks where its levels meet, wherever the viewer stands")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());
        OffscreenScene land;
        land.Lights    = MakeOldLight();
        land.HasLand   = true;
        land.HasShapes = false;
        land.Land      = Wotah::TerrainShape{};

        // Viewers at places that give every ring layout (a level's middle lands on one side of the viewer or the
        // other). Looking straight down from 400 units up, four levels' edges cross the picture; no pixel may show
        // the background through a crack. Looking along the land from 30 units up, the lower part of the picture is
        // land all the way to the horizon.
        for (const float x : {0.0f, 0.3f, 1.1f, 2.7f, 5.3f, -9.9f})
        {
            CAPTURE(x);
            const glm::vec3            above{x, 400.0f, 0.7f * x};
            const std::vector<uint8_t> down =
                DrawSceneOffscreen(gpu.Device, MakeLandCamera(above, above - glm::vec3(-0.01f, 400.0f, 0.0f)), land);
            CHECK(CountPixels(down, 0x1A, 0x1A, 0x1A) == 0);

            const glm::vec3            low{x, 30.0f, -60.0f + x};
            const std::vector<uint8_t> along =
                DrawSceneOffscreen(gpu.Device, MakeLandCamera(low, low + glm::vec3(0.0f, -0.15f, -1.0f)), land);
            const std::vector<uint8_t> lowerPart(along.begin() + (along.size() * 6 / 10 / 4) * 4, along.end());
            CHECK(CountPixels(lowerPart, 0x1A, 0x1A, 0x1A) == 0);
        }
    }

    TEST_CASE("the beach: sand by the water, green patches on the higher ground behind it, the sea in front")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());
        OffscreenScene beach;
        beach.Lights    = MakeDayLight();
        beach.HasSky    = true;
        beach.HasLand   = true;
        beach.HasWater  = true;
        beach.HasShapes = false;
        beach.Land      = Wotah::TerrainShape{};
        beach.Sea       = Wotah::Testing::MakeStillSea(beach.Land.SeaLevel);

        // Standing on the beach looking east (inland), across the dry sand to the dunes and hills: plenty of sand
        // (warm: red well above blue) and some green.
        const float                ground = Wotah::GetTerrainHeight(beach.Land, {8.0f, 0.0f});
        const glm::vec3            eye{8.0f, ground + 1.7f, 0.0f};
        const std::vector<uint8_t> inland =
            DrawSceneOffscreen(gpu.Device, MakeLandCamera(eye, eye + glm::vec3(1.0f, -0.08f, 0.0f)), beach);
        std::size_t sand  = 0;
        std::size_t green = 0;
        for (std::size_t index = 0; index + 3 < inland.size(); index += 4)
        {
            const int red   = inland[index];
            const int blue  = inland[index + 2];
            const int grass = inland[index + 1];
            sand += red > blue + 40 && red > 90 ? 1 : 0;
            green += grass > red + 4 && grass > blue + 10 ? 1 : 0;
        }
        const std::size_t pixelCount = inland.size() / 4;
        CAPTURE(sand);
        CAPTURE(green);
        CHECK(sand > pixelCount / 10);
        CHECK(green > pixelCount / 200);

        // Turning round to the sea (west): just below the horizon, some 18 units out, the water, bluer than red
        // (nearer, the picture's lower part is still the beach at one's feet).
        const std::vector<uint8_t> seaward =
            DrawSceneOffscreen(gpu.Device, MakeLandCamera(eye, eye + glm::vec3(-1.0f, -0.08f, 0.0f)), beach);
        const std::array<int, 3> water = GetPixel(seaward, OffscreenSize.width / 2, OffscreenSize.height * 55 / 100);
        CAPTURE(water);
        CHECK(water[2] > water[0]);
    }

    TEST_CASE("the sea over the beach: see-through at the waterline, its depth read from the scene, deep far out")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());
        OffscreenScene beach;
        beach.Lights       = MakeDayLight();
        beach.HasSky       = true;
        beach.HasLand      = true;
        beach.HasWater     = true;
        beach.HasShapes    = false;
        beach.Land         = Wotah::TerrainShape{};
        beach.Sea          = Wotah::Testing::MakeStillSea(beach.Land.SeaLevel);
        OffscreenScene dry = beach;
        dry.HasWater       = false;

        // Straight down where the water is some 30 centimetres deep: clear, the sand shows through, still warm (red
        // above blue) and at least half as bright as without the water.
        const glm::vec3          shallow{-5.0f, 5.0f, 0.0f};
        const Wotah::Camera      atShallow = MakeLandCamera(shallow, shallow - glm::vec3(-0.01f, 5.0f, 0.0f));
        const std::array<int, 3> wet       = GetCenterColor(DrawSceneOffscreen(gpu.Device, atShallow, beach));
        const std::array<int, 3> bare      = GetCenterColor(DrawSceneOffscreen(gpu.Device, atShallow, dry));
        CAPTURE(wet);
        CAPTURE(bare);
        CHECK(wet[0] > wet[2]);
        CHECK((wet[0] + wet[1] + wet[2]) * 2 > bare[0] + bare[1] + bare[2]);

        // At the very edge, a few millimetres deep, the line of foam where the swash ends: far brighter than the wet
        // sand there, and nearly white (its colors close together).
        const glm::vec3          edge{-0.1f, 5.0f, 0.0f};
        const Wotah::Camera      atEdge = MakeLandCamera(edge, edge - glm::vec3(-0.01f, 5.0f, 0.0f));
        const std::array<int, 3> foam   = GetCenterColor(DrawSceneOffscreen(gpu.Device, atEdge, beach));
        const std::array<int, 3> sand   = GetCenterColor(DrawSceneOffscreen(gpu.Device, atEdge, dry));
        CAPTURE(foam);
        CAPTURE(sand);
        CHECK(foam[0] + foam[1] + foam[2] > sand[0] + sand[1] + sand[2] + 60);
        CHECK(std::abs(foam[0] - foam[2]) < 40);

        // Far out, over some 14 units of water, straight down: the sea floor is gone, the water deep blue-green.
        const glm::vec3          deep{-400.0f, 5.0f, 0.0f};
        const Wotah::Camera      atDeep   = MakeLandCamera(deep, deep - glm::vec3(-0.01f, 5.0f, 0.0f));
        const std::array<int, 3> open     = GetCenterColor(DrawSceneOffscreen(gpu.Device, atDeep, beach));
        const std::array<int, 3> seaFloor = GetCenterColor(DrawSceneOffscreen(gpu.Device, atDeep, dry));
        CAPTURE(open);
        CAPTURE(seaFloor);
        CHECK(open[2] > open[0] + 20);
        CHECK(seaFloor[0] > seaFloor[2]); // the sand down there is warm: the water hid it
    }

    TEST_CASE("the sea mirrors what stands over it, and what stands in front of it never shows bent into it")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());

        // A sea a unit deep over the plain ground (as near the beach), a sun in the north 45 degrees up (shadows fall
        // towards the camera, away from where the test looks), the sky; cameras looking north over the water.
        Wotah::DirectionalLight sun;
        sun.Direction = Wotah::MakeLightDirection(0.0f, glm::radians(45.0f));
        sun.Intensity = 0.9f;
        OffscreenScene withShapes;
        withShapes.Lights.AmbientColor      = {0.2f, 0.24f, 0.32f};
        withShapes.Lights.DirectionalLights = {sun};
        withShapes.Lights.SkySun            = sun;
        withShapes.HasSky                   = true;
        withShapes.HasGround                = true;
        withShapes.HasWater                 = true;
        withShapes.HasShapes                = true;
        withShapes.Sea                      = Wotah::Testing::MakeStillSea(1.0f);
        OffscreenScene noShapes             = withShapes;
        noShapes.HasShapes                  = false;

        // The ball a unit over the water, off to the side of the cube, the camera 0.4 units over the water (a flat
        // view, where the water mirrors much): where the camera sees the ball's mirror image (its middle mirrored in
        // the surface), the water mirrors the dark blue ball instead of the bright sky: much less red.
        const float level                 = withShapes.Sea.Level;
        withShapes.BallPlace              = {6.0f, level + 1.0f, -6.0f};
        const Wotah::Camera      low      = MakeLandCamera({6.0f, level + 0.4f, 2.0f}, {6.0f, level - 0.5f, -6.0f});
        const glm::uvec2         ball     = FindPixelOf(low, {6.0f, level - 1.0f, -6.0f});
        const std::array<int, 3> mirrored = GetPixel(DrawSceneOffscreen(gpu.Device, low, withShapes), ball.x, ball.y);
        const std::array<int, 3> sky      = GetPixel(DrawSceneOffscreen(gpu.Device, low, noShapes), ball.x, ball.y);
        CAPTURE(mirrored);
        CAPTURE(sky);
        CHECK(mirrored[0] + 15 < sky[0]);

        // The orange cube just under the water (its top 5 centimetres down, so nothing of it is mirrored) 24 units
        // off, the camera 4 units over the water, the ball out of sight under the ground: the water seen well over the
        // cube on the picture (4 to 7 units behind it) shows nothing of its orange. The view bent into the water there
        // goes on away from the camera; the cube lies nearer and is not on it, though the bent view lands on it on the
        // picture (an orange ghost showed there once, a few rows high). Looking straight through instead, the water
        // there is a little deeper and less red than the bent view without the cube; never redder.
        withShapes.Sea                      = Wotah::Testing::MakeStillSea(1.05f);
        noShapes.Sea                        = withShapes.Sea;
        withShapes.BallPlace                = {40.0f, -10.0f, 40.0f};
        const float                sea      = withShapes.Sea.Level;
        const Wotah::Camera        high     = MakeLandCamera({0.0f, sea + 4.0f, 24.0f}, {0.0f, sea, 0.0f});
        const glm::uvec2           near     = FindPixelOf(high, {0.0f, sea, -3.9f});
        const glm::uvec2           far      = FindPixelOf(high, {0.0f, sea, -7.0f});
        const std::vector<uint8_t> withCube = DrawSceneOffscreen(gpu.Device, high, withShapes);
        const std::vector<uint8_t> water    = DrawSceneOffscreen(gpu.Device, high, noShapes);
        int                        reddest  = -255;
        for (uint32_t row = far.y; row < near.y; ++row)
        {
            const std::array<int, 3> with    = GetPixel(withCube, near.x, row);
            const std::array<int, 3> without = GetPixel(water, near.x, row);
            reddest                          = std::max(reddest, (with[0] - with[1]) - (without[0] - without[1]));
        }
        CAPTURE(far.y);
        CAPTURE(near.y);
        CHECK(near.y > far.y + 5);
        CHECK(reddest <= 5);
    }

    TEST_CASE("a ball lying in shallow water shows blue through it; under the water everything is tinted")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());
        OffscreenScene beach;
        beach.Lights    = MakeDayLight();
        beach.HasSky    = true;
        beach.HasLand   = true;
        beach.HasWater  = true;
        beach.HasShapes = true;
        beach.Land      = Wotah::TerrainShape{};
        beach.Sea       = Wotah::Testing::MakeStillSea(beach.Land.SeaLevel);

        // The ball resting on the sea floor some 16 units out, about a metre deep, nearly all of it under the water;
        // the camera a little over the water close to it. Its middle shows through the water as the blue ball, not as
        // the sand behind it: once the view bent into the water was guessed from a flat floor, and moved the ball's
        // half under the water a long way off, so it looked like glass with sand behind.
        const float     floor = Wotah::GetTerrainHeight(beach.Land, {-16.0f, 0.0f});
        const glm::vec3 middle{-16.0f, floor + BallRadius, 0.0f};
        beach.BallPlace                   = middle;
        const float              eyeFloor = Wotah::GetTerrainHeight(beach.Land, {-14.0f, 1.2f});
        const Wotah::Camera      near     = MakeLandCamera({-14.0f, eyeFloor + 1.0f, 1.2f}, {-16.0f, middle.y, 0.0f});
        const glm::uvec2         atBall   = FindPixelOf(near, middle);
        const std::array<int, 3> ball     = GetPixel(DrawSceneOffscreen(gpu.Device, near, beach), atBall.x, atBall.y);
        CAPTURE(middle.y);
        CAPTURE(ball);
        CHECK(middle.y + BallRadius < beach.Sea.Level + 0.2f); // all but its top is under the water
        CHECK(ball[2] > ball[0] + 80);
        CHECK(ball[2] > ball[1] + 40);

        // Under the water, a little over the floor, looking along it: the sand at the camera's feet keeps less of its
        // red than without water, and far off (where the view meets no ground) only water shows: blue-green.
        beach.HasShapes                     = false;
        OffscreenScene dry                  = beach;
        dry.HasWater                        = false;
        const glm::vec3            under    = {-16.0f, floor + 0.4f, 4.0f};
        const Wotah::Camera        inside   = MakeLandCamera(under, under + glm::vec3(0.0f, -0.1f, -1.0f));
        const std::vector<uint8_t> wet      = DrawSceneOffscreen(gpu.Device, inside, beach);
        const std::vector<uint8_t> bare     = DrawSceneOffscreen(gpu.Device, inside, dry);
        const std::array<int, 3>   sand     = GetPixel(wet, OffscreenSize.width / 2, OffscreenSize.height * 9 / 10);
        const std::array<int, 3>   sandBare = GetPixel(bare, OffscreenSize.width / 2, OffscreenSize.height * 9 / 10);
        const std::array<int, 3>   far      = GetPixel(wet, OffscreenSize.width / 2, OffscreenSize.height / 3);
        CAPTURE(sand);
        CAPTURE(sandBare);
        CAPTURE(far);
        CHECK(sand[0] * 10 < sandBare[0] * 9);
        CHECK(far[2] > far[0] + 30);
        CHECK(far[1] > far[0] + 30);
    }

    TEST_CASE("the sea grows darker steadily farther out, without bands; the ground shows grains near, calm far")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());
        OffscreenScene beach;
        beach.Lights    = MakeDayLight();
        beach.HasSky    = true;
        beach.HasLand   = true;
        beach.HasWater  = true;
        beach.HasShapes = false;
        beach.Land      = Wotah::TerrainShape{};
        beach.Sea       = Wotah::Testing::MakeStillSea(beach.Land.SeaLevel);

        // From 70 units up over the land, looking out across the sea from its shallows to where it is some 15 units
        // deep: down the picture the water's green changes smoothly, up and down by little more than its change from
        // one end to the other. Once the water's thickness was measured where the bent view lands on the picture,
        // which jumps: then the water showed bands, a rise and a sudden drop every few rows (three times as much up
        // and down).
        const Wotah::Camera        over = MakeLandCamera({120.0f, 70.0f, -120.0f}, {-20.0f, 0.0f, 0.0f});
        const std::vector<uint8_t> sea  = DrawSceneOffscreen(gpu.Device, over, beach);
        for (const uint32_t column : {300u, 400u, 500u})
        {
            CAPTURE(column);
            constexpr uint32_t FirstRow  = 80;
            constexpr uint32_t LastRow   = 280; // still over the water in these columns
            int                upAndDown = 0;
            for (uint32_t row = FirstRow; row + 2 <= LastRow; row += 2)
            {
                upAndDown += std::abs(GetPixel(sea, column, row + 2)[1] - GetPixel(sea, column, row)[1]);
            }
            const int overall = std::abs(GetPixel(sea, column, LastRow)[1] - GetPixel(sea, column, FirstRow)[1]);
            CAPTURE(upAndDown);
            CAPTURE(overall);
            CHECK(upAndDown - overall < 50);
        }

        // A metre over dry sand, straight down: its grains and ripples show (the colors vary from pixel to pixel).
        const float                sandHeight = Wotah::GetTerrainHeight(beach.Land, {15.0f, 0.0f});
        const glm::vec3            low{15.0f, sandHeight + 1.0f, 0.0f};
        const std::vector<uint8_t> sand =
            DrawSceneOffscreen(gpu.Device, MakeLandCamera(low, low - glm::vec3(-0.01f, 1.0f, 0.0f)), beach);
        int differs = 0;
        for (uint32_t row = 200; row < 400; ++row)
        {
            for (uint32_t column = 200; column < 400; ++column)
            {
                differs += std::abs(GetPixel(sand, column, row)[1] - GetPixel(sand, column + 1, row)[1]);
            }
        }
        CAPTURE(differs);
        CHECK(differs > 200 * 200 * 2); // more than 2 steps of green, on average, from one pixel to the next

        // Far along the beach, low over it: the sand there is calm, its fine detail averaged (mip levels, filtered at a
        // slant), not a flicker of grains and ripples from one pixel to the next.
        const glm::vec3            eye{15.0f, sandHeight + 1.7f, -60.0f};
        const std::vector<uint8_t> along =
            DrawSceneOffscreen(gpu.Device, MakeLandCamera(eye, eye + glm::vec3(0.0f, -0.03f, 1.0f)), beach);
        int            far    = 0;
        const uint32_t farRow = OffscreenSize.height / 2 + 30;
        for (uint32_t column = 100; column < 500; ++column)
        {
            far += std::abs(GetPixel(along, column, farRow)[1] - GetPixel(along, column + 1, farRow)[1]);
        }
        CAPTURE(far);
        CHECK(far < 400 * 3); // under 3 steps of green on average
    }

    TEST_CASE("the scene under the sky: the shapes in front, the sky behind, the draw order still free")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());
        OffscreenScene what;
        what.Lights                       = MakeOldLight();
        what.HasSky                       = true;
        const std::vector<uint8_t> pixels = DrawSceneOffscreen(gpu.Device, MakeStartCamera(), what);

        CHECK(CountPixels(pixels, 0xED, 0x76, 0x00) > 3000); // the cube's top, as without the sky
        CHECK(CountPixels(pixels, 0x1A, 0x1A, 0x1A) == 0);   // the sky covers all of the background

        // The top-left corner is sky above the horizon: blue.
        CHECK(pixels[2] > pixels[0] + 40);

        what.IsBallFirst = true;
        CHECK(DrawSceneOffscreen(gpu.Device, MakeStartCamera(), what) == pixels);
    }

    TEST_CASE("the ground: lit grass at height 0 on its area, nothing past its edge, hiding what lies under it")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());
        OffscreenScene ground;
        ground.Lights    = MakeOldLight();
        ground.HasSky    = true;
        ground.HasGround = true;
        ground.HasShapes = false;

        // Looking down from 2 units up onto the middle, and onto a spot just inside the edge (the area ends at 32):
        // grass, green above all.
        for (const float x : {0.0f, 31.0f})
        {
            CAPTURE(x);
            Wotah::Camera down;
            down.SetPosition({x, 2.0f, 0.0f});
            down.SetTarget({x + 0.05f, 0.0f, -1.0f});
            const std::array<int, 3> grass = GetCenterColor(DrawSceneOffscreen(gpu.Device, down, ground));
            CHECK(grass[1] > grass[0]);
            CHECK(grass[1] > grass[2]);
        }

        // Past the edge there is no ground: the sky's haze below the horizon, more blue than green.
        Wotah::Camera beyond;
        beyond.SetPosition({40.0f, 2.0f, 0.0f});
        beyond.SetTarget({40.05f, 0.0f, -1.0f});
        const std::array<int, 3> haze = GetCenterColor(DrawSceneOffscreen(gpu.Device, beyond, ground));
        CHECK(haze[2] > haze[1]);
        // A ball sunk into the ground is hidden by it completely: 2 or 20 units deep gives the very same picture, and
        // both differ from the ball lying on the ground. (The sky is blue too, so counting blue pixels would not tell.)
        OffscreenScene withShapes           = ground;
        withShapes.HasShapes                = true;
        withShapes.BallPlace                = {-1.3f, -2.0f, -0.9f};
        const std::vector<uint8_t> sunk     = DrawSceneOffscreen(gpu.Device, MakeStartCamera(), withShapes);
        withShapes.BallPlace                = {-1.3f, -20.0f, -0.9f};
        const std::vector<uint8_t> deep     = DrawSceneOffscreen(gpu.Device, MakeStartCamera(), withShapes);
        withShapes.BallPlace                = BallPosition;
        const std::vector<uint8_t> onGround = DrawSceneOffscreen(gpu.Device, MakeStartCamera(), withShapes);
        CHECK(sunk == deep);
        CHECK(sunk != onGround);
    }

    TEST_CASE("ground and shapes: the same picture whichever is drawn first")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());
        OffscreenScene what;
        what.Lights                            = MakeOldLight();
        what.HasSky                            = true;
        what.HasGround                         = true;
        const std::vector<uint8_t> groundFirst = DrawSceneOffscreen(gpu.Device, MakeStartCamera(), what);
        what.IsGroundLast                      = true;
        what.IsBallFirst                       = true;
        CHECK(DrawSceneOffscreen(gpu.Device, MakeStartCamera(), what) == groundFirst);
        CHECK(CountPixels(groundFirst, 0xED, 0x76, 0x00) > 3000); // the cube's top still shows
    }

    TEST_CASE("resizing: the window reports it once, the swapchain is made again at the new size, frames go on")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());
        Wotah::Swapchain swapchain;
        REQUIRE(swapchain.Create(gpu.Device, gpu.Window));
        Wotah::Renderer renderer;
        REQUIRE(renderer.Create(gpu.Device, swapchain));
        Scene scene;
        REQUIRE(scene.Create(gpu.Device, swapchain.GetFormat(), renderer.GetDepthFormat(), MakeOldLight(),
                             renderer.GetSceneBehind(), renderer.GetMirror()));
        CHECK_FALSE(gpu.Window.TakeResize());
        CHECK_FALSE(swapchain.IsOutOfDate());

        // The land and the sea too: the sea reads the renderer's depth image, which a resize makes again.
        OffscreenScene what;
        what.Lights    = MakeOldLight();
        what.HasSky    = true;
        what.HasGround = true;
        what.HasLand   = true;
        what.HasWater  = true;
        CHECK(DrawFramesLikeApp(gpu, swapchain, renderer, scene, what, 5));

        for (const Wotah::WindowSize size : {Wotah::WindowSize{800, 300}, Wotah::WindowSize{320, 640}})
        {
            CAPTURE(size.Width);
            CAPTURE(size.Height);
            gpu.Window.SetSize(size);
            gpu.Window.PollEvents();
            CHECK(gpu.Window.TakeResize());
            CHECK_FALSE(gpu.Window.TakeResize()); // answered once
            CHECK(gpu.Window.GetSize().Width == size.Width);
            CHECK(gpu.Window.GetSize().Height == size.Height);

            REQUIRE(renderer.RecreateSwapchain(gpu.Device, gpu.Window));
            CHECK(swapchain.GetExtent().width == size.Width);
            CHECK(swapchain.GetExtent().height == size.Height);
            CHECK_FALSE(swapchain.IsOutOfDate());
            CHECK(DrawFramesLikeApp(gpu, swapchain, renderer, scene, what, 5));
        }

        gpu.Window.SetSize({0, 100}); // refused and logged, the window keeps its size
        gpu.Window.PollEvents();
        CHECK_FALSE(gpu.Window.TakeResize());
    }

    TEST_CASE("drawing on its own thread: frames go on while the main thread resizes the window")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());
        Wotah::Swapchain swapchain;
        REQUIRE(swapchain.Create(gpu.Device, gpu.Window));
        Wotah::Renderer renderer;
        REQUIRE(renderer.Create(gpu.Device, swapchain));
        Scene scene;
        REQUIRE(scene.Create(gpu.Device, swapchain.GetFormat(), renderer.GetDepthFormat(), MakeOldLight(),
                             renderer.GetSceneBehind(), renderer.GetMirror()));

        // Sizes like a drag of the window's edge: many small steps, one right after the other.
        constexpr Wotah::WindowSize DragEnd{900, 520};
        constexpr uint32_t          DragStep = 20;

        std::atomic<int>  frames = 0;
        std::atomic<bool> failed = false;
        {
            std::jthread drawing(DrawUntilStopped, std::ref(gpu), std::ref(swapchain), std::ref(renderer),
                                 std::cref(scene), std::ref(frames), std::ref(failed));
            CHECK(HandleEventsUntilDrawn(gpu.Window, frames, 5));

            // One big jump, and frames keep coming at the new size.
            gpu.Window.SetSize({800, 300});
            gpu.Window.PollEvents();
            CHECK(HandleEventsUntilDrawn(gpu.Window, frames, frames + 5));

            for (uint32_t width = HiddenWindowSize.Width; width <= DragEnd.Width; width += DragStep)
            {
                gpu.Window.SetSize({width, DragEnd.Height});
                gpu.Window.PollEvents();
            }
            CHECK(HandleEventsUntilDrawn(gpu.Window, frames, frames + 5));
        } // stops the drawing thread and waits for it
        CHECK_FALSE(failed);

        // The drawing thread took the last size change and drew at the last size.
        CHECK_FALSE(gpu.Window.TakeResize());
        CHECK(swapchain.GetExtent().width == DragEnd.Width);
        CHECK(swapchain.GetExtent().height == DragEnd.Height);
    }

    TEST_CASE("the window between threads: another thread wakes its event wait, a window with an area needs no wait")
    {
        Wotah::Window window;
        REQUIRE(window.Create(HiddenWindowSize, "wotah tests (hidden)"));

        // Hidden but not minimized, with an area: drawing may go on at once, even with a stop that never comes.
        const std::stop_source neverStopped;
        CHECK(window.WaitForDrawableArea(neverStopped.get_token()));

        // Without Wake this would sleep forever: a hidden window gets no events of its own.
        {
            const std::jthread waker(WakeUntilStopped, std::ref(window));
            window.WaitEvents();
        }

        // No input happened; taking it leaves room, so the next frames do not allocate.
        std::vector<Wotah::InputEvent> events;
        window.TakeInputEvents(events);
        CHECK(events.empty());
        window.TakeInputEvents(events);
        CHECK(events.capacity() > 0);
    }

    TEST_CASE("shadows: the cube's shadow falls on the ground away from the light, right from its foot")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());
        const Wotah::Camera camera = MakeShadowCamera();

        // One light from the west at 45 degrees: the cube (1 high, x -0.5..0.5) throws its shadow 1 unit to the east.
        OffscreenScene scene;
        scene.Lights.AmbientColor      = glm::vec3(0.2f);
        scene.Lights.DirectionalLights = {MakeSideLight(1.0f, 0.8f)};
        scene.Lights.Shadows           = GroundShadows;
        scene.HasGround                = true;
        OffscreenScene noShadows       = scene;
        noShadows.Lights.Shadows       = {};

        const std::vector<uint8_t> shadowed   = DrawSceneOffscreen(gpu.Device, camera, scene);
        const std::vector<uint8_t> unshadowed = DrawSceneOffscreen(gpu.Device, camera, noShadows);

        // Points on the ground in tiles of the same checker shade (shaders/terrain.frag), away from the ball's shadow.
        const glm::vec3 inShadow{1.2f, 0.0f, 0.3f};    // east of the cube
        const glm::vec3 atTheFoot{0.6f, 0.0f, 0.3f};   // right next to the cube's east face
        const glm::vec3 inTheLight{-0.8f, 0.0f, 0.3f}; // west of the cube, facing the light

        // In the shadow only the ambient light is left: 0.2 against 0.2 + 0.8 * cos 45 = 0.77 in the light.
        const std::array<int, 3> dark = GetColorAt(shadowed, camera, inShadow);
        const std::array<int, 3> foot = GetColorAt(shadowed, camera, atTheFoot);
        const std::array<int, 3> lit  = GetColorAt(shadowed, camera, inTheLight);
        CHECK(dark[1] < lit[1] / 2);
        CHECK(foot[1] < lit[1] / 2); // the shadow starts at the cube, it does not float away from it

        // Where the light reaches, shadows change nothing; without shadows the same spot is lit.
        const std::array<int, 3> litWithout  = GetColorAt(unshadowed, camera, inTheLight);
        const std::array<int, 3> darkWithout = GetColorAt(unshadowed, camera, inShadow);
        CHECK(IsColorNear(lit, static_cast<float>(litWithout[0]) / 255.0f, static_cast<float>(litWithout[1]) / 255.0f,
                          static_cast<float>(litWithout[2]) / 255.0f));
        CHECK(darkWithout[1] > lit[1] * 3 / 4);
    }

    TEST_CASE("every light casts its own shadow, each taking away only its own light; a dark light casts none")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());
        const Wotah::Camera camera = MakeShadowCamera();

        // Two equal lights at 45 degrees, one from the west and one from the east: a shadow on each side of the cube.
        OffscreenScene scene;
        scene.Lights.AmbientColor         = glm::vec3(0.2f);
        scene.Lights.DirectionalLights    = {MakeSideLight(1.0f, 0.5f), MakeSideLight(-1.0f, 0.5f)};
        scene.Lights.Shadows              = GroundShadows;
        scene.HasGround                   = true;
        const std::vector<uint8_t> pixels = DrawSceneOffscreen(gpu.Device, camera, scene);

        const glm::vec3 east{1.2f, 0.0f, 0.3f};  // in the western light's shadow only
        const glm::vec3 west{-0.8f, 0.0f, 0.3f}; // in the eastern light's shadow only
        const glm::vec3 open{0.3f, 0.0f, 1.6f};  // south of the cube: both lights reach it

        // Lit by both: 0.2 + 2 * 0.5 * cos 45 = 0.91; by one: 0.2 + 0.35 = 0.55, so about 0.61 of it; by none: 0.22.
        const std::array<int, 3> eastColor = GetColorAt(pixels, camera, east);
        const std::array<int, 3> westColor = GetColorAt(pixels, camera, west);
        const std::array<int, 3> openColor = GetColorAt(pixels, camera, open);
        const float              eastShare = static_cast<float>(eastColor[1]) / static_cast<float>(openColor[1]);
        const float              westShare = static_cast<float>(westColor[1]) / static_cast<float>(openColor[1]);
        CHECK(eastShare > 0.5f);
        CHECK(eastShare < 0.72f);
        CHECK(westShare > 0.5f);
        CHECK(westShare < 0.72f);

        // The eastern light switched off: it casts no shadow, so the west side is as bright as the open ground.
        OffscreenScene oneDark                        = scene;
        oneDark.Lights.DirectionalLights[1].Intensity = 0.0f;
        const std::vector<uint8_t> darkPixels         = DrawSceneOffscreen(gpu.Device, camera, oneDark);
        const std::array<int, 3>   westNow            = GetColorAt(darkPixels, camera, west);
        const std::array<int, 3>   openNow            = GetColorAt(darkPixels, camera, open);
        CHECK(std::abs(westNow[1] - openNow[1]) <= 3);
    }

    TEST_CASE("shadows leave open ground clean: no stripes or speckles, even from a light low over the horizon")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());

        // The program's sun (45 degrees) and moon (about 17 degrees over the horizon): flat ground cannot shadow
        // itself, so with shadows on, the ground alone must look exactly as without them (shadow acne would not).
        Wotah::DirectionalLight sun;
        sun.Direction = Wotah::MakeLightDirection(glm::radians(90.0f), glm::radians(45.0f));
        sun.Intensity = 0.9f;
        Wotah::DirectionalLight moon;
        moon.Direction = {0.3f, -0.3f, -1.0f};
        moon.Intensity = 0.5f;
        OffscreenScene ground;
        ground.Lights.DirectionalLights = {sun, moon};
        ground.Lights.Shadows           = GroundShadows;
        ground.HasGround                = true;
        ground.HasShapes                = false;
        OffscreenScene noShadows        = ground;
        noShadows.Lights.Shadows        = {};

        for (const Wotah::Camera& camera : {MakeShadowCamera(), MakeStartCamera()})
        {
            const std::vector<uint8_t> with    = DrawSceneOffscreen(gpu.Device, camera, ground);
            const std::vector<uint8_t> without = DrawSceneOffscreen(gpu.Device, camera, noShadows);
            CHECK(CountDifferentPixels(with, without, 2) == 0);
        }
    }

    TEST_CASE("shadows leave lit shapes clean: no rings on the ball, no speckles on the cube, even in low light")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());

        // A light low over the horizon, grazing the ball's side and the cube's face: where a surface could shadow
        // itself most. Nothing stands between the light and the shapes, so with shadows on they must look as without.
        Wotah::DirectionalLight low;
        low.Direction = {1.0f, -0.25f, -0.3f};
        low.Intensity = 0.9f;
        OffscreenScene shapes;
        shapes.Lights.AmbientColor      = glm::vec3(0.15f);
        shapes.Lights.DirectionalLights = {low};
        shapes.Lights.Shadows           = GroundShadows;
        OffscreenScene noShadows        = shapes;
        noShadows.Lights.Shadows        = {};

        Wotah::Camera close;
        close.SetPosition({-2.6f, 1.2f, 0.6f});
        close.SetTarget({-0.7f, 0.4f, -0.4f});
        (void)close.SetClipDistances(0.1f, 100.0f); // valid values

        const std::size_t different = CountDifferentPixels(DrawSceneOffscreen(gpu.Device, close, shapes),
                                                           DrawSceneOffscreen(gpu.Device, close, noShadows), 2);
        CAPTURE(different);
        CHECK(different == 0);
    }

    TEST_CASE("shadow edges are sharp close to the camera")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());

        // The western light at 45 degrees: the cube's top edge (x 0.5, 1 high) ends its shadow at x 1.5.
        OffscreenScene scene;
        scene.Lights.AmbientColor      = glm::vec3(0.2f);
        scene.Lights.DirectionalLights = {MakeSideLight(1.0f, 0.8f)};
        scene.Lights.Shadows           = GroundShadows;
        scene.HasGround                = true;

        // Close above that edge: one picture pixel is about 3 mm of ground there.
        Wotah::Camera close;
        close.SetPosition({1.5f, 1.2f, 1.5f});
        close.SetTarget({1.5f, 0.0f, 0.3f});
        (void)close.SetClipDistances(0.1f, 100.0f); // valid values
        const std::vector<uint8_t> pixels = DrawSceneOffscreen(gpu.Device, close, scene);

        // Walk across the edge on the ground in 2.5 mm steps and measure how far the change from shadow to light
        // stretches (10 % to 90 % of the way); the checker tile is the same over the whole walk (x 1..2, z 0..1).
        constexpr float  Step  = 0.0025f;
        constexpr int    Steps = 160; // x 1.3 .. 1.7
        std::vector<int> green;
        for (int step = 0; step <= Steps; ++step)
        {
            green.push_back(GetColorAt(pixels, close, {1.3f + Step * static_cast<float>(step), 0.0f, 0.3f})[1]);
        }
        const int dark = *std::min_element(green.begin(), green.end());
        const int lit  = *std::max_element(green.begin(), green.end());
        REQUIRE(lit - dark > 40); // there is an edge to measure
        const int  low  = dark + (lit - dark) / 10;
        const int  high = dark + (lit - dark) * 9 / 10;
        const auto between =
            std::count_if(green.begin(), green.end(), [low, high](int value) { return value > low && value < high; });
        const float width = static_cast<float>(between) * Step;
        CAPTURE(width);
        CHECK(width < 0.01f); // at most 1 cm of blur
    }

    TEST_CASE("the lights hand out each shadow-casting light's view, and none without a shadow area or light")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());
        Wotah::Lighting lighting;
        REQUIRE(lighting.Create(gpu.Device));
        CHECK(lighting.GetShadowMaps().GetArrayView() != VK_NULL_HANDLE);
        CHECK(lighting.GetShadowMaps().GetSampler() != VK_NULL_HANDLE);
        CHECK(lighting.GetShadowMaps().GetFormat() == VK_FORMAT_D16_UNORM);

        CHECK(lighting.GetShadowMaps().GetLightCapacity() == 1); // room for one light to start with

        const Wotah::Camera camera = MakeStartCamera();
        Wotah::SceneLights  lights;
        lights.DirectionalLights = {MakeSideLight(1.0f, 1.0f), MakeSideLight(-1.0f, 0.0f)};
        REQUIRE(lighting.Update(lights, camera));
        CHECK_FALSE(lighting.FindShadowViewProjection(0, 0).has_value()); // no shadow area yet

        // A view per cascade, each the light's view of that cascade's stretch of the camera's view.
        lights.Shadows = GroundShadows;
        REQUIRE(lighting.Update(lights, camera));
        const std::optional<Wotah::CascadeEnds> ends =
            Wotah::MakeCascadeEnds(camera.GetNearDistance(), std::min(lights.ShadowReach, camera.GetFarDistance()));
        REQUIRE(ends.has_value());
        const std::optional<Wotah::ShadowArea> nearest =
            Wotah::MakeCascadeArea(camera, camera.GetNearDistance(), (*ends)[0]);
        REQUIRE(nearest.has_value());
        const std::optional<glm::mat4> first = lighting.FindShadowViewProjection(0, 0);
        REQUIRE(first.has_value());
        CHECK(*first ==
              *Wotah::MakeShadowViewProjection(lights.DirectionalLights[0].Direction, *nearest, GroundShadows));
        for (uint32_t cascade = 1; cascade < Wotah::ShadowCascadeCount; ++cascade)
        {
            CHECK(lighting.FindShadowViewProjection(0, cascade).has_value());
        }
        CHECK_FALSE(lighting.FindShadowViewProjection(0, Wotah::ShadowCascadeCount).has_value()); // no such cascade
        CHECK_FALSE(lighting.FindShadowViewProjection(1, 0).has_value());                         // a dark light
        CHECK_FALSE(lighting.FindShadowViewProjection(2, 0).has_value());                         // no such light
        CHECK_FALSE(lighting.FindShadowViewProjection(Wotah::MaxDirectionalLights, 0).has_value());

        // Every light casting shadows: the maps make room for all of them.
        lights.DirectionalLights.assign(Wotah::MaxDirectionalLights, MakeSideLight(1.0f, 0.5f));
        REQUIRE(lighting.Update(lights, camera));
        CHECK(lighting.GetShadowMaps().GetLightCapacity() == Wotah::MaxDirectionalLights);
        CHECK(lighting.FindShadowViewProjection(Wotah::MaxDirectionalLights - 1, Wotah::ShadowCascadeCount - 1)
                  .has_value());

        // A layer that does not exist is refused without recording anything.
        CHECK_FALSE(
            lighting.GetShadowMaps().BeginLayer(VK_NULL_HANDLE, Wotah::GetShadowLayer(Wotah::MaxDirectionalLights, 0)));
    }

    TEST_CASE("depth formats and images")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());
        const std::optional<VkFormat> depthFormat = Wotah::FindDepthFormat(gpu.Device);
        REQUIRE(depthFormat.has_value());

        {
            Wotah::Image neverCreated; // destroyed without Create: safe
        }
        Wotah::Image image;
        CHECK_FALSE(image.Create(gpu.Device, {0, 16}, *depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT));

        REQUIRE(image.Create(gpu.Device, {64, 32}, *depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT));
        CHECK(image.GetHandle() != VK_NULL_HANDLE);
        CHECK(image.GetView() != VK_NULL_HANDLE);
        CHECK(image.GetAspect() == VK_IMAGE_ASPECT_DEPTH_BIT);
        CHECK(image.GetExtent().width == 64);
        CHECK(image.GetExtent().height == 32);

        // Layers: one image, a view of all of them; no layers is refused.
        REQUIRE(image.Create(gpu.Device, {32, 32}, *depthFormat,
                             VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 3));
        CHECK(image.GetLayerCount() == 3);
        CHECK(image.GetView() != VK_NULL_HANDLE);
        CHECK_FALSE(image.Create(gpu.Device, {32, 32}, *depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 0));

        // A second Create replaces the first; a color format gets a color view.
        REQUIRE(image.Create(gpu.Device, {16, 16}, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT));
        CHECK(image.GetAspect() == VK_IMAGE_ASPECT_COLOR_BIT);
        CHECK(image.GetMipLevelCount() == 1);

        // Mip levels: as many as halving the larger side down to 1 allows, no more, and not none.
        CHECK(Wotah::GetMipLevelCountFor({1, 1}) == 1);
        CHECK(Wotah::GetMipLevelCountFor({5, 3}) == 3);
        CHECK(Wotah::GetMipLevelCountFor({64, 32}) == 7);
        CHECK(Wotah::GetMipLevelCountFor({1024, 1024}) == 11);
        REQUIRE(image.Create(gpu.Device, {64, 32}, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, 2, 7));
        CHECK(image.GetMipLevelCount() == 7);
        CHECK(image.GetLayerCount() == 2);
        CHECK_FALSE(image.Create(gpu.Device, {64, 32}, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, 1, 8));
        CHECK_FALSE(image.Create(gpu.Device, {64, 32}, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, 1, 0));

        image.Destroy();
        image.Destroy(); // twice is safe
        CHECK(image.GetHandle() == VK_NULL_HANDLE);
        CHECK(image.GetView() == VK_NULL_HANDLE);
        CHECK(image.GetLayerCount() == 0);
    }

    TEST_CASE("one-time commands: run once and waited for; submitting without a start is refused")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());
        {
            Wotah::OneTimeCommands neverBegun; // destroyed without Begin: safe
        }
        Wotah::OneTimeCommands commands;
        CHECK_FALSE(commands.SubmitAndWait()); // logged
        REQUIRE(commands.Begin(gpu.Device));
        CHECK(commands.Get() != VK_NULL_HANDLE);
        CHECK(commands.SubmitAndWait()); // nothing recorded: still runs
        CHECK(commands.Get() == VK_NULL_HANDLE);
    }

    TEST_CASE("the UI: the stats card over the scene, see-through, in a thin light border, with a shadow and its text")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());
        const std::optional<VkFormat> depthFormat = Wotah::FindDepthFormat(gpu.Device);
        REQUIRE(depthFormat.has_value());
        Wotah::UserInterface ui;
        REQUIRE(ui.Create());
        Wotah::UiRenderer uiRenderer;
        REQUIRE(uiRenderer.Create(gpu.Device, OffscreenColorFormat, *depthFormat));

        // ImGui measures a window that sizes itself in its first frame and shows it from the next (imgui.cpp, Begin),
        // so a few frames run before the one that is drawn.
        constexpr int               FirstFrames = 3;
        constexpr Wotah::WindowSize Size{OffscreenSize.width, OffscreenSize.height};
        for (int frame = 0; frame < FirstFrames; ++frame)
        {
            ShowUiFrame(ui, Size);
        }
        OffscreenScene what;
        what.HasShapes                    = false; // only the background behind the UI
        what.Ui                           = &uiRenderer;
        const std::vector<uint8_t> pixels = DrawSceneOffscreen(gpu.Device, Wotah::Camera{}, what);

        // The panel: the theme's slate with a little of the background (the program's, 0.1 gray) showing through; the
        // shadow beneath darkens that part by less than a level. The pixel lies in the window's left padding, beside
        // its rows (as in ui_tests.cpp).
        constexpr float    Background  = 0.1f;
        constexpr uint32_t PanelPixelX = 18;
        constexpr uint32_t PanelPixelY = 62;
        const ImVec4&      panel       = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        const float        throughPart = (1.0f - panel.w) * Background;
        CHECK(IsColorNear(GetPixel(pixels, PanelPixelX, PanelPixelY), panel.x * panel.w + throughPart,
                          panel.y * panel.w + throughPart, panel.z * panel.w + throughPart));

        // Along the panel's row, coming in from the background right of the stats window (a card's margin past its
        // edge, before the hints window), the first light pixel is its border: well lighter than the panel inside it
        // (55 against 22 when measured). Just outside it lies the shadow, darker than the background (18 to 21 against
        // 26).
        const ImGuiWindow* statsWindow = ImGui::FindWindowByName(Wotah::StatsWindowTitle);
        REQUIRE(statsWindow != nullptr);
        constexpr int BorderLift  = 25;
        constexpr int ShadowDepth = 4;
        const int     panelLevel  = GetPixel(pixels, PanelPixelX, PanelPixelY)[0];
        const int     background  = ToPixel(Background);
        uint32_t      edge        = static_cast<uint32_t>(statsWindow->Pos.x + statsWindow->Size.x + Wotah::CardMargin);
        while (edge > PanelPixelX && GetPixel(pixels, edge, PanelPixelY)[0] < panelLevel + BorderLift)
        {
            --edge;
        }
        REQUIRE(edge > PanelPixelX); // a border was found
        CHECK(GetPixel(pixels, edge + 2, PanelPixelY)[0] <= background - ShadowDepth);

        // The text, drawn from the font pictures the UI renderer sent to the GPU: light pixels in the stats window's
        // corner, in the hints' corner, and in the control bar's buttons at the bottom middle.
        constexpr PixelArea StatsCorner{0, 0, 250, 250};
        constexpr PixelArea HintsCorner{350, 0, 600, 250};
        constexpr PixelArea BottomMiddle{100, 500, 500, 600};
        CHECK(CountLightPixels(pixels, StatsCorner) > 100);
        CHECK(CountLightPixels(pixels, HintsCorner) > 100);
        CHECK(CountLightPixels(pixels, BottomMiddle) > 100);

        // Away from the window the background stays as it was.
        CHECK(IsColorNear(GetPixel(pixels, OffscreenSize.width - 10, OffscreenSize.height - 10), Background, Background,
                          Background));
    }

    TEST_CASE("the UI draws in the renderer's frames; its renderer needs the running UI and comes once")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());
        Wotah::Swapchain swapchain;
        REQUIRE(swapchain.Create(gpu.Device, gpu.Window));
        Wotah::Renderer renderer;
        REQUIRE(renderer.Create(gpu.Device, swapchain));
        const VkFormat colorFormat = swapchain.GetFormat();
        const VkFormat depthFormat = renderer.GetDepthFormat();
        {
            Wotah::UiRenderer withoutUi;
            CHECK_FALSE(withoutUi.Create(gpu.Device, colorFormat, depthFormat)); // the UI does not run yet
            CHECK_FALSE(withoutUi.Draw(VK_NULL_HANDLE)); // never created: refused before it records anything
        }

        Wotah::UserInterface ui;
        REQUIRE(ui.Create());
        Wotah::UiRenderer uiRenderer;
        REQUIRE(uiRenderer.Create(gpu.Device, colorFormat, depthFormat));
        {
            Wotah::UiRenderer second;
            CHECK_FALSE(second.Create(gpu.Device, colorFormat, depthFormat)); // one at a time
        }

        constexpr int FrameCount = 10;
        int           drawn      = 0;
        for (int frame = 0; frame < FrameCount; ++frame)
        {
            gpu.Window.PollEvents();
            const VkExtent2D        extent = swapchain.GetExtent();
            const Wotah::WindowSize size{extent.width, extent.height};
            ShowUiFrame(ui, size);
            const Wotah::FrameStart start = renderer.BeginFrame();
            REQUIRE(start != Wotah::FrameStart::Failed);
            if (start == Wotah::FrameStart::Skip)
            {
                continue;
            }
            renderer.BeginScene();
            REQUIRE(uiRenderer.Draw(renderer.GetCommandBuffer()));
            REQUIRE(renderer.EndFrame());
            ++drawn;
        }
        gpu.Device.WaitUntilIdle();
        CHECK(drawn == FrameCount);

        // The UI keeps running while its renderer is attached (logged), and stops once the renderer is gone.
        ui.Destroy();
        CHECK(ImGui::GetCurrentContext() != nullptr);
        uiRenderer.Destroy();
        uiRenderer.Destroy(); // twice is safe
        ui.Destroy();
        CHECK(ImGui::GetCurrentContext() == nullptr);
    }
}
