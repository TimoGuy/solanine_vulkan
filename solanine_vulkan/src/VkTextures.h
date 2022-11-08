#pragma once
#include <vector>
#include <vulkan/vulkan.h>
class VulkanEngine;
struct AllocatedImage;


namespace vkutil
{
	bool loadImageFromFile(VulkanEngine& engine, const char* fname, VkFormat imageFormat, uint32_t mipLevels, AllocatedImage& outImage);		// @NOTE: mipLevels set to 0 will generate all mipmaps
	bool loadImageFromBuffer(VulkanEngine& engine, int texWidth, int texHeight, VkDeviceSize imageSize, VkFormat imageFormat, void* pixels, uint32_t mipLevels, AllocatedImage& outImage);
	bool loadImageCubemapFromFile(VulkanEngine& engine, std::vector<const char*> fnames, bool isHDR, VkFormat imageFormat, uint32_t mipLevels, AllocatedImage& outImage);		// @NOTE: fnames order is ...
}
