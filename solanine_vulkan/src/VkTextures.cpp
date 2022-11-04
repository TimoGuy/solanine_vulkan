#include "VkTextures.h"

#include <iostream>
#include <stb_image.h>
#include "VkInitializers.h"
#include "VkDataStructures.h"
#include "VulkanEngine.h"


bool vkutil::loadImageFromFile(VulkanEngine& engine, const char* fname, VkFormat imageFormat, uint32_t mipLevels, AllocatedImage& outImage)
{
	//
	// Load image from file
	//
	int texWidth, texHeight, texChannels;
	stbi_uc* pixels = stbi_load(fname, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

	if (!pixels)
	{
		std::cerr << "ERROR: failed to load texture " << fname << std::endl;
		return false;
	}

	void* pixelPtr = pixels;
	VkDeviceSize imageSize = texWidth * texHeight * 4;		// @HARDCODED: bc planning on having the alpha channel in here too
	//VkFormat imageFormat = VK_FORMAT_R8G8B8A8_SRGB;		// @HARDCODED: this could easily change

	bool ret = loadImageFromBuffer(engine, texWidth, texHeight, imageSize, imageFormat, pixelPtr, mipLevels, outImage);
	stbi_image_free(pixels);

	std::cout << "Texture (mips=" << outImage._mipLevels << ")" << std::endl << "\t" << fname << std::endl << "\tloaded successfully" << std::endl;

	return ret;
}

bool vkutil::loadImageFromBuffer(VulkanEngine& engine, int texWidth, int texHeight, VkDeviceSize imageSize, VkFormat imageFormat, void* pixels, uint32_t mipLevels, AllocatedImage& outImage)
{
	//
	// Copy image to CPU-side buffer
	//
	AllocatedBuffer stagingBuffer = engine.createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

	void* data;
	vmaMapMemory(engine._allocator, stagingBuffer._allocation, &data);
	memcpy(data, pixels, static_cast<size_t>(imageSize));
	vmaUnmapMemory(engine._allocator, stagingBuffer._allocation);

	//
	// Create GPU-side buffer
	//
	const uint32_t maxMipmaps = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;
	AllocatedImage newImage;
	newImage._mipLevels = mipLevels == 0 ? maxMipmaps : std::min(mipLevels, maxMipmaps);

	VkExtent3D imageExtent = {
		.width = static_cast<uint32_t>(texWidth),
		.height = static_cast<uint32_t>(texHeight),
		.depth = 1,
	};
	VkImageCreateInfo dstImageInfo =
		vkinit::imageCreateInfo(
			imageFormat,
			VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			imageExtent,
			newImage._mipLevels
		);

	VmaAllocationCreateInfo dstImageAllocInfo = {
		.usage = VMA_MEMORY_USAGE_GPU_ONLY,
	};
	vmaCreateImage(engine._allocator, &dstImageInfo, &dstImageAllocInfo, &newImage._image, &newImage._allocation, nullptr);

	//
	// Copy image data to GPU
	//
	engine.immediateSubmit([&](VkCommandBuffer cmd) {
		// Image layout for copying optimal
		VkImageMemoryBarrier imageBarrierToTransfer = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.image = newImage._image,
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = newImage._mipLevels,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
		};
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
			0, nullptr,
			0, nullptr,
			1, &imageBarrierToTransfer
		);

		// Copy pixel data into image
		VkBufferImageCopy copyRegion = {
			.bufferOffset = 0,
			.bufferRowLength = 0,
			.bufferImageHeight = 0,
			.imageSubresource = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = 0,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
			.imageExtent = imageExtent,
		};
		vkCmdCopyBufferToImage(cmd, stagingBuffer._buffer, newImage._image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

		// @NOTE: the transform to SHADER_READ_ONLY_OPTIMAL is missing bc the next part
		// when mipmapping is generated is when the transformation will happen  -Timo
		});

	//
	// Check if linear blitting is supported for mipmap generation
	//
	VkFormatProperties formatProperties;
	vkGetPhysicalDeviceFormatProperties(engine._chosenGPU, imageFormat, &formatProperties);
	if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
	{
		std::cerr << "ERROR: texture image format doesn't support linear blitting" << std::endl;
		return false;
	}

	//
	// Loop thru and upload mips to GPU
	//
	engine.immediateSubmit([&](VkCommandBuffer cmd) {
		VkImageMemoryBarrier imageBarrier = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = newImage._image,
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
		};

		int32_t mipWidth = texWidth;
		int32_t mipHeight = texHeight;

		for (uint32_t mipLevel = 1; mipLevel < newImage._mipLevels; mipLevel++)		// @NOTE: start at mipLevel=1 bc the first mipLevel (0) gets copied into the buffer instead of blitted like in this section
		{
			// Pipeline barrier for changing prev mip to a src optimal image
			imageBarrier.subresourceRange.baseMipLevel = mipLevel - 1;
			imageBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			imageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			imageBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			imageBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			vkCmdPipelineBarrier(
				cmd,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
				0, nullptr,
				0, nullptr,
				1, &imageBarrier
			);

			// Blit image to next mip (blitted img ends up being DST_OPTIMAL layout)
			VkImageBlit blitRegion = {
				.srcSubresource = {
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.mipLevel = mipLevel - 1,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
				.srcOffsets = {
					{ 0, 0, 0 },
					{ mipWidth, mipHeight, 1 },
				},
				.dstSubresource = {
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.mipLevel = mipLevel,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
				.dstOffsets = {
					{ 0, 0, 0 },
					{ mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 },
				},
			};
			vkCmdBlitImage(cmd,
				newImage._image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				newImage._image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1, &blitRegion,
				VK_FILTER_LINEAR
			);

			// Pipeline barrier for changing prev mip to shader reading optimal image
			imageBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			imageBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imageBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			vkCmdPipelineBarrier(
				cmd,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
				0, nullptr,
				0, nullptr,
				1, &imageBarrier
			);

			// Update mip sizes
			if (mipWidth > 1) mipWidth /= 2;
			if (mipHeight > 1) mipHeight /= 2;
		}

		// Pipeline barrier for changing FINAL prev mip to shader reading optimal image
		imageBarrier.subresourceRange.baseMipLevel = newImage._mipLevels - 1;
		imageBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		imageBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(
			cmd,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
			0, nullptr,
			0, nullptr,
			1, &imageBarrier
		);
		});

	//
	// Cleanup
	//
	auto engineAllocator = engine._allocator;
	engine._mainDeletionQueue.pushFunction([=]() {
		vmaDestroyImage(engineAllocator, newImage._image, newImage._allocation);
		});
	vmaDestroyBuffer(engine._allocator, stagingBuffer._buffer, stagingBuffer._allocation);

	outImage = newImage;
	return true;
}

bool vkutil::loadImageCubemapFromFile(VulkanEngine& engine, std::vector<const char*> fnames, bool isHDR, VkFormat imageFormat, uint32_t mipLevels, AllocatedImage& outImage)
{
	if (mipLevels != 1)
	{
		std::cerr << "ERROR: currently, mipmap generation is not supported for cubemaps" << std::endl;
		return false;
	}

	if (fnames.size() != 6)
	{
		std::cerr << "ERROR: for cubemap creation, 6 fnames are expected. Actual: " << fnames.size() << std::endl;
		return false;
	}

	//
	// Load images from files
	//
	std::vector<void*> pixelPtrs;
	std::vector<size_t> imageSizes;
	std::vector<VkExtent3D> imageExtents;
	int maxDimension = -1;
	for (const char* fname : fnames)
	{
		int texWidth, texHeight, texChannels;
		if (isHDR)
		{
			float_t* pixels = stbi_loadf(fname, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
			if (!pixels)
			{
				std::cerr << "ERROR: failed to load texture " << fname << std::endl;
				return false;
			}

			pixelPtrs.push_back(pixels);
			imageSizes.push_back(static_cast<size_t>(texWidth * texHeight * 4 * sizeof(float_t)));    // @HARDCODED: bc planning on having the alpha channel in here too
		}
		else
		{
			stbi_uc* pixels = stbi_load(fname, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
			if (!pixels)
			{
				std::cerr << "ERROR: failed to load texture " << fname << std::endl;
				return false;
			}

			pixelPtrs.push_back(pixels);
			imageSizes.push_back(static_cast<size_t>(texWidth * texHeight * 4 * sizeof(stbi_uc)));		// @HARDCODED: bc planning on having the alpha channel in here too
		}
		imageExtents.push_back({ (uint32_t)texWidth, (uint32_t)texHeight, 1 });
		maxDimension = std::max(maxDimension, std::max(texWidth, texHeight));
	}

	//VkFormat imageFormat = VK_FORMAT_R8G8B8A8_SRGB;		// @HARDCODED: this could easily change

	//
	// Copy image to CPU-side buffer
	//
	size_t totalImageSize = 0;
	for (size_t imageSize : imageSizes)
		totalImageSize += imageSize;
	AllocatedBuffer stagingBuffer = engine.createBuffer(totalImageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

	char* data;
	vmaMapMemory(engine._allocator, stagingBuffer._allocation, (void**)&data);
	for (size_t i = 0; i < pixelPtrs.size(); i++)
	{
		void* pixelPtr = pixelPtrs[i];
		size_t imageSize = imageSizes[i];
		memcpy(data, pixelPtr, imageSize);
		data += imageSize;  // Call me... Dmitri the Evil
	}
	vmaUnmapMemory(engine._allocator, stagingBuffer._allocation);

	for (void* pixelPtr : pixelPtrs)
		stbi_image_free(pixelPtr);

	//
	// Create GPU-side buffer
	//
	const uint32_t maxMipmaps = static_cast<uint32_t>(std::floor(std::log2(maxDimension))) + 1;  // @NOTE: Idk really know if we're gonna support mipmaps for cubemaps
	AllocatedImage newImage;
	newImage._mipLevels = mipLevels == 0 ? maxMipmaps : std::min(mipLevels, maxMipmaps);

	VkImageCreateInfo dstImageInfo =
		vkinit::imageCubemapCreateInfo(
			imageFormat,
			VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			imageExtents[0],	// @NOTE: @TODO @ASSERT All of the extents should be equal to each other!!!
			newImage._mipLevels
		);

	VmaAllocationCreateInfo dstImageAllocInfo = {
		.usage = VMA_MEMORY_USAGE_GPU_ONLY,
	};
	vmaCreateImage(engine._allocator, &dstImageInfo, &dstImageAllocInfo, &newImage._image, &newImage._allocation, nullptr);

	//
	// Copy image data to GPU
	//
	engine.immediateSubmit([&](VkCommandBuffer cmd) {
		// Image layout for copying optimal
		VkImageMemoryBarrier imageBarrierToTransfer = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.image = newImage._image,
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = newImage._mipLevels,
				.baseArrayLayer = 0,
				.layerCount = 6,
			},
		};
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
			0, nullptr,
			0, nullptr,
			1, &imageBarrierToTransfer
		);

		// Copy pixel data into image
		std::vector<VkBufferImageCopy> copyRegions;
		size_t copyOffset = 0;
		for (uint32_t face = 0; face < 6; face++)
		{
			auto imageExtent = imageExtents[face];
			VkBufferImageCopy copyRegion = {
				.bufferOffset = copyOffset,
				.bufferRowLength = 0,
				.bufferImageHeight = 0,
				.imageSubresource = {
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.mipLevel = 0,
					.baseArrayLayer = face,
					.layerCount = 1,
				},
				.imageExtent = imageExtent,
			};
			copyRegions.push_back(copyRegion);
			copyOffset += imageSizes[face];
		}
		vkCmdCopyBufferToImage(
			cmd,
			stagingBuffer._buffer,
			newImage._image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			static_cast<uint32_t>(copyRegions.size()),
			copyRegions.data()
		);

		// @NOTE: the transform to SHADER_READ_ONLY_OPTIMAL is missing bc the next part
		// when mipmapping is generated is when the transformation will happen  -Timo
		});

	//
	// Check if linear blitting is supported for mipmap generation
	//
	VkFormatProperties formatProperties;
	vkGetPhysicalDeviceFormatProperties(engine._chosenGPU, imageFormat, &formatProperties);
	if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
	{
		std::cerr << "ERROR: texture image format doesn't support linear blitting" << std::endl;
		return false;
	}

	//
	// Mipmapping????? @TODO: Maybe... make mipmapping a thing?
	//
	engine.immediateSubmit([&](VkCommandBuffer cmd) {
		VkImageMemoryBarrier imageBarrier = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = newImage._image,
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 6,
			},
		};

		// @INCOMPLETE: mipmapping stuff skipped right here...

		// Pipeline barrier for changing FINAL prev mip to shader reading optimal image
		imageBarrier.subresourceRange.baseMipLevel = newImage._mipLevels - 1;
		imageBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		imageBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(
			cmd,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
			0, nullptr,
			0, nullptr,
			1, &imageBarrier
		);
		});

	//
	// Cleanup
	//
	auto engineAllocator = engine._allocator;
	engine._mainDeletionQueue.pushFunction([=]() {
		vmaDestroyImage(engineAllocator, newImage._image, newImage._allocation);
		});
	vmaDestroyBuffer(engine._allocator, stagingBuffer._buffer, stagingBuffer._allocation);

	std::string combinedFnames = "";
	for (auto fname : fnames)
		combinedFnames += "\t" + std::string(fname) + "\n";

	std::cout << "Cubemap (mips=" << newImage._mipLevels << ")" << std::endl << combinedFnames << "\tloaded successfully" << std::endl;

	outImage = newImage;
	return true;
}
