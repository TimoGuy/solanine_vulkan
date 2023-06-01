#include "VkDescriptorBuilderUtil.h"

#include <algorithm>
#include <unordered_map>
#include <utility>


namespace vkutil
{
    namespace descriptorallocator
    {
        VkDescriptorPool grabPool();
        VkDescriptorPool createPool(uint32_t count, VkDescriptorPoolCreateFlags flags);

        VkDevice device;
        VkDescriptorPool currentPool{ VK_NULL_HANDLE };
        std::vector<VkDescriptorPool> usedPools;
        std::vector<VkDescriptorPool> freePools;

        std::vector<std::pair<VkDescriptorType, float_t>> descriptorSizes = {
            { VK_DESCRIPTOR_TYPE_SAMPLER, 0.5f },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4.f },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4.f },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1.f },
            { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1.f },
            { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1.f },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2.f },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2.f },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1.f },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1.f },
            { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 0.5f }
        };

        void init(VkDevice newDevice)
        {
            device = newDevice;
        }

        void cleanup()
        {
            for (auto p : freePools)
                vkDestroyDescriptorPool(device, p, nullptr);
            for (auto p : usedPools)
                vkDestroyDescriptorPool(device, p, nullptr);
        }

        void resetPools()
        {
            for (auto p : usedPools)
            {
                vkResetDescriptorPool(device, p, 0);
                freePools.push_back(p);
            }

            usedPools.clear();
            currentPool = VK_NULL_HANDLE;
        }

        bool allocate(VkDescriptorSet* set, VkDescriptorSetLayout layout)
        {
            if (currentPool == VK_NULL_HANDLE)
            {
                currentPool = grabPool();
                usedPools.push_back(currentPool);
            }

            VkDescriptorSetAllocateInfo allocInfo = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .pNext = nullptr,
                .descriptorPool = currentPool,
                .descriptorSetCount = 1,
                .pSetLayouts = &layout,
            };

            VkResult allocResult = vkAllocateDescriptorSets(device, &allocInfo, set);
            bool needReallocate = false;

            switch (allocResult)
            {
            case VK_SUCCESS:
                return true;
            case VK_ERROR_FRAGMENTED_POOL:
            case VK_ERROR_OUT_OF_POOL_MEMORY:
                needReallocate = true;
                break;
            default:
                return false;
            }

            if (needReallocate)
            {
                currentPool = grabPool();
                usedPools.push_back(currentPool);

                // Try again
                allocResult = vkAllocateDescriptorSets(device, &allocInfo, set);
                if (allocResult == VK_SUCCESS)
                    return true;
            }

            return false;
        }

        VkDescriptorPool grabPool()
        {
            if (freePools.size() > 0)
            {
                VkDescriptorPool pool = freePools.back();
                freePools.pop_back();
                return pool;
            }
            else
                return createPool(1000, 0);
        }

        VkDescriptorPool createPool(uint32_t count, VkDescriptorPoolCreateFlags flags)
        {
            std::vector<VkDescriptorPoolSize> sizes;
            sizes.reserve(descriptorSizes.size());
            for (auto sz : descriptorSizes)
                sizes.push_back({ sz.first, uint32_t(sz.second * count) });
            
            VkDescriptorPoolCreateInfo poolInfo = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .flags = flags,
                .maxSets = count,
                .poolSizeCount = (uint32_t)sizes.size(),
                .pPoolSizes = sizes.data(),
            };

            VkDescriptorPool descriptorPool;
            vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool);
            return descriptorPool;
        }
    }

    namespace descriptorlayoutcache
    {
        struct DescriptorLayoutHash
        {
            size_t operator()(const DescriptorLayoutInfo &k) const
            {
                return k.hash();
            }
        };

        std::unordered_map<DescriptorLayoutInfo, VkDescriptorSetLayout, DescriptorLayoutHash> layoutCache;
        VkDevice device;

        void init(VkDevice newDevice)
        {
            device = newDevice;
        }

        void cleanup()
        {
            for (auto pair : layoutCache)
                vkDestroyDescriptorSetLayout(device, pair.second, nullptr);
        }

        VkDescriptorSetLayout createDescriptorLayout(VkDescriptorSetLayoutCreateInfo* info)
        {
            DescriptorLayoutInfo layoutInfo;
            layoutInfo.bindings.reserve(info->bindingCount);
            bool isSorted = true;
            int32_t lastBinding = -1;

            for (uint32_t i = 0; i < info->bindingCount; i++)
            {
                layoutInfo.bindings.push_back(info->pBindings[i]);

                // Check if sorted
                if ((int32_t)info->pBindings[i].binding > lastBinding)
                {
                    lastBinding = info->pBindings[i].binding;
                }
                else
                    isSorted = false;
            }

            if (!isSorted)
            {
                // Sort bindings
                std::sort(
                    layoutInfo.bindings.begin(),
                    layoutInfo.bindings.end(),
                    [](VkDescriptorSetLayoutBinding& a, VkDescriptorSetLayoutBinding& b) {
                        return a.binding < b.binding;
                    }
                );
            }

            // Grab from cache
            auto it = layoutCache.find(layoutInfo);
            if (it != layoutCache.end())
                return it->second;
            else
            {
                // Create new layout & add to cache
                VkDescriptorSetLayout layout;
                vkCreateDescriptorSetLayout(device, info, nullptr, &layout);

                layoutCache[layoutInfo] = layout;
                return layout;
            }
        }

        bool DescriptorLayoutInfo::operator==(const DescriptorLayoutInfo& other) const
        {
            if (other.bindings.size() != bindings.size())
                return false;

            // Bindings are pre-sorted, so just zip compare
            for (size_t i = 0; i < bindings.size(); i++)
            {
                if (other.bindings[i].binding != bindings[i].binding)
                    return false;
                if (other.bindings[i].descriptorType != bindings[i].descriptorType)
                    return false;
                if (other.bindings[i].descriptorCount != bindings[i].descriptorCount)
                    return false;
                if (other.bindings[i].stageFlags != bindings[i].stageFlags)
                    return false;
            }

            return true;
        }

        size_t DescriptorLayoutInfo::hash() const
        {
            size_t result = std::hash<size_t>()(bindings.size());

            for (const VkDescriptorSetLayoutBinding& b : bindings)
            {
                // Pack the binding data into a single int64. Not fully correct but it's ok
                size_t binding_hash = b.binding | b.descriptorType << 8 | b.descriptorCount << 16 | b.stageFlags << 24;

                // Shuffle the packed binding data and xor it with the main hash
                result ^= std::hash<size_t>()(binding_hash);
            }

            return result;
        }
    }

    //
    // DescriptorBuilder class
    //
    DescriptorBuilder DescriptorBuilder::begin()
    {
        DescriptorBuilder builder;
        return builder;
    }

    DescriptorBuilder& DescriptorBuilder::bindBuffer(uint32_t binding, VkDescriptorBufferInfo* bufferInfo, VkDescriptorType type, VkShaderStageFlags stageFlags)
    {
        // Create the descriptor binding for the layout
		VkDescriptorSetLayoutBinding newBinding = {
            .binding = binding,
            .descriptorType = type,
            .descriptorCount = 1,
            .stageFlags = stageFlags,
            .pImmutableSamplers = nullptr,
        };

		bindings.push_back(newBinding);

		// Create the descriptor write
		VkWriteDescriptorSet newWrite = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,
            .dstBinding = binding,
            .descriptorCount = 1,
            .descriptorType = type,
            .pBufferInfo = bufferInfo,
        };

		writes.push_back(newWrite);
		return *this;
    }

    DescriptorBuilder& DescriptorBuilder::bindImage(uint32_t binding, VkDescriptorImageInfo* imageInfo, VkDescriptorType type, VkShaderStageFlags stageFlags)
    {
        VkDescriptorSetLayoutBinding newBinding = {
            .binding = binding,
            .descriptorType = type,
            .descriptorCount = 1,
            .stageFlags = stageFlags,
            .pImmutableSamplers = nullptr,
        };

		bindings.push_back(newBinding);

		VkWriteDescriptorSet newWrite = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,
            .dstBinding = binding,
            .descriptorCount = 1,
            .descriptorType = type,
            .pImageInfo = imageInfo,
        };

		writes.push_back(newWrite);
		return *this;
    }

    DescriptorBuilder& DescriptorBuilder::bindImageArray(uint32_t binding, uint32_t imageCount, VkDescriptorImageInfo* imageInfos, VkDescriptorType type, VkShaderStageFlags stageFlags)
    {
        // See for how to use this: http://kylehalladay.com/blog/tutorial/vulkan/2018/01/28/Textue-Arrays-Vulkan.html

        VkDescriptorSetLayoutBinding newBinding = {
            .binding = binding,
            .descriptorType = type,
            .descriptorCount = imageCount,
            .stageFlags = stageFlags,
            .pImmutableSamplers = nullptr,
        };

		bindings.push_back(newBinding);

		VkWriteDescriptorSet newWrite = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,
            .dstBinding = binding,
            .descriptorCount = imageCount,
            .descriptorType = type,
            .pImageInfo = imageInfos,
        };

		writes.push_back(newWrite);
		return *this;
    }

    bool DescriptorBuilder::build(VkDescriptorSet& set, VkDescriptorSetLayout& layout)
    {
        // Build layout first
        VkDescriptorSetLayoutCreateInfo layoutInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .bindingCount = static_cast<uint32_t>(bindings.size()),
            .pBindings = bindings.data(),
        };

        layout = vkutil::descriptorlayoutcache::createDescriptorLayout(&layoutInfo);

        // Allocate descriptor
        bool success = vkutil::descriptorallocator::allocate(&set, layout);
        if (!success)
            return false;

        // Write descriptor
        for (VkWriteDescriptorSet& w : writes)
            w.dstSet = set;

        vkUpdateDescriptorSets(vkutil::descriptorallocator::device, (uint32_t)writes.size(), writes.data(), 0, nullptr);
        return true;
    }

    bool DescriptorBuilder::build(VkDescriptorSet& set)
    {
        VkDescriptorSetLayout layout;
        return build(set, layout);
    }
}