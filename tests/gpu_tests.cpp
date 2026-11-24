/**
 * @file gpu_tests.cpp
 * Tests that need a Vulkan 1.3 GPU: the window, device, swapchain, pipelines with every option, frames through the
 * renderer, depth images, and the example scene (cube and ball) drawn offscreen and read back. Every window stays
 * hidden, so running the tests never shows anything on screen. Skip them with --test-suite-exclude=gpu on machines
 * without a GPU.
 */
#include "platform/window.hpp"
#include "render/buffer.hpp"
#include "render/camera.hpp"
#include "render/device.hpp"
#include "render/image.hpp"
#include "render/mesh.hpp"
#include "render/mesh_data.hpp"
#include "render/pipeline.hpp"
#include "render/renderer.hpp"
#include "render/swapchain.hpp"
#include "test_utils.hpp"

#include <doctest/doctest.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

using Wotah::Testing::HiddenGpu;
using Wotah::Testing::HiddenWindowSize;

namespace
{
    /** The program's shaders (shaders/mesh.vert, cube.frag, ball.frag), as the program builds them. */
    constexpr uint32_t MeshVertexShader[] = {
#include "mesh.vert.inc"
    };
    constexpr uint32_t CubeFragmentShader[] = {
#include "cube.frag.inc"
    };
    constexpr uint32_t BallFragmentShader[] = {
#include "ball.frag.inc"
    };

    /** The test shaders (tests/shaders/water_test.*): vertex input, push constants and a uniform buffer. */
    constexpr uint32_t WaterTestVertexShader[] = {
#include "water_test.vert.inc"
    };
    constexpr uint32_t WaterTestFragmentShader[] = {
#include "water_test.frag.inc"
    };

    /** The program's scene (app.cpp): the ball's size and place, and how finely it is cut. */
    constexpr float     BallRadius = 0.5f;
    constexpr glm::vec3 BallPosition{-1.3f, 0.0f, -0.9f};
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

    /** A mesh pipeline's settings, as app.cpp builds them: depth tested and written, back faces culled. */
    Wotah::PipelineSettings MakeMeshSettings(std::span<const uint32_t> fragmentShader, VkFormat colorFormat,
                                             VkFormat depthFormat)
    {
        Wotah::PipelineSettings settings;
        settings.VertexShader     = MeshVertexShader;
        settings.FragmentShader   = fragmentShader;
        settings.ColorFormat      = colorFormat;
        settings.DepthFormat      = depthFormat;
        settings.VertexBindings   = Wotah::MeshVertexBindings;
        settings.VertexAttributes = Wotah::MeshVertexAttributes;
        settings.CullMode         = VK_CULL_MODE_BACK_BIT;
        settings.FrontFace        = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        settings.DepthTest        = true;
        settings.DepthWrite       = true;
        settings.PushConstants    = MeshRanges;
        return settings;
    }

