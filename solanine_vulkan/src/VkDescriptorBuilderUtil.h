#pragma once

#include <vector>
#include <vulkan/vulkan.h>


namespace vkutil
{
    namespace descriptorallocator
    {
        void init(VkDevice newDevice);
        void cleanup();

        void resetPools();
        bool allocate(VkDescriptorSet* set, VkDescriptorSetLayout layout);
    }

    namespace descriptorlayoutcache
    {
        void init(VkDevice newDevice);
        void cleanup();

        VkDescriptorSetLayout createDescriptorLayout(VkDescriptorSetLayoutCreateInfo* info);

        struct DescriptorLayoutInfo
        {
            std::vector<VkDescriptorSetLayoutBinding> bindings;
            bool operator==(const DescriptorLayoutInfo& other) const;
            size_t hash() const;
        };
    }

    class DescriptorBuilder
    {
    public:
        static DescriptorBuilder begin();

        DescriptorBuilder& bindBuffer(uint32_t binding, VkDescriptorBufferInfo* bufferInfo, VkDescriptorType type, VkShaderStageFlags stageFlags);
        DescriptorBuilder& bindImage(uint32_t binding, VkDescriptorImageInfo* imageInfo, VkDescriptorType type, VkShaderStageFlags stageFlags);
        DescriptorBuilder& bindImageArray(uint32_t binding, uint32_t imageCount, VkDescriptorImageInfo* imageInfos, VkDescriptorType type, VkShaderStageFlags stageFlags);

        bool build(VkDescriptorSet& set, VkDescriptorSetLayout& layout);
        bool build(VkDescriptorSet& set);
    private:
        DescriptorBuilder() { }

        std::vector<VkWriteDescriptorSet> writes;
        std::vector<VkDescriptorSetLayoutBinding> bindings;
    };
}