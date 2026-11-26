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
#include "render/pipeline.hpp"
#include "render/renderer.hpp"
#include "render/shadow_maps.hpp"
#include "render/sky.hpp"
#include "render/swapchain.hpp"
#include "render/ui_renderer.hpp"
#include "render/vulkan_utils.hpp"
#include "render/water.hpp"
#include "test_utils.hpp"
#include "ui/control_bar.hpp"
#include "ui/hints_window.hpp"
#include "ui/stats_window.hpp"
#include "ui/user_interface.hpp"

#include <doctest/doctest.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <imgui.h>
#include <imgui_internal.h> // ImGui::FindWindowByName, to see where a card ended up
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <atomic>
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
    /** The program's shaders (shaders/mesh.vert, cube.frag, ball.frag, ...), as the program builds them. */
    constexpr uint32_t MeshVertexShader[] = {
#include "mesh.vert.inc"
    };
    constexpr uint32_t CubeFragmentShader[] = {
#include "cube.frag.inc"
    };
    constexpr uint32_t TerrainFragmentShader[] = {
#include "terrain.frag.inc"
    };
    constexpr uint32_t BallFragmentShader[] = {
#include "ball.frag.inc"
    };
    constexpr uint32_t ShadowVertexShader[] = {
#include "shadow.vert.inc"
    };
    constexpr uint32_t WaterFragmentShader[] = {
#include "water.frag.inc"
    };

    /** The test shaders (tests/shaders/water_test.*): vertex input, push constants and a uniform buffer. */
    constexpr uint32_t WaterTestVertexShader[] = {
#include "water_test.vert.inc"
    };
    constexpr uint32_t WaterTestFragmentShader[] = {
#include "water_test.frag.inc"
    };

    /** The program's ground (app.cpp): 64 x 64 units, flat at height 0, a height point every half unit. */
    constexpr float    GroundSize  = 64.0f;
    constexpr uint32_t GroundCells = 128;

    /**
     * Water over that ground, 3 units above it, as deep as the program's (shaders/water.frag, FloorDepth): what the
     * water shader expects to see through.
     */
    constexpr Wotah::WaterArea TestWater{{GroundSize, GroundSize}, 3.0f};
    constexpr uint32_t         TestWaterCells = 64;

    /**
     * A scene of shapes to test drawing with, as the program had it before its water: a cube and a ball standing on the
     * ground, and how finely the ball is cut.
     */
    constexpr glm::vec3 CubePosition{0.0f, 0.5f, 0.0f};
    constexpr float     BallRadius = 0.5f;
    constexpr glm::vec3 BallPosition{-1.3f, BallRadius, -0.9f};
    constexpr uint32_t  BallSegments = 48;
    constexpr uint32_t  BallRings    = 24;

    /** What mesh.vert reads: camera and object matrix. */
    struct MeshPushConstants
    {
        glm::mat4 ViewProjection{1.0f};
        glm::mat4 Model{1.0f};
    };

    /** The one push constant range of every mesh pipeline. */
    constexpr std::array<VkPushConstantRange, 1> MeshRanges{
        {{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants)}}};

    /** A mesh pipeline's settings, as app.cpp builds them: depth tested and written, back faces culled, lights read. */
    Wotah::PipelineSettings MakeMeshSettings(std::span<const uint32_t> fragmentShader, VkFormat colorFormat,
                                             VkFormat depthFormat, std::span<const VkDescriptorSetLayout> setLayouts)
    {
        Wotah::PipelineSettings settings;
        settings.VertexShader         = MeshVertexShader;
        settings.FragmentShader       = fragmentShader;
        settings.ColorFormat          = colorFormat;
        settings.DepthFormat          = depthFormat;
        settings.VertexBindings       = Wotah::MeshVertexBindings;
        settings.VertexAttributes     = Wotah::MeshVertexAttributes;
        settings.CullMode             = VK_CULL_MODE_BACK_BIT;
        settings.FrontFace            = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        settings.DepthTest            = true;
        settings.DepthWrite           = true;
        settings.PushConstants        = MeshRanges;
        settings.DescriptorSetLayouts = setLayouts;
        return settings;
    }

    /** The water's pipeline settings, as app.cpp builds them: blended over the scene, both sides, depth not written. */
    Wotah::PipelineSettings MakeWaterSettings(VkFormat colorFormat, VkFormat depthFormat,
                                              std::span<const VkDescriptorSetLayout> setLayouts)
    {
        Wotah::PipelineSettings settings = MakeMeshSettings(WaterFragmentShader, colorFormat, depthFormat, setLayouts);
        settings.Blend                   = Wotah::BlendMode::Filtered;
        settings.CullMode                = VK_CULL_MODE_NONE;
        settings.DepthWrite              = false;
        return settings;
    }

    /** The shadow pipeline's settings, as app.cpp builds them: depth only, no culling, depths pushed back a little. */
    Wotah::PipelineSettings MakeShadowSettings(VkFormat shadowFormat)
    {
        Wotah::PipelineSettings settings;
        settings.VertexShader      = ShadowVertexShader;
        settings.DepthFormat       = shadowFormat;
        settings.VertexBindings    = Wotah::MeshVertexBindings;
        settings.VertexAttributes  = Wotah::MeshVertexAttributes;
        settings.CullMode          = VK_CULL_MODE_NONE;
        settings.DepthTest         = true;
        settings.DepthWrite        = true;
        settings.DepthBiasConstant = 2.0f;
        settings.DepthBiasSlope    = 2.0f;
        settings.PushConstants     = MeshRanges;
        return settings;
    }

    /** Where the program's shadow casters can stand (app.cpp): the whole ground and room above it. */
    constexpr Wotah::ShadowArea GroundShadows{glm::vec3(0.0f), 1.41421356f * GroundSize * 0.5f + 2.0f};

    /** Records one mesh's draw, as app.cpp does: pipeline, lights, camera and place, mesh. */
    void RecordMeshDraw(VkCommandBuffer commands, const Wotah::Pipeline& pipeline, const Wotah::Lighting& lighting,
                        const Wotah::Mesh& mesh, const MeshPushConstants& pushConstants)
    {
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.GetHandle());
        lighting.Bind(commands, pipeline.GetLayout());
        vkCmdPushConstants(commands, pipeline.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushConstants),
                           &pushConstants);
        mesh.Draw(commands);
    }

    /**
     * What an offscreen picture shows: its lights, which parts are in it, where the ball lies, the draw order, and the
     * UI over it all when Ui is set (not owned; its last ended UI frame is drawn).
     */
    struct OffscreenScene
    {
        Wotah::SceneLights Lights;
        bool               HasSky       = false;
        bool               HasGround    = false;
        bool               HasShapes    = true;
        bool               HasWater     = false;
        bool               IsBallFirst  = false;
        bool               IsGroundLast = false;
        glm::vec3          BallPlace    = BallPosition;
        float              SkySeconds   = 0.0f;
        Wotah::UiRenderer* Ui           = nullptr;

        /** The moon's disk for the sky (Sky::Draw, SkyLights::MoonDisk): none unless a test sets it. */
        Wotah::DirectionalLight Moon{{0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, 0.0f};
    };

    /** The offscreen pictures' size and color format (DrawSceneOffscreen): square, with room for the UI's cards. */
    constexpr VkExtent2D OffscreenSize{600, 600};
    constexpr VkFormat   OffscreenColorFormat = VK_FORMAT_R8G8B8A8_UNORM;

    /** The program's scene on the GPU: the lights, the sky, the ground, both shapes and their pipelines. */
    struct Scene
    {
        Wotah::Lighting Lighting;
        Wotah::Sky      Sky;
        Wotah::Pipeline TerrainPipeline;
        Wotah::Pipeline CubePipeline;
        Wotah::Pipeline BallPipeline;
        Wotah::Pipeline ShadowPipeline;
        Wotah::Pipeline WaterPipeline;
        Wotah::Mesh     Terrain;
        Wotah::Mesh     Cube;
        Wotah::Mesh     Ball;
        Wotah::Mesh     Water;

        [[nodiscard]] bool Create(const Wotah::Device& device, VkFormat colorFormat, VkFormat depthFormat,
                                  const Wotah::SceneLights& lights)
        {
            if (!Lighting.Create(device) || !Lighting.Update(lights, Wotah::Camera{}))
            {
                return false;
            }
            const std::array<VkDescriptorSetLayout, 1> setLayouts{Lighting.GetSetLayout()};
            return Sky.Create(device, colorFormat, depthFormat) &&
                   ShadowPipeline.Create(device, MakeShadowSettings(Lighting.GetShadowMaps().GetFormat())) &&
                   TerrainPipeline.Create(
                       device, MakeMeshSettings(TerrainFragmentShader, colorFormat, depthFormat, setLayouts)) &&
                   Terrain.Create(device, Wotah::MakeTerrainMesh(Wotah::MakeFlatHeightField(
                                              GroundSize, GroundSize, GroundCells, GroundCells))) &&
                   CubePipeline.Create(device,
                                       MakeMeshSettings(CubeFragmentShader, colorFormat, depthFormat, setLayouts)) &&
                   BallPipeline.Create(device,
                                       MakeMeshSettings(BallFragmentShader, colorFormat, depthFormat, setLayouts)) &&
                   Cube.Create(device, Wotah::MakeCube()) &&
                   Ball.Create(device, Wotah::MakeSphere(BallRadius, BallSegments, BallRings)) &&
                   WaterPipeline.Create(device, MakeWaterSettings(colorFormat, depthFormat, setLayouts)) &&
                   Water.Create(device, Wotah::MakeWaterMesh(TestWater, TestWaterCells));
        }

        /**
         * Records the sky (its sun from the first light, its moon from what.Moon), then ground and shapes as app.cpp
         * does, in any order the options pick (the depth test must make the order not matter), then the water.
         */
        void Record(VkCommandBuffer commands, const glm::mat4& viewProjection, const OffscreenScene& what) const
        {
            if (what.HasSky && !what.Lights.DirectionalLights.empty())
            {
                Sky.Draw(commands, viewProjection, what.Lights.DirectionalLights.front(), what.Moon, what.SkySeconds);
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
            if (what.HasWater)
            {
                MeshPushConstants pushConstants;
                pushConstants.ViewProjection = viewProjection;
                RecordMeshDraw(commands, WaterPipeline, Lighting, Water, pushConstants);
            }
        }

        /**
         * Records every shadow map of every light that casts shadows, as app.cpp does: ground and shapes, as far as the
         * options include them. Before the scene, outside its drawing.
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
                vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, ShadowPipeline.GetHandle());
                if (what.HasGround)
                {
                    RecordCaster(commands, *lightView, Terrain, glm::mat4(1.0f));
                }
                if (what.HasShapes)
                {
                    RecordCaster(commands, *lightView, Cube, glm::translate(glm::mat4(1.0f), CubePosition));
                    RecordCaster(commands, *lightView, Ball, glm::translate(glm::mat4(1.0f), what.BallPlace));
                }
                maps.EndLayer(commands, layer);
            }
        }

        /** Records one shadow caster into the light's map. */
        void RecordCaster(VkCommandBuffer commands, const glm::mat4& lightView, const Wotah::Mesh& mesh,
                          const glm::mat4& model) const
        {
            MeshPushConstants caster;
            caster.ViewProjection = lightView;
            caster.Model          = model;
            vkCmdPushConstants(commands, ShadowPipeline.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(caster),
                               &caster);
            mesh.Draw(commands);
        }

        /** Records the ground, as app.cpp does: the terrain where its heights say, no transform. */
        void RecordGround(VkCommandBuffer commands, const glm::mat4& viewProjection) const
        {
            MeshPushConstants ground;
            ground.ViewProjection = viewProjection;
            RecordMeshDraw(commands, TerrainPipeline, Lighting, Terrain, ground);
        }

        /** Records cube and ball, in the order the options pick. */
        void RecordShapes(VkCommandBuffer commands, const glm::mat4& viewProjection, const OffscreenScene& what) const
        {
            MeshPushConstants cube;
            cube.ViewProjection = viewProjection;
            cube.Model          = glm::translate(glm::mat4(1.0f), CubePosition);
            MeshPushConstants ball;
            ball.ViewProjection = viewProjection;
            ball.Model          = glm::translate(glm::mat4(1.0f), what.BallPlace);
            if (what.IsBallFirst)
            {
                RecordMeshDraw(commands, BallPipeline, Lighting, Ball, ball);
            }
            RecordMeshDraw(commands, CubePipeline, Lighting, Cube, cube);
            if (!what.IsBallFirst)
            {
                RecordMeshDraw(commands, BallPipeline, Lighting, Ball, ball);
            }
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

    /** A whole-image layout change for the offscreen drawing. */
    void ChangeLayout(VkCommandBuffer commands, const Wotah::Image& image, VkImageLayout from, VkImageLayout to,
                      VkPipelineStageFlags sourceStage, VkPipelineStageFlags targetStage)
    {
        VkImageMemoryBarrier barrier =
            Wotah::MakeVulkanInfo<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
        barrier.srcAccessMask       = VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask       = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.oldLayout           = from;
        barrier.newLayout           = to;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = image.GetHandle();
        barrier.subresourceRange    = {image.GetAspect(), 0, 1, 0, 1};
        vkCmdPipelineBarrier(commands, sourceStage, targetStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    /**
     * Draws the program's scene (cube and ball, with a depth image) through the camera into a hidden 600 x 600 RGBA8
     * image, exactly with the program's settings, and returns its pixels (4 bytes each, rows top to bottom).
     */
    std::vector<uint8_t> DrawSceneOffscreen(const Wotah::Device& device, const Wotah::Camera& camera,
                                            const OffscreenScene& what)
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
        REQUIRE(depth.Create(device, Size, *depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT));

        constexpr VkDeviceSize ByteCount = VkDeviceSize{Size.width} * Size.height * 4;
        Wotah::Buffer          readback;
        REQUIRE(readback.Create(device, ByteCount, VK_BUFFER_USAGE_TRANSFER_DST_BIT, Wotah::BufferMemory::GpuToCpu));

        Scene scene;
        REQUIRE(scene.Create(device, ColorFormat, *depthFormat, what.Lights));
        REQUIRE(scene.Lighting.Update(what.Lights, camera)); // the shadow cascades follow the camera

        // ---- Record: clear, draw, copy to the buffer ----
        const VkDevice          handle = device.GetHandle();
        VkCommandPoolCreateInfo poolInfo =
            Wotah::MakeVulkanInfo<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
        poolInfo.queueFamilyIndex = device.GetQueueFamily();
        VkCommandPool pool        = VK_NULL_HANDLE;
        REQUIRE(vkCreateCommandPool(handle, &poolInfo, nullptr, &pool) == VK_SUCCESS);
        VkCommandBufferAllocateInfo commandInfo =
            Wotah::MakeVulkanInfo<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
        commandInfo.commandPool        = pool;
        commandInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandInfo.commandBufferCount = 1;
        VkCommandBuffer commands       = VK_NULL_HANDLE;
        REQUIRE(vkAllocateCommandBuffers(handle, &commandInfo, &commands) == VK_SUCCESS);

        VkCommandBufferBeginInfo beginInfo =
            Wotah::MakeVulkanInfo<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
        REQUIRE(vkBeginCommandBuffer(commands, &beginInfo) == VK_SUCCESS);
        scene.RecordShadows(commands, what); // the lights' shadow maps first, as the program does
        ChangeLayout(commands, color, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        ChangeLayout(commands, depth, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                     VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);

        VkRenderingAttachmentInfo colorAttachment =
            Wotah::MakeVulkanInfo<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
        colorAttachment.imageView        = color.GetView();
        colorAttachment.imageLayout      = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp           = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp          = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.clearValue.color = {{0.1f, 0.1f, 0.1f, 1.0f}}; // the program's background
        VkRenderingAttachmentInfo depthAttachment =
            Wotah::MakeVulkanInfo<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
        depthAttachment.imageView                     = depth.GetView();
        depthAttachment.imageLayout                   = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp                        = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp                       = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.clearValue.depthStencil.depth = 1.0f; // the far plane
        VkRenderingInfo rendering      = Wotah::MakeVulkanInfo<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
        rendering.renderArea           = {{0, 0}, Size};
        rendering.layerCount           = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments    = &colorAttachment;
        rendering.pDepthAttachment     = &depthAttachment;
        vkCmdBeginRendering(commands, &rendering);
        const VkViewport viewport{0.0f, 0.0f, static_cast<float>(Size.width), static_cast<float>(Size.height),
                                  0.0f, 1.0f};
        const VkRect2D   scissor{{0, 0}, Size};
        vkCmdSetViewport(commands, 0, 1, &viewport);
        vkCmdSetScissor(commands, 0, 1, &scissor);
        scene.Record(commands, camera.GetViewProjection(), what);
        if (what.Ui != nullptr)
        {
            REQUIRE(what.Ui->Draw(commands)); // last, over everything, as app.cpp does
        }
        vkCmdEndRendering(commands);

        ChangeLayout(commands, color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent      = {Size.width, Size.height, 1};
        vkCmdCopyImageToBuffer(commands, color.GetHandle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.GetHandle(),
                               1, &region);
        VkMemoryBarrier toHost = Wotah::MakeVulkanInfo<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
        toHost.srcAccessMask   = VK_ACCESS_TRANSFER_WRITE_BIT;
        toHost.dstAccessMask   = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &toHost, 0,
                             nullptr, 0, nullptr);
        REQUIRE(vkEndCommandBuffer(commands) == VK_SUCCESS);

        VkSubmitInfo submit       = Wotah::MakeVulkanInfo<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
        submit.commandBufferCount = 1;
        submit.pCommandBuffers    = &commands;
        REQUIRE(vkQueueSubmit(device.GetQueue(), 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS);
        device.WaitUntilIdle();

        std::vector<uint8_t> pixels(static_cast<std::size_t>(ByteCount));
        REQUIRE(readback.Read(std::as_writable_bytes(std::span(pixels))));
        vkDestroyCommandPool(handle, pool, nullptr);
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
            Wotah::Camera    camera;
            const VkExtent2D extent = swapchain.GetExtent();
            (void)camera.SetAspectRatio(static_cast<float>(extent.width) / static_cast<float>(extent.height));
            scene.RecordShadows(renderer.GetCommandBuffer(), what);
            renderer.BeginScene();
            scene.Record(renderer.GetCommandBuffer(), camera.GetViewProjection(), what);
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
        OffscreenScene what;
        what.Lights    = MakeOldLight();
        what.HasSky    = true;
        what.HasGround = true;
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
            Wotah::Camera    camera;
            const VkExtent2D extent = swapchain.GetExtent();
            (void)camera.SetAspectRatio(static_cast<float>(extent.width) / static_cast<float>(extent.height));
            if (start == Wotah::FrameStart::Ready)
            {
                scene.RecordShadows(renderer.GetCommandBuffer(), what);
                renderer.BeginScene();
                scene.Record(renderer.GetCommandBuffer(), camera.GetViewProjection(), what);
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
        (void)Wotah::ShowControlBar(false); // nothing is clicked in these tests
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
        CHECK(cube.Create(gpu.Device, MakeMeshSettings(CubeFragmentShader, colorFormat, *depthFormat, lightLayouts)));
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
        SUBCASE("blending through a filter the shader writes, both sides drawn: the water's pipeline")
        {
            Wotah::Pipeline pipeline;
            CHECK(pipeline.Create(gpu.Device, MakeWaterSettings(colorFormat, *depthFormat, lightLayouts)));
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
            CHECK(shadows.Create(gpu.Device, MakeShadowSettings(lighting.GetShadowMaps().GetFormat())));
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
        REQUIRE(scene.Create(gpu.Device, swapchain.GetFormat(), renderer.GetDepthFormat(), MakeOldLight()));

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
        ground.HasGround = true;
        ground.HasShapes = false;

        // From one edge low over the ground towards the far edge, 60 units away: half grass, half haze.
        Wotah::Camera far;
        far.SetPosition({-30.0f, 0.5f, 0.0f});
        far.SetTarget({30.0f, 0.0f, 0.0f});
        const std::array<int, 3> day = GetCenterColor(DrawSceneOffscreen(gpu.Device, far, ground));
        CHECK(day[2] > 120);

        // The same grass in a night's haze: far darker, the blue most of all (the day haze is the bluest part).
        ground.Lights.HazeColor        = {0.02f, 0.028f, 0.055f};
        const std::array<int, 3> night = GetCenterColor(DrawSceneOffscreen(gpu.Device, far, ground));
        CHECK(night[2] + 80 < day[2]);
        CHECK(night[0] + night[1] + night[2] + 150 < day[0] + day[1] + day[2]);
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

        // Low over the water looking north, 3 degrees down: mostly the sky mirrored, as seen 3 degrees up.
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
            CHECK(mirrored[channel] * 10 >= sky[channel] * 7);
        }
        CHECK(mirrored[2] > mirrored[0] + 40);

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
        // show through: the picture is the same whether the sky is drawn behind the water or not.
        Wotah::Camera pastFloor;
        pastFloor.SetPosition({0.0f, 3.5f, 0.0f});
        pastFloor.SetTarget({0.0f, 3.5f - 0.0175f, -1.0f});
        OffscreenScene noSky                = scene;
        noSky.HasSky                        = false;
        const std::array<int, 3> withSky    = GetCenterColor(DrawSceneOffscreen(gpu.Device, pastFloor, scene));
        const std::array<int, 3> withoutSky = GetCenterColor(DrawSceneOffscreen(gpu.Device, pastFloor, noSky));
        CAPTURE(withSky);
        CAPTURE(withoutSky);
        CHECK(withSky == withoutSky);
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
        REQUIRE(scene.Create(gpu.Device, swapchain.GetFormat(), renderer.GetDepthFormat(), MakeOldLight()));
        CHECK_FALSE(gpu.Window.TakeResize());
        CHECK_FALSE(swapchain.IsOutOfDate());

        OffscreenScene what;
        what.Lights    = MakeOldLight();
        what.HasSky    = true;
        what.HasGround = true;
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
        REQUIRE(scene.Create(gpu.Device, swapchain.GetFormat(), renderer.GetDepthFormat(), MakeOldLight()));

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
