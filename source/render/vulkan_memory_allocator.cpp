/**
 * @file vulkan_memory_allocator.cpp
 * The one place the Vulkan Memory Allocator's implementation is compiled (VMA docs, "Quick start": define
 * VMA_IMPLEMENTATION in exactly one .cpp file). Everything else includes <vk_mem_alloc.h> for the declarations only.
 */
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
