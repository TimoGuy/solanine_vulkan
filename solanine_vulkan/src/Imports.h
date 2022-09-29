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

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/ext.hpp>


struct AllocatedBuffer
{
	VkBuffer _buffer;
	VmaAllocation _allocation;
};

struct AllocatedImage
{
	VkImage _image;
	VmaAllocation _allocation;
};
