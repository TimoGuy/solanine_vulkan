#pragma once

#include <iostream>
#include <fstream>
#include <vector>
#include <functional>
#include <deque>
#include <filesystem>
#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>
#include <glm/glm.hpp>


struct AllocatedBuffer
{
	VkBuffer _buffer;
	VmaAllocation _allocation;
};
