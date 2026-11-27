/**
 * @file descriptor_set.cpp
 * A descriptor set with its own layout and pool.
 */
#include "render/descriptor_set.hpp"

#include "core/logger.hpp"
#include "render/device.hpp"
#include "render/vulkan_utils.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>

Wotah::DescriptorSet::~DescriptorSet()
{
    Destroy();
}

bool Wotah::DescriptorSet::Create(const Device& device, std::span<const DescriptorBinding> bindings)
{
    Destroy(); // a second Create replaces the first instead of leaking it
    if (bindings.empty())
    {
        Logger::Error("A descriptor set needs at least one binding");
        return false;
    }
    m_device = device.GetHandle();

    // The layout: binding i is bindings[i], one descriptor each.
    std::vector<VkDescriptorSetLayoutBinding> layoutBindings(bindings.size());
    for (uint32_t index = 0; index < layoutBindings.size(); ++index)
    {
        layoutBindings[index].binding         = index;
        layoutBindings[index].descriptorType  = bindings[index].Type;
        layoutBindings[index].descriptorCount = 1;
        layoutBindings[index].stageFlags      = bindings[index].Stages;
        m_types.push_back(bindings[index].Type);
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo =
        MakeVulkanInfo<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    layoutInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
    layoutInfo.pBindings    = layoutBindings.data();
    if (!Succeeded(vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_layout),
                   "create a descriptor set layout"))
    {
        return false;
    }

    // A pool with room for exactly this one set: one descriptor per binding.
    std::vector<VkDescriptorPoolSize> poolSizes;
    for (const DescriptorBinding& binding : bindings)
    {
        const auto same = std::ranges::find(poolSizes, binding.Type, &VkDescriptorPoolSize::type);
        if (same != poolSizes.end())
        {
            ++same->descriptorCount;
        }
        else
        {
            poolSizes.push_back({binding.Type, 1});
        }
    }
    VkDescriptorPoolCreateInfo poolInfo =
        MakeVulkanInfo<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
    poolInfo.maxSets       = 1;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes    = poolSizes.data();
    if (!Succeeded(vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_pool), "create a descriptor pool"))
    {
        return false;
    }

    VkDescriptorSetAllocateInfo setInfo =
        MakeVulkanInfo<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    setInfo.descriptorPool     = m_pool;
    setInfo.descriptorSetCount = 1;
    setInfo.pSetLayouts        = &m_layout;
    return Succeeded(vkAllocateDescriptorSets(m_device, &setInfo, &m_set), "allocate a descriptor set");
}

void Wotah::DescriptorSet::Destroy()
{
    if (m_device == VK_NULL_HANDLE)
    {
        return;
    }
    if (m_pool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(m_device, m_pool, nullptr); // frees the set with it
        m_pool = VK_NULL_HANDLE;
        m_set  = VK_NULL_HANDLE;
    }
    if (m_layout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(m_device, m_layout, nullptr);
        m_layout = VK_NULL_HANDLE;
    }
    m_types.clear();
    m_device = VK_NULL_HANDLE;
}

void Wotah::DescriptorSet::WriteBuffer(uint32_t binding, VkBuffer buffer, VkDeviceSize size)
{
    if (!IsBindingOf(binding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER))
    {
        return;
    }
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer          = buffer;
    bufferInfo.range           = size;
    VkWriteDescriptorSet write = MakeVulkanInfo<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
    write.dstSet               = m_set;
    write.dstBinding           = binding;
    write.descriptorCount      = 1;
    write.descriptorType       = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo          = &bufferInfo;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

void Wotah::DescriptorSet::WriteImage(uint32_t binding, VkImageView view, VkSampler sampler, VkImageLayout layout)
{
    if (!IsBindingOf(binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER))
    {
        return;
    }
    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler          = sampler;
    imageInfo.imageView        = view;
    imageInfo.imageLayout      = layout;
    VkWriteDescriptorSet write = MakeVulkanInfo<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
    write.dstSet               = m_set;
    write.dstBinding           = binding;
    write.descriptorCount      = 1;
    write.descriptorType       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo           = &imageInfo;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

void Wotah::DescriptorSet::WriteStorageImage(uint32_t binding, VkImageView view)
{
    if (!IsBindingOf(binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE))
    {
        return;
    }
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageView        = view;
    imageInfo.imageLayout      = VK_IMAGE_LAYOUT_GENERAL;
    VkWriteDescriptorSet write = MakeVulkanInfo<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
    write.dstSet               = m_set;
    write.dstBinding           = binding;
    write.descriptorCount      = 1;
    write.descriptorType       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.pImageInfo           = &imageInfo;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

void Wotah::DescriptorSet::Bind(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, uint32_t setIndex,
                                VkPipelineBindPoint bindPoint) const
{
    vkCmdBindDescriptorSets(commandBuffer, bindPoint, pipelineLayout, setIndex, 1, &m_set, 0, nullptr);
}

bool Wotah::DescriptorSet::IsBindingOf(uint32_t binding, VkDescriptorType type) const
{
    if (m_set == VK_NULL_HANDLE || binding >= m_types.size() || m_types[binding] != type)
    {
        Logger::Error("Descriptor binding {} is not of the kind written, or the set was not created", binding);
        return false;
    }
    return true;
}
