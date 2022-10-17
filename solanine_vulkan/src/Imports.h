#pragma once

#include <iostream>
#include <fstream>
#include <vector>
#include <functional>
#include <deque>
#include <filesystem>
#include <unordered_map>
#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>
#include <taskflow/taskflow.hpp>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/ext.hpp>
#include <glm/gtx/vector_angle.hpp>


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
	VkDescriptorSet textureSet{ VK_NULL_HANDLE };	// Texture is defaulted to NULL
	VkPipeline pipeline;    // @NOTE: in the case of PBR MATERIAL, there is going to be one pipeline, one pipelinelayout and many many texture set descriptorsets for the PBR Material  -Timo
	VkPipelineLayout pipelineLayout;
};
