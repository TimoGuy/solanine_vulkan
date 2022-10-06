#pragma once
#include "Imports.h"
#include "VulkanEngine.h"


namespace vkutil
{
	bool loadImageFromFile(VulkanEngine& engine, const char* fname, uint32_t mipLevels, AllocatedImage& outImage);		// @NOTE: mipLevels set to 0 will generate all mipmaps
}
