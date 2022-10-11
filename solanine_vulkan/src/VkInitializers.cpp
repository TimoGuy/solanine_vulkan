#include "VkInitializers.h"


VkCommandPoolCreateInfo vkinit::commandPoolCreateInfo(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags)
{
	VkCommandPoolCreateInfo commandPoolInfo = {};
	commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	commandPoolInfo.pNext = nullptr;

	commandPoolInfo.queueFamilyIndex = queueFamilyIndex;
	commandPoolInfo.flags = flags;
	return commandPoolInfo;
}

VkCommandBufferAllocateInfo vkinit::commandBufferAllocateInfo(VkCommandPool pool, uint32_t count, VkCommandBufferLevel level)
{
	VkCommandBufferAllocateInfo cmdAllocInfo = {};
	cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmdAllocInfo.pNext = nullptr;

	cmdAllocInfo.commandPool = pool;
	cmdAllocInfo.commandBufferCount = count;
	cmdAllocInfo.level = level;
	return cmdAllocInfo;
}

VkPipelineShaderStageCreateInfo vkinit::pipelineShaderStageCreateInfo(VkShaderStageFlagBits stage, VkShaderModule shaderModule)
{
	VkPipelineShaderStageCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	info.pNext = nullptr;
	info.stage = stage;
	info.module = shaderModule;
	info.pName = "main";
	return info;
}

VkPipelineVertexInputStateCreateInfo vkinit::vertexInputStateCreateInfo()
{
	VkPipelineVertexInputStateCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	info.pNext = nullptr;
	info.vertexBindingDescriptionCount = 0;
	info.vertexAttributeDescriptionCount = 0;
	return info;
}

VkPipelineInputAssemblyStateCreateInfo vkinit::inputAssemblyCreateInfo(VkPrimitiveTopology topology)
{
	VkPipelineInputAssemblyStateCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	info.pNext = nullptr;
	info.topology = topology;
	info.primitiveRestartEnable = VK_FALSE;		// IDK what the heck this does
	return info;
}

VkPipelineRasterizationStateCreateInfo vkinit::rasterizationStateCreateInfo(VkPolygonMode polygonMode)
{
	VkPipelineRasterizationStateCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	info.pNext = nullptr;

	info.depthClampEnable = VK_FALSE;	// @NOTE: note this... bc you'll need it for shadow rendering yo!  -Timo
	info.rasterizerDiscardEnable = VK_FALSE;	// IDK what the functionality of this is... like why is it ever VK_TRUE?
	info.polygonMode = polygonMode;
	info.lineWidth = 1.0f;

	info.cullMode = VK_CULL_MODE_BACK_BIT;
	info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

	info.depthBiasEnable = VK_FALSE;
	info.depthBiasConstantFactor = 0.0f;
	info.depthBiasClamp = 0.0f;
	info.depthBiasSlopeFactor = 0.0f;

	return info;
}

VkPipelineMultisampleStateCreateInfo vkinit::multisamplingStateCreateInfo()
{
	VkPipelineMultisampleStateCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	info.pNext = nullptr;

	info.sampleShadingEnable = VK_FALSE;
	info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	info.minSampleShading = 1.0f;
	info.pSampleMask = nullptr;
	info.alphaToCoverageEnable = VK_FALSE;
	info.alphaToOneEnable = VK_FALSE;

	return info;
}

VkPipelineColorBlendAttachmentState vkinit::colorBlendAttachmentState()
{
	VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachment.blendEnable = VK_FALSE;
	return colorBlendAttachment;
}

VkPipelineLayoutCreateInfo vkinit::pipelineLayoutCreateInfo()
{
	VkPipelineLayoutCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	info.pNext = nullptr;

	info.flags = 0;
	info.setLayoutCount = 0;
	info.pSetLayouts = nullptr;
	info.pushConstantRangeCount = 0;
	info.pPushConstantRanges = nullptr;

	return info;
}

VkImageCreateInfo vkinit::imageCreateInfo(VkFormat format, VkImageUsageFlags usageFlags, VkExtent3D extent, uint32_t mipLevels)
{
	VkImageCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.pNext = nullptr,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent = extent,
		.mipLevels = mipLevels,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = usageFlags
	};

	return info;
}

VkImageCreateInfo vkinit::imageCubemapCreateInfo(VkFormat format, VkImageUsageFlags usageFlags, VkExtent3D extent, uint32_t mipLevels)
{
	VkImageCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.pNext = nullptr,
		.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent = extent,
		.mipLevels = mipLevels,
		.arrayLayers = 6,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = usageFlags
	};

	return info;
}

