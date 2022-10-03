#include "VkTextures.h"
#include <stb_image.h>
#include "VkInitializers.h"


bool vkutil::loadImageFromFile(VulkanEngine& engine, const char* fname, AllocatedImage& outImage)
{
	//
	// Load image from file
	//
	int texWidth, texHeight, texChannels;
	stbi_uc* pixels = stbi_load(fname, &texWidth, &texHeight, &texChannels, STBI_default);

	if (!pixels)
	{
		std::cerr << "ERROR: failed to load texture " << fname << std::endl;
		return false;
	}

	void* pixelPtr = pixels;
	VkDeviceSize imageSize = texWidth * texHeight * texChannels;

	VkFormat imageFormat = VK_FORMAT_R8G8B8A8_SRGB;		// @HARDCODED: this could easily change

	AllocatedBuffer stagingBuffer = engine.createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

	void* data;
	vmaMapMemory(engine._allocator, stagingBuffer._allocation, &data);
	memcpy(data, pixelPtr, static_cast<size_t>(imageSize));
	vmaUnmapMemory(engine._allocator, stagingBuffer._allocation);

	stbi_image_free(pixels);

	//
	// Create the image in vulkan
	//
	VkExtent3D imageExtent = {
		.width = static_cast<uint32_t>(texWidth),
		.height = static_cast<uint32_t>(texHeight),
		.depth = 1,
	};
	VkImageCreateInfo dImageInfo = vkinit::imageCreateInfo(imageFormat, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, imageExtent);

	AllocatedImage newImage;
	VmaAllocationCreateInfo dImageAllocInfo = {
		.usage = VMA_MEMORY_USAGE_GPU_ONLY,
	};
	vmaCreateImage(engine._allocator, &dImageInfo, &dImageAllocInfo, &newImage._image, &newImage._allocation, nullptr);

	//
	// Copy image data into created image
	//
	engine.immediateSubmit([&](VkCommandBuffer cmd) {
		// Image layout for copying optimal
		VkImageSubresourceRange range = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		};
		VkImageMemoryBarrier imageBarrierToTransfer = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.image = newImage._image,
			.subresourceRange = range,
		};
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageBarrierToTransfer);

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

		// Image layout for shader access optimal
		VkImageMemoryBarrier imageBarrierToReadable = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.image = newImage._image,
			.subresourceRange = range,
		};
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageBarrierToReadable);
		});

	//
	// Cleanup
	//
	engine._mainDeletionQueue.pushFunction([=]() {
		vmaDestroyImage(engine._allocator, newImage._image, newImage._allocation);
		});
	vmaDestroyBuffer(engine._allocator, stagingBuffer._buffer, stagingBuffer._allocation);

	std::cout << "Texture " << fname << " loaded successfully" << std::endl;

	outImage = newImage;
	return true;
}
