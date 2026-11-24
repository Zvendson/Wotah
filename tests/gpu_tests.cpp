/**
 * @file gpu_tests.cpp
 * Tests that need a Vulkan 1.3 GPU: the window, device, swapchain, pipelines with every option, frames through the
 * renderer, and the example cube drawn offscreen and read back. Every window stays hidden, so running the tests never
 * shows anything on screen. Skip them with --test-suite-exclude=gpu on machines without a GPU.
 */
#include "platform/window.hpp"
#include "render/buffer.hpp"
#include "render/camera.hpp"
#include "render/device.hpp"
#include "render/mesh.hpp"
#include "render/mesh_data.hpp"
#include "render/pipeline.hpp"
#include "render/renderer.hpp"
#include "render/swapchain.hpp"
#include "test_utils.hpp"

#include <doctest/doctest.h>
#include <glm/mat4x4.hpp>
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
    /** The example cube's shaders (shaders/cube.*), as the program builds them. */
    constexpr uint32_t CubeVertexShader[] = {
#include "cube.vert.inc"
    };
    constexpr uint32_t CubeFragmentShader[] = {
#include "cube.frag.inc"
    };

    /** The test shaders (tests/shaders/water_test.*): vertex input, push constants and a uniform buffer. */
    constexpr uint32_t WaterTestVertexShader[] = {
#include "water_test.vert.inc"
    };
    constexpr uint32_t WaterTestFragmentShader[] = {
#include "water_test.frag.inc"
    };

    /** What the cube's vertex shader reads (shaders/cube.vert). */
    struct CubePushConstants
    {
        glm::mat4 ViewProjection{1.0f};
    };

    /** The cube pipeline's settings, as app.cpp builds them. */
    Wotah::PipelineSettings MakeCubeSettings(VkFormat colorFormat, std::span<const VkPushConstantRange> ranges)
    {
        Wotah::PipelineSettings settings;
        settings.VertexShader     = CubeVertexShader;
        settings.FragmentShader   = CubeFragmentShader;
        settings.ColorFormat      = colorFormat;
        settings.VertexBindings   = Wotah::MeshVertexBindings;
        settings.VertexAttributes = Wotah::MeshVertexAttributes;
        settings.CullMode         = VK_CULL_MODE_BACK_BIT;
        settings.FrontFace        = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        settings.PushConstants    = ranges;
        return settings;
    }

    /** The cube's one push constant range. */
    constexpr std::array<VkPushConstantRange, 1> CubeRanges{
        {{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(CubePushConstants)}}};

    /** Records the cube's draw, as app.cpp does: pipeline, camera, mesh. */
    void RecordCubeDraw(VkCommandBuffer commands, const Wotah::Pipeline& pipeline, const Wotah::Mesh& cube,
                        const CubePushConstants& pushConstants)
    {
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.GetHandle());
        vkCmdPushConstants(commands, pipeline.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushConstants),
                           &pushConstants);
        cube.Draw(commands);
    }

    /** A memory type allowed by the mask that has every wanted property; fails the test when there is none. */
    uint32_t FindMemoryType(VkPhysicalDevice gpu, uint32_t allowedTypes, VkMemoryPropertyFlags wanted)
    {
        VkPhysicalDeviceMemoryProperties properties;
        vkGetPhysicalDeviceMemoryProperties(gpu, &properties);
        for (uint32_t index = 0; index < properties.memoryTypeCount; ++index)
        {
            if ((allowedTypes & (1u << index)) != 0 && (properties.memoryTypes[index].propertyFlags & wanted) == wanted)
            {
                return index;
            }
        }
        FAIL("no fitting memory type");
        return 0;
    }

    /**
     * Draws the example cube through the camera into a hidden 600 x 600 RGBA8 image, exactly with the program's
     * pipeline settings, and returns its pixels (4 bytes each, rows top to bottom).
     */
    std::vector<uint8_t> DrawCubeOffscreen(const Wotah::Device& device, const Wotah::Camera& camera)
    {
        constexpr uint32_t Size   = 600;
        constexpr VkFormat Format = VK_FORMAT_R8G8B8A8_UNORM;
        const VkDevice     handle = device.GetHandle();

        // ---- The image to draw into, and a buffer the CPU can read it from ----
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType   = VK_IMAGE_TYPE_2D;
        imageInfo.format      = Format;
        imageInfo.extent      = {Size, Size, 1};
        imageInfo.mipLevels   = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples     = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.usage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        VkImage image         = VK_NULL_HANDLE;
        REQUIRE(vkCreateImage(handle, &imageInfo, nullptr, &image) == VK_SUCCESS);

        VkMemoryRequirements imageNeeds;
        vkGetImageMemoryRequirements(handle, image, &imageNeeds);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = imageNeeds.size;
        allocation.memoryTypeIndex =
            FindMemoryType(device.GetGpu(), imageNeeds.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VkDeviceMemory imageMemory = VK_NULL_HANDLE;
        REQUIRE(vkAllocateMemory(handle, &allocation, nullptr, &imageMemory) == VK_SUCCESS);
        REQUIRE(vkBindImageMemory(handle, image, imageMemory, 0) == VK_SUCCESS);

        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image            = image;
        viewInfo.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format           = Format;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView view          = VK_NULL_HANDLE;
        REQUIRE(vkCreateImageView(handle, &viewInfo, nullptr, &view) == VK_SUCCESS);

        constexpr VkDeviceSize ByteCount = VkDeviceSize{Size} * Size * 4;
        Wotah::Buffer          readback;
        REQUIRE(readback.Create(device, ByteCount, VK_BUFFER_USAGE_TRANSFER_DST_BIT, Wotah::BufferMemory::GpuToCpu));

        // ---- The program's cube pipeline and buffers ----
        Wotah::Pipeline pipeline;
        REQUIRE(pipeline.Create(device, MakeCubeSettings(Format, CubeRanges)));
        Wotah::Mesh cube;
        REQUIRE(cube.Create(device, Wotah::MakeCube()));
        CubePushConstants pushConstants;
        pushConstants.ViewProjection = camera.GetViewProjection();

        // ---- Record: clear, draw, copy to the buffer ----
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

        VkImageMemoryBarrier toDraw{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toDraw.dstAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        toDraw.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        toDraw.newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        toDraw.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDraw.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDraw.image               = image;
        toDraw.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &toDraw);

        VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        color.imageView        = view;
        color.imageLayout      = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp           = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp          = VK_ATTACHMENT_STORE_OP_STORE;
        color.clearValue.color = {{0.1f, 0.1f, 0.1f, 1.0f}}; // the program's background
        VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
        rendering.renderArea           = {{0, 0}, {Size, Size}};
        rendering.layerCount           = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments    = &color;
        vkCmdBeginRendering(commands, &rendering);
        const VkViewport viewport{0.0f, 0.0f, static_cast<float>(Size), static_cast<float>(Size), 0.0f, 1.0f};
        const VkRect2D   scissor{{0, 0}, {Size, Size}};
        vkCmdSetViewport(commands, 0, 1, &viewport);
        vkCmdSetScissor(commands, 0, 1, &scissor);
        RecordCubeDraw(commands, pipeline, cube, pushConstants);
        vkCmdEndRendering(commands);

        VkImageMemoryBarrier toCopy = toDraw;
        toCopy.srcAccessMask        = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        toCopy.dstAccessMask        = VK_ACCESS_TRANSFER_READ_BIT;
        toCopy.oldLayout            = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        toCopy.newLayout            = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &toCopy);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent      = {Size, Size, 1};
        vkCmdCopyImageToBuffer(commands, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.GetHandle(), 1, &region);

        VkBufferMemoryBarrier toHost{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        toHost.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
        toHost.dstAccessMask       = VK_ACCESS_HOST_READ_BIT;
        toHost.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toHost.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toHost.buffer              = readback.GetHandle();
        toHost.size                = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1,
                             &toHost, 0, nullptr);
        REQUIRE(vkEndCommandBuffer(commands) == VK_SUCCESS);

        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers    = &commands;
        REQUIRE(vkQueueSubmit(device.GetQueue(), 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS);
        device.WaitUntilIdle();

        // ---- Read the pixels, free everything ----
        std::vector<uint8_t> pixels(static_cast<std::size_t>(ByteCount));
        REQUIRE(readback.Read(std::as_writable_bytes(std::span(pixels))));

        vkDestroyCommandPool(handle, pool, nullptr);
        vkDestroyImageView(handle, view, nullptr);
        vkDestroyImage(handle, image, nullptr);
        vkFreeMemory(handle, imageMemory, nullptr);
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
        const VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;

        Wotah::Pipeline cube;
        CHECK(cube.Create(gpu.Device, MakeCubeSettings(colorFormat, CubeRanges)));
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
        water.DepthFormat          = VK_FORMAT_D32_SFLOAT;
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

    TEST_CASE("the renderer draws frames of the cube")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());
        Wotah::Swapchain swapchain;
        REQUIRE(swapchain.Create(gpu.Device, gpu.Window));
        Wotah::Pipeline pipeline;
        REQUIRE(pipeline.Create(gpu.Device, MakeCubeSettings(swapchain.GetFormat(), CubeRanges)));
        Wotah::Mesh cube;
        REQUIRE(cube.Create(gpu.Device, Wotah::MakeCube()));
        {
            Wotah::Renderer neverCreated;
        }
        Wotah::Renderer renderer;
        REQUIRE(renderer.Create(gpu.Device, swapchain));

        Wotah::Camera camera;
        (void)camera.SetAspectRatio(static_cast<float>(HiddenWindowSize.Width) /
                                    static_cast<float>(HiddenWindowSize.Height)); // valid, so this cannot fail
        CubePushConstants pushConstants;
        pushConstants.ViewProjection = camera.GetViewProjection();

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
            RecordCubeDraw(renderer.GetCommandBuffer(), pipeline, cube, pushConstants);
            REQUIRE(renderer.EndFrame());
            ++drawn;
        }
        gpu.Device.WaitUntilIdle();
        CHECK(drawn == FrameCount);

        renderer.Destroy();
        renderer.Destroy();
        CHECK(renderer.GetCommandBuffer() == VK_NULL_HANDLE);
    }

    TEST_CASE("the cube looks right: three lit faces with black edges from the start view")
    {
        HiddenGpu gpu;
        REQUIRE(gpu.Create());

        // The program's start view (app.cpp).
        Wotah::Camera camera;
        camera.SetPosition({2.0f, 1.6f, 2.6f});
        camera.SetTarget({0.0f, 0.0f, 0.0f});
        REQUIRE(camera.SetClipDistances(0.1f, 10.0f));
        const std::vector<uint8_t> pixels = DrawCubeOffscreen(gpu.Device, camera);

        // shaders/cube.frag: orange * (0.25 + 0.75 * light); the top gets the most light, the front the least.
        CHECK(CountPixels(pixels, 0xED, 0x76, 0x00) > 10000);  // top
        CHECK(CountPixels(pixels, 0x85, 0x42, 0x00) > 10000);  // right (+x)
        CHECK(CountPixels(pixels, 0x6B, 0x35, 0x00) > 10000);  // front (+z)
        CHECK(CountPixels(pixels, 0x00, 0x00, 0x00) > 500);    // the black edges
        CHECK(CountPixels(pixels, 0x1A, 0x1A, 0x1A) > 250000); // the background around it

        // The center of the picture is the cube, not the background.
        const std::size_t center = (300 * 600 + 300) * 4;
        CHECK(pixels[center] > 0x40);
    }
}
