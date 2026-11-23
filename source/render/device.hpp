/**
 * @file device.hpp
 * The Vulkan base everything else is built on: the instance, the window's surface, the chosen GPU, the logical device
 * and its one queue.
 *
 *     Wotah::Device device;
 *     if (device.Create(window, "My App"))
 *     {
 *         vkCreateSwapchainKHR(device.GetHandle(), ...);
 *         device.WaitUntilIdle();
 *     }
 *     device.Destroy();
 *
 * Vulkan stays inside device.cpp; this header only forward-declares the handle types.
 */
#pragma once

#include "render/vulkan_handles.hpp"

#include <cstdint>
#include <string>

namespace Wotah
{
    class Window;

    /**
     * The Vulkan instance, the window's surface, the GPU and the logical device with one queue that can both draw and
     * present. The destructor runs Destroy, so nothing leaks when Create fails part way.
     */
    class Device final
    {
    public:
        Device() = default;
        ~Device();

        Device(const Device&)            = delete;
        Device& operator=(const Device&) = delete;

        /**
         * Creates the instance (with the validation layer in debug builds, when installed), the window's surface,
         * picks the first Vulkan 1.3 GPU that can draw and present to it, and creates the logical device with dynamic
         * rendering; false (and logged) when one step fails. The window must outlive the device.
         */
        [[nodiscard]] bool Create(const Window& window, const std::string& appName);

        /** Frees the device, the surface and the instance; safe to call more than once or after a failed Create. */
        void Destroy();

        /** Waits until the GPU has finished all submitted work, so nothing is destroyed while in use. */
        void WaitUntilIdle() const;

        /** The Vulkan instance. */
        [[nodiscard]] VkInstance GetInstance() const noexcept { return m_instance; }

        /** The window's surface. */
        [[nodiscard]] VkSurfaceKHR GetSurface() const noexcept { return m_surface; }

        /** The chosen GPU. */
        [[nodiscard]] VkPhysicalDevice GetGpu() const noexcept { return m_gpu; }

        /** The logical device. */
        [[nodiscard]] VkDevice GetHandle() const noexcept { return m_device; }

        /** The queue that draws and presents. */
        [[nodiscard]] VkQueue GetQueue() const noexcept { return m_queue; }

        /** The queue family of GetQueue, for command pools. */
        [[nodiscard]] uint32_t GetQueueFamily() const noexcept { return m_queueFamily; }

    private:
        // Declared in creation order; Destroy frees them in reverse.
        VkInstance       m_instance    = nullptr;
        VkSurfaceKHR     m_surface     = nullptr;
        VkPhysicalDevice m_gpu         = nullptr;
        uint32_t         m_queueFamily = 0;
        VkDevice         m_device      = nullptr;
        VkQueue          m_queue       = nullptr;
    };
} // namespace Wotah
