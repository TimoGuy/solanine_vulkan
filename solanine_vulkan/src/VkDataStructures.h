#pragma once

#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>
#include <functional>
#include <deque>

namespace vkglTF { struct Model; }


#define VK_CHECK(x)                                                    \
	do                                                                 \
	{                                                                  \
		VkResult err = x;                                              \
		if (err)                                                       \
		{                                                              \
			std::cout <<"Detected Vulkan error: " << err << std::endl; \
			abort();                                                   \
		}                                                              \
	} while (0)


struct AllocatedBuffer
{
	VkBuffer _buffer;
	VmaAllocation _allocation;
};

struct AllocatedImage
{
	uint32_t _mipLevels;
	VkImage _image;
	VmaAllocation _allocation;
};

struct Texture
{
	AllocatedImage image;
	VkImageView imageView;
	VkSampler sampler;    // @NOTE: It actually isn't necessary to have a 1-to-1 with samplers and textures, however, this is for simplicity.  -Timo
};

struct Material
{
	VkDescriptorSet textureSet{ VK_NULL_HANDLE };   // Texture is defaulted to NULL
	VkPipeline pipeline;                            // @NOTE: in the case of PBR MATERIAL, there is going to be one pipeline, one pipelinelayout and many many texture set descriptorsets for the PBR Material  -Timo
	VkPipelineLayout pipelineLayout;
};

struct MeshCapturedInfo
{
	vkglTF::Model* model;
	uint32_t meshIndexCount;
	uint32_t meshFirstIndex;
	uint32_t meshNumInModel;
	uint32_t modelDrawCount;
	uint32_t baseModelRenderObjectIndex;
};

struct IndirectBatch
{
	vkglTF::Model* model;
	uint32_t first;
	uint32_t count;
};

struct DeletionQueue
{
	std::deque<std::function<void()>> deletors;

	void pushFunction(std::function<void()>&& function)
	{
		deletors.push_back(function);
	}

	void flush()
	{
		// Call deletor lambdas in reverse order
		for (auto it = deletors.rbegin(); it != deletors.rend(); it++)
		{
			(*it)();
		}

		deletors.clear();
	}
};