VkImageViewCreateInfo vkinit::imageviewCreateInfo(VkFormat format, VkImage image, VkImageAspectFlags aspectFlags, uint32_t mipLevels)
{
	VkImageViewCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.pNext = nullptr,
		.image = image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = format,
		.subresourceRange = {
			.aspectMask = aspectFlags,
			.baseMipLevel = 0,
			.levelCount = mipLevels,
			.baseArrayLayer = 0,
			.layerCount = 1
		}
	};

	return info;
}

VkImageViewCreateInfo vkinit::imageviewCubemapCreateInfo(VkFormat format, VkImage image, VkImageAspectFlags aspectFlags, uint32_t mipLevels)
{
	VkImageViewCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.pNext = nullptr,
		.image = image,
		.viewType = VK_IMAGE_VIEW_TYPE_CUBE,
		.format = format,
		.subresourceRange = {
			.aspectMask = aspectFlags,
			.baseMipLevel = 0,
			.levelCount = mipLevels,
			.baseArrayLayer = 0,
			.layerCount = 6
		}
	};

	return info;
}

VkPipelineDepthStencilStateCreateInfo vkinit::depthStencilCreateInfo(bool doDepthTest, bool doDepthWrite, VkCompareOp compareOp)
{
	VkPipelineDepthStencilStateCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.pNext = nullptr,
		.depthTestEnable = (doDepthTest ? VK_TRUE : VK_FALSE),
		.depthWriteEnable = (doDepthWrite ? VK_TRUE : VK_FALSE),
		.depthCompareOp = (doDepthTest ? compareOp : VK_COMPARE_OP_ALWAYS),
		.depthBoundsTestEnable = VK_FALSE,
		.stencilTestEnable = VK_FALSE,
		.minDepthBounds = 0.0f,
		.maxDepthBounds = 1.0f,
	};

	return info;
}

VkDescriptorSetLayoutBinding vkinit::descriptorSetLayoutBinding(VkDescriptorType type, VkShaderStageFlags stageFlags, uint32_t binding)
{
	VkDescriptorSetLayoutBinding setBind = {
		.binding = binding,
		.descriptorType = type,
		.descriptorCount = 1,
		.stageFlags = stageFlags,
		.pImmutableSamplers = nullptr,
	};
	return setBind;
}

VkWriteDescriptorSet vkinit::writeDescriptorBuffer(VkDescriptorType type, VkDescriptorSet dstSet, VkDescriptorBufferInfo* bufferInfo, uint32_t binding)
{
	VkWriteDescriptorSet setWrite = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.pNext = nullptr,
		.dstSet = dstSet,
		.dstBinding = binding,
		.descriptorCount = 1,
		.descriptorType = type,
		.pBufferInfo = bufferInfo,
	};
	return setWrite;
}

VkCommandBufferBeginInfo vkinit::commandBufferBeginInfo(VkCommandBufferUsageFlags flags)
{
	VkCommandBufferBeginInfo info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.pNext = nullptr,
		.flags = flags,
		.pInheritanceInfo = nullptr,
	};
	return info;
}

VkSubmitInfo vkinit::submitInfo(VkCommandBuffer* cmd)
{
	VkSubmitInfo info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.pNext = nullptr,
		.waitSemaphoreCount = 0,
		.pWaitSemaphores = nullptr,
		.pWaitDstStageMask = nullptr,
		.commandBufferCount = 1,
		.pCommandBuffers = cmd,
		.signalSemaphoreCount = 0,
		.pSignalSemaphores = nullptr,
	};
	return info;
}

VkSamplerCreateInfo vkinit::samplerCreateInfo(VkFilter filters, VkSamplerAddressMode samplerAddressMode)
{
	VkSamplerCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.pNext = nullptr,
		.magFilter = filters,
		.minFilter = filters,
		.addressModeU = samplerAddressMode,
		.addressModeV = samplerAddressMode,
		.addressModeW = samplerAddressMode,
	};
	return info;
}

VkWriteDescriptorSet vkinit::writeDescriptorImage(VkDescriptorType type, VkDescriptorSet dstSet, VkDescriptorImageInfo* imageInfo, uint32_t binding)
{
	VkWriteDescriptorSet info = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.pNext = nullptr,
		.dstSet = dstSet,
		.dstBinding = binding,
		.descriptorCount = 1,
		.descriptorType = type,
		.pImageInfo = imageInfo,
	};
	return info;
}
