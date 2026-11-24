/**
 * @file device.cpp
 * Creates and frees the Vulkan instance, surface, GPU choice, logical device and queue.
 */
#include "render/device.hpp"

#include "core/logger.hpp"
#include "platform/window.hpp"
#include "render/vulkan_utils.hpp"

#include <vulkan/vulkan.h>

#include <cstring>
#include <optional>
#include <span>
#include <vector>

namespace
{
    /** The Khronos validation layer that comes with the Vulkan SDK. */
    constexpr const char* ValidationLayerName = "VK_LAYER_KHRONOS_validation";

    /** Debug builds turn on the validation layer when it is installed; release builds never do. */
#ifdef NDEBUG
    constexpr bool WantsValidation = false;
#else
    constexpr bool WantsValidation = true;
#endif

    /**
     * The one queue's priority; with a single queue any value in [0, 1] works the same (Vulkan spec, "Queue
     * Priority").
     */
    constexpr float QueuePriority = 1.0f;

    /** A GPU and the queue family on it that can both draw and present to the surface. */
    struct GpuChoice
    {
        VkPhysicalDevice Gpu         = VK_NULL_HANDLE;
        uint32_t         QueueFamily = 0;
    };

    /** Whether the validation layer is installed. */
    bool HasValidationLayer()
    {
        uint32_t count = 0;
        if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS)
        {
            return false;
        }
        std::vector<VkLayerProperties> layers(count);
        const VkResult                 result = vkEnumerateInstanceLayerProperties(&count, layers.data());
        if (result != VK_SUCCESS &&
            result != VK_INCOMPLETE) // VK_INCOMPLETE: a layer appeared meanwhile; check the rest
        {
            return false;
        }
        layers.resize(count);

        for (const VkLayerProperties& layer : layers)
        {
            if (std::strcmp(layer.layerName, ValidationLayerName) == 0)
            {
                return true;
            }
        }
        return false;
    }

    /** The instance with the window's extensions and, when wanted and installed, the validation layer. */
    VkInstance CreateInstance(const Wotah::Window& window, const std::string& appName)
    {
        const std::span<const char* const> extensions = window.GetRequiredInstanceExtensions();
        if (extensions.empty())
        {
            return VK_NULL_HANDLE;
        }

        VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        appInfo.pApplicationName = appName.c_str();
        appInfo.apiVersion       = VK_API_VERSION_1_3;

        VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        createInfo.pApplicationInfo        = &appInfo;
        createInfo.enabledExtensionCount   = static_cast<uint32_t>(extensions.size()); // GLFW gave it as a uint32_t
        createInfo.ppEnabledExtensionNames = extensions.data();

        if constexpr (WantsValidation)
        {
            if (HasValidationLayer())
            {
                createInfo.enabledLayerCount   = 1;
                createInfo.ppEnabledLayerNames = &ValidationLayerName;
                Wotah::Logger::Debug("Vulkan validation is on");
            }
            else
            {
                Wotah::Logger::Warning("{} is not installed, so Vulkan validation is off", ValidationLayerName);
            }
        }

        VkInstance instance = VK_NULL_HANDLE;
        if (!Wotah::Succeeded(vkCreateInstance(&createInfo, nullptr, &instance), "create the Vulkan instance"))
        {
            return VK_NULL_HANDLE;
        }
        return instance;
    }

    /** The first queue family on the GPU that can draw and present to the surface. */
    std::optional<uint32_t> FindQueueFamily(VkPhysicalDevice gpu, VkSurfaceKHR surface)
    {
        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count);
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, families.data());
        families.resize(count);

        for (uint32_t index = 0; index < count; ++index)
        {
            VkBool32 canPresent = VK_FALSE;
            if (vkGetPhysicalDeviceSurfaceSupportKHR(gpu, index, surface, &canPresent) != VK_SUCCESS)
            {
                continue;
            }
            if ((families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 && canPresent == VK_TRUE)
            {
                return index;
            }
        }
        return std::nullopt;
    }

    /** The first Vulkan 1.3 GPU with a queue family that can draw and present to the surface. */
    std::optional<GpuChoice> FindGpu(VkInstance instance, VkSurfaceKHR surface)
    {
        uint32_t count = 0;
        if (!Wotah::Succeeded(vkEnumeratePhysicalDevices(instance, &count, nullptr), "list the GPUs"))
        {
            return std::nullopt;
        }
        std::vector<VkPhysicalDevice> gpus(count);
        const VkResult                result = vkEnumeratePhysicalDevices(instance, &count, gpus.data());
        if (result != VK_INCOMPLETE &&
            !Wotah::Succeeded(result, "list the GPUs")) // VK_INCOMPLETE: a GPU appeared meanwhile
        {
            return std::nullopt;
        }
        gpus.resize(count);

        for (VkPhysicalDevice gpu : gpus)
        {
            VkPhysicalDeviceProperties properties;
            vkGetPhysicalDeviceProperties(gpu, &properties);
            if (properties.apiVersion < VK_API_VERSION_1_3)
            {
                continue;
            }

            if (const std::optional<uint32_t> family = FindQueueFamily(gpu, surface))
            {
                Wotah::Logger::Info("Using GPU {}", properties.deviceName);
                GpuChoice choice;
                choice.Gpu         = gpu;
                choice.QueueFamily = *family;
                return choice;
            }
        }
        return std::nullopt;
    }

    /** The logical device with one queue from the chosen family, the swapchain extension and dynamic rendering. */
    VkDevice CreateLogicalDevice(const GpuChoice& choice)
    {
        VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = choice.QueueFamily;
        queueInfo.queueCount       = 1;
        queueInfo.pQueuePriorities = &QueuePriority;

        // Dynamic rendering draws without VkRenderPass / VkFramebuffer; it is core in Vulkan 1.3 but still opt-in.
        VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        features13.dynamicRendering = VK_TRUE;

        const char* const  swapchainExtension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        VkDeviceCreateInfo createInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        createInfo.pNext                   = &features13;
        createInfo.queueCreateInfoCount    = 1;
        createInfo.pQueueCreateInfos       = &queueInfo;
        createInfo.enabledExtensionCount   = 1;
        createInfo.ppEnabledExtensionNames = &swapchainExtension;

        VkDevice device = VK_NULL_HANDLE;
        if (!Wotah::Succeeded(vkCreateDevice(choice.Gpu, &createInfo, nullptr, &device), "create the logical device"))
        {
            return VK_NULL_HANDLE;
        }
        return device;
    }
} // namespace

