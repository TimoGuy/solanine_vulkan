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
