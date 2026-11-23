/**
 * @file vulkan_handles.hpp
 * Vulkan's handle types without the Vulkan headers, so class headers can pass handles around and still compile fast.
 *
 * They are declared exactly as vulkan_core.h declares them on 64-bit systems (VK_DEFINE_HANDLE, and
 * VK_DEFINE_NON_DISPATCHABLE_HANDLE with VK_USE_64_BIT_PTR_DEFINES). Every .cpp file that includes a class header and
 * <vulkan/vulkan.h> sees both declarations, so a mismatch is a compile error, never a silent one.
 */
#pragma once

// 32-bit builds declare the non-dispatchable handles as uint64_t instead of pointers (vulkan_core.h).
static_assert(sizeof(void*) == 8, "vulkan_handles.hpp matches Vulkan's handle types on 64-bit systems only");

typedef struct VkInstance_T* VkInstance;
typedef struct VkPhysicalDevice_T* VkPhysicalDevice;
typedef struct VkDevice_T* VkDevice;
typedef struct VkQueue_T* VkQueue;
typedef struct VkSurfaceKHR_T* VkSurfaceKHR;
