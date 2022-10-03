#pragma once
#include "Imports.h"
#include "VulkanEngine.h"


namespace vkutil
{
	bool loadImageFromFile(VulkanEngine& engine, const char* fname, AllocatedImage& outImage);
}
