#pragma once

class VulkanEngine;
struct AllocatedImage;


namespace vkutil
{
	bool loadImageFromFile(VulkanEngine& engine, const char* fname, VkFormat imageFormat, uint32_t mipLevels, AllocatedImage& outImage);
	bool loadImageFromFile(VulkanEngine& engine, const char* fname, VkFormat imageFormat, uint32_t mipLevels, int32_t& outWidth, int32_t& outHeight, AllocatedImage& outImage);		// @NOTE: mipLevels set to 0 will generate all mipmaps
	bool loadImageFromBuffer(VulkanEngine& engine, int texWidth, int texHeight, VkDeviceSize imageSize, VkFormat imageFormat, void* pixels, uint32_t mipLevels, AllocatedImage& outImage);
	bool loadImage3DFromFile(VulkanEngine& engine, std::vector<const char*> fnames, VkFormat imageFormat, AllocatedImage& outImage);  // @NOTE: as of right now, there are no mipmaps being created, since this is mainly meant for uploading lightmaps.
	bool loadImage3DFromBuffer(VulkanEngine& engine, int texWidth, int texHeight, int texDepth, VkDeviceSize imageSize, VkFormat imageFormat, void* pixels, AllocatedImage& outImage);  // @NOTE: as of right now, there are no mipmaps being created, since this is mainly meant for uploading lightmaps.
	bool loadImageCubemapFromFile(VulkanEngine& engine, std::vector<const char*> fnames, bool isHDR, VkFormat imageFormat, uint32_t mipLevels, AllocatedImage& outImage);		// @NOTE: fnames order is ...
}