Wotah::Device::~Device()
{
    Destroy();
}

bool Wotah::Device::Create(const Window& window, const std::string& appName)
{
    m_instance = CreateInstance(window, appName);
    if (m_instance == VK_NULL_HANDLE)
    {
        return false;
    }

    m_surface = window.CreateSurface(m_instance);
    if (m_surface == VK_NULL_HANDLE)
    {
        return false;
    }

    const std::optional<GpuChoice> choice = FindGpu(m_instance, m_surface);
    if (!choice)
    {
        Logger::Error("No Vulkan 1.3 GPU with graphics + present support found");
        return false;
    }
    m_gpu         = choice->Gpu;
    m_queueFamily = choice->QueueFamily;

    m_device = CreateLogicalDevice(*choice);
    if (m_device == VK_NULL_HANDLE)
    {
        return false;
    }
    vkGetDeviceQueue(m_device, m_queueFamily, 0, &m_queue);
    return true;
}

void Wotah::Device::Destroy()
{
    // Children before their parents: the device, then the surface, then the instance that made both.
    // vkDestroySurfaceKHR accepts VK_NULL_HANDLE (Vulkan spec), so a part-way Create is fine.
    if (m_device != VK_NULL_HANDLE)
    {
        vkDestroyDevice(m_device, nullptr);
    }
    if (m_instance != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        vkDestroyInstance(m_instance, nullptr);
    }

    m_instance    = VK_NULL_HANDLE;
    m_surface     = VK_NULL_HANDLE;
    m_gpu         = VK_NULL_HANDLE;
    m_queueFamily = 0;
    m_device      = VK_NULL_HANDLE;
    m_queue       = VK_NULL_HANDLE;
}

void Wotah::Device::WaitUntilIdle() const
{
    if (m_device == VK_NULL_HANDLE)
    {
        return;
    }
    // Only fails when the device is lost; then there is nothing left to wait for, so the result is logged and
    // teardown goes on.
    (void)Wotah::Succeeded(vkDeviceWaitIdle(m_device), "wait for the GPU to finish");
}