    /** Records one mesh's draw, as app.cpp does: pipeline, camera and place, mesh. */
    void RecordMeshDraw(VkCommandBuffer commands, const Wotah::Pipeline& pipeline, const Wotah::Mesh& mesh,
                        const MeshPushConstants& pushConstants)
    {
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.GetHandle());
        vkCmdPushConstants(commands, pipeline.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushConstants),
                           &pushConstants);
        mesh.Draw(commands);
    }

    /** The program's scene on the GPU: both pipelines and both shapes. */
    struct Scene
    {
        Wotah::Pipeline CubePipeline;
        Wotah::Pipeline BallPipeline;
        Wotah::Mesh     Cube;
        Wotah::Mesh     Ball;

        [[nodiscard]] bool Create(const Wotah::Device& device, VkFormat colorFormat, VkFormat depthFormat)
        {
            return CubePipeline.Create(device, MakeMeshSettings(CubeFragmentShader, colorFormat, depthFormat)) &&
                   BallPipeline.Create(device, MakeMeshSettings(BallFragmentShader, colorFormat, depthFormat)) &&
                   Cube.Create(device, Wotah::MakeCube()) &&
                   Ball.Create(device, Wotah::MakeSphere(BallRadius, BallSegments, BallRings));
        }

        /** Records cube and ball as app.cpp does, in either order: the depth test must make the order not matter. */
        void Record(VkCommandBuffer commands, const glm::mat4& viewProjection, bool isBallFirst) const
        {
            MeshPushConstants cube;
            cube.ViewProjection = viewProjection;
            MeshPushConstants ball;
            ball.ViewProjection = viewProjection;
            ball.Model          = glm::translate(glm::mat4(1.0f), BallPosition);
            if (isBallFirst)
            {
                RecordMeshDraw(commands, BallPipeline, Ball, ball);
            }
            RecordMeshDraw(commands, CubePipeline, Cube, cube);
            if (!isBallFirst)
            {
                RecordMeshDraw(commands, BallPipeline, Ball, ball);
            }
        }
    };

    /** A whole-image layout change for the offscreen drawing. */
    void ChangeLayout(VkCommandBuffer commands, const Wotah::Image& image, VkImageLayout from, VkImageLayout to,
                      VkPipelineStageFlags sourceStage, VkPipelineStageFlags targetStage)
    {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
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
    std::vector<uint8_t> DrawSceneOffscreen(const Wotah::Device& device, const Wotah::Camera& camera, bool isBallFirst)
    {
        constexpr VkExtent2D Size{600, 600};
        constexpr VkFormat   ColorFormat = VK_FORMAT_R8G8B8A8_UNORM;

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
        REQUIRE(scene.Create(device, ColorFormat, *depthFormat));

        // ---- Record: clear, draw, copy to the buffer ----
        const VkDevice          handle = device.GetHandle();
        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.queueFamilyIndex = device.GetQueueFamily();
        VkCommandPool pool        = VK_NULL_HANDLE;
        REQUIRE(vkCreateCommandPool(handle, &poolInfo, nullptr, &pool) == VK_SUCCESS);
        VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        commandInfo.commandPool        = pool;
        commandInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandInfo.commandBufferCount = 1;
        VkCommandBuffer commands       = VK_NULL_HANDLE;
        REQUIRE(vkAllocateCommandBuffers(handle, &commandInfo, &commands) == VK_SUCCESS);

        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        REQUIRE(vkBeginCommandBuffer(commands, &beginInfo) == VK_SUCCESS);
        ChangeLayout(commands, color, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        ChangeLayout(commands, depth, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                     VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);

        VkRenderingAttachmentInfo colorAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        colorAttachment.imageView        = color.GetView();
        colorAttachment.imageLayout      = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp           = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp          = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.clearValue.color = {{0.1f, 0.1f, 0.1f, 1.0f}}; // the program's background
        VkRenderingAttachmentInfo depthAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        depthAttachment.imageView                     = depth.GetView();
        depthAttachment.imageLayout                   = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp                        = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp                       = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.clearValue.depthStencil.depth = 1.0f; // the far plane
        VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
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
        scene.Record(commands, camera.GetViewProjection(), isBallFirst);
        vkCmdEndRendering(commands);

        ChangeLayout(commands, color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent      = {Size.width, Size.height, 1};
        vkCmdCopyImageToBuffer(commands, color.GetHandle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.GetHandle(),
                               1, &region);
        VkMemoryBarrier toHost{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        toHost.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &toHost, 0,
                             nullptr, 0, nullptr);
        REQUIRE(vkEndCommandBuffer(commands) == VK_SUCCESS);

        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
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
        window.PollEvents(); // not minimized: returns right away
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

        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkFence           fence = VK_NULL_HANDLE;
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
        VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkSemaphore           imageAvailable = VK_NULL_HANDLE;
        REQUIRE(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailable) == VK_SUCCESS);
        CHECK(swapchain.AcquireNextImage(imageAvailable).has_value());
        const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        VkSubmitInfo               submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores    = &imageAvailable;
        submit.pWaitDstStageMask  = &waitStage;
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
        CHECK(cube.Create(gpu.Device, MakeMeshSettings(CubeFragmentShader, colorFormat, *depthFormat)));
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
        VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
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
        SUBCASE("settings that do not fit are refused")
        {
            Wotah::PipelineSettings noShaders;
            noShaders.ColorFormat                 = colorFormat;
            Wotah::PipelineSettings noDepthFormat = water;
            noDepthFormat.DepthFormat             = VK_FORMAT_UNDEFINED;
            Wotah::PipelineSettings noColorFormat = water;
            noColorFormat.ColorFormat             = VK_FORMAT_UNDEFINED;

            Wotah::Pipeline pipeline;
            CHECK_FALSE(pipeline.Create(gpu.Device, noShaders));
            CHECK_FALSE(pipeline.Create(gpu.Device, noDepthFormat));
            CHECK_FALSE(pipeline.Create(gpu.Device, noColorFormat));
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
        REQUIRE(scene.Create(gpu.Device, swapchain.GetFormat(), renderer.GetDepthFormat()));

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
            scene.Record(renderer.GetCommandBuffer(), camera.GetViewProjection(), frame % 2 == 0);
            REQUIRE(renderer.EndFrame());
            ++drawn;
        }
        gpu.Device.WaitUntilIdle();
        CHECK(drawn == FrameCount);

        renderer.Destroy();
        renderer.Destroy();
        CHECK(renderer.GetCommandBuffer() == VK_NULL_HANDLE);
    }

    TEST_CASE("the scene looks right: the lit cube with black edges in front of the blue ball")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());

        // The program's start view (app.cpp).
        Wotah::Camera camera;
        camera.SetPosition({2.0f, 1.6f, 2.6f});
        camera.SetTarget({0.0f, 0.0f, 0.0f});
        REQUIRE(camera.SetClipDistances(0.1f, 10.0f));
        const std::vector<uint8_t> pixels = DrawSceneOffscreen(gpu.Device, camera, false);

        // shaders/cube.frag: orange * (0.25 + 0.75 * light); the top gets the most light, the front the least.
        CHECK(CountPixels(pixels, 0xED, 0x76, 0x00) > 10000);  // top
        CHECK(CountPixels(pixels, 0x85, 0x42, 0x00) > 10000);  // right (+x)
        CHECK(CountPixels(pixels, 0x6B, 0x35, 0x00) > 10000);  // front (+z)
        CHECK(CountPixels(pixels, 0x00, 0x00, 0x00) > 500);    // the black edges
        CHECK(CountPixels(pixels, 0x1A, 0x1A, 0x1A) > 200000); // the background around them
        CHECK(CountBluePixels(pixels) > 2000);                 // the ball, partly behind the cube

        // The center of the picture is the cube, not the background.
        const std::size_t center = (300 * 600 + 300) * 4;
        CHECK(pixels[center] > 0x40);

        // The depth image decides what is in front: drawing the ball first gives the very same picture.
        CHECK(DrawSceneOffscreen(gpu.Device, camera, true) == pixels);
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

        // A second Create replaces the first; a color format gets a color view.
        REQUIRE(image.Create(gpu.Device, {16, 16}, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT));
        CHECK(image.GetAspect() == VK_IMAGE_ASPECT_COLOR_BIT);

        image.Destroy();
        image.Destroy(); // twice is safe
        CHECK(image.GetHandle() == VK_NULL_HANDLE);
        CHECK(image.GetView() == VK_NULL_HANDLE);
    }
}
