/**
 * Vulkan glTF model and texture loading class based on tinyglTF (https://github.com/syoyo/tinygltf)
 *
 * Copyright (C) 2018-2022 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#include "VkglTFModel.h"
#include "VulkanEngine.h"
#include "VkTextures.h"
#include "VkInitializers.h"


namespace vkglTF
{
	//
	// Bounding box
	//
	BoundingBox::BoundingBox() { };
	BoundingBox::BoundingBox(glm::vec3 min, glm::vec3 max) : min(min), max(max) { };

	BoundingBox BoundingBox::getAABB(glm::mat4 m)
	{
		glm::vec3 min = glm::vec3(m[3]);
		glm::vec3 max = min;
		glm::vec3 v0, v1;

		glm::vec3 right = glm::vec3(m[0]);
		v0 = right * this->min.x;
		v1 = right * this->max.x;
		min += glm::min(v0, v1);
		max += glm::max(v0, v1);

		glm::vec3 up = glm::vec3(m[1]);
		v0 = up * this->min.y;
		v1 = up * this->max.y;
		min += glm::min(v0, v1);
		max += glm::max(v0, v1);

		glm::vec3 back = glm::vec3(m[2]);
		v0 = back * this->min.z;
		v1 = back * this->max.z;
		min += glm::min(v0, v1);
		max += glm::max(v0, v1);

		return BoundingBox(min, max);
	}

	////
	//// Texture
	////
	//void Texture::updateDescriptor()
	//{
	//	descriptor.sampler = sampler;
	//	descriptor.imageView = view;
	//	descriptor.imageLayout = imageLayout;
	//}

	//void Texture::destroy()
	//{
	//	vkDestroyImageView(device->_device, view, nullptr);
	//	vkDestroyImage(device->_device, image, nullptr);
	//	vkFreeMemory(device->_device, deviceMemory, nullptr);
	//	vkDestroySampler(device->_device, sampler, nullptr);
	//}

	//void Texture::fromglTfImage(tinygltf::Image& gltfimage, TextureSampler textureSampler, VulkanEngine* device, VkQueue copyQueue)
	//{
	//	this->device = device;

	//	unsigned char* buffer = nullptr;
	//	VkDeviceSize bufferSize = 0;
	//	bool deleteBuffer = false;
	//	if (gltfimage.component == 3)
	//	{
	//		// Most devices don't support RGB only on Vulkan so convert if necessary
	//		// TODO: Check actual format support and transform only if required
	//		bufferSize = gltfimage.width * gltfimage.height * 4;
	//		buffer = new unsigned char[bufferSize];
	//		unsigned char* rgba = buffer;
	//		unsigned char* rgb = &gltfimage.image[0];
	//		for (int32_t i = 0; i < gltfimage.width * gltfimage.height; ++i)
	//		{
	//			for (int32_t j = 0; j < 3; ++j)
	//			{
	//				rgba[j] = rgb[j];
	//			}
	//			rgba += 4;
	//			rgb += 3;
	//		}
	//		deleteBuffer = true;
	//	}
	//	else
	//	{
	//		buffer = &gltfimage.image[0];
	//		bufferSize = gltfimage.image.size();
	//	}

	//	VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;

	//	VkFormatProperties formatProperties;

	//	width = gltfimage.width;
	//	height = gltfimage.height;
	//	mipLevels = static_cast<uint32_t>(floor(log2(std::max(width, height))) + 1.0);

	//	vkGetPhysicalDeviceFormatProperties(device->_chosenGPU, format, &formatProperties);
	//	assert(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT);
	//	assert(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT);

	//	VkMemoryAllocateInfo memAllocInfo{};
	//	memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	//	VkMemoryRequirements memReqs{};

	//	VkBuffer stagingBuffer;
	//	VkDeviceMemory stagingMemory;

	//	VkBufferCreateInfo bufferCreateInfo{};
	//	bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	//	bufferCreateInfo.size = bufferSize;
	//	bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	//	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	//	VK_CHECK(vkCreateBuffer(device->_device, &bufferCreateInfo, nullptr, &stagingBuffer));
	//	vkGetBufferMemoryRequirements(device->_device, stagingBuffer, &memReqs);
	//	memAllocInfo.allocationSize = memReqs.size;
	//	memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, nullptr);
	//	VK_CHECK(vkAllocateMemory(device->_device, &memAllocInfo, nullptr, &stagingMemory));
	//	VK_CHECK(vkBindBufferMemory(device->_device, stagingBuffer, stagingMemory, 0));

	//	uint8_t* data;
	//	VK_CHECK(vkMapMemory(device->_device, stagingMemory, 0, memReqs.size, 0, (void**)&data));
	//	memcpy(data, buffer, bufferSize);
	//	vkUnmapMemory(device->_device, stagingMemory);

	//	VkImageCreateInfo imageCreateInfo{};
	//	imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	//	imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
	//	imageCreateInfo.format = format;
	//	imageCreateInfo.mipLevels = mipLevels;
	//	imageCreateInfo.arrayLayers = 1;
	//	imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	//	imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	//	imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
	//	imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	//	imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	//	imageCreateInfo.extent = { width, height, 1 };
	//	imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	//	VK_CHECK(vkCreateImage(device->_device, &imageCreateInfo, nullptr, &image));
	//	vkGetImageMemoryRequirements(device->_device, image, &memReqs);
	//	memAllocInfo.allocationSize = memReqs.size;
	//	memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, nullptr);
	//	VK_CHECK(vkAllocateMemory(device->_device, &memAllocInfo, nullptr, &deviceMemory));
	//	VK_CHECK(vkBindImageMemory(device->_device, image, deviceMemory, 0));

	//	VkImageSubresourceRange subresourceRange = {
	//		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
	//		.levelCount = 1,
	//		.layerCount = 1,
	//	};

	//	device->immediateSubmit([&](VkCommandBuffer cmd)
	//		{

	//			{
	//				VkImageMemoryBarrier imageMemoryBarrier{};
	//				imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	//				imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	//				imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	//				imageMemoryBarrier.srcAccessMask = 0;
	//				imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	//				imageMemoryBarrier.image = image;
	//				imageMemoryBarrier.subresourceRange = subresourceRange;
	//				vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
	//			}

	//			VkBufferImageCopy bufferCopyRegion = {};
	//			bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	//			bufferCopyRegion.imageSubresource.mipLevel = 0;
	//			bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
	//			bufferCopyRegion.imageSubresource.layerCount = 1;
	//			bufferCopyRegion.imageExtent.width = width;
	//			bufferCopyRegion.imageExtent.height = height;
	//			bufferCopyRegion.imageExtent.depth = 1;

	//			vkCmdCopyBufferToImage(cmd, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferCopyRegion);

	//			{
	//				VkImageMemoryBarrier imageMemoryBarrier{};
	//				imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	//				imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	//				imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	//				imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	//				imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	//				imageMemoryBarrier.image = image;
	//				imageMemoryBarrier.subresourceRange = subresourceRange;
	//				vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
	//			}
	//		});

	//	vkFreeMemory(device->_device, stagingMemory, nullptr);
	//	vkDestroyBuffer(device->_device, stagingBuffer, nullptr);

	//	// Generate the mip chain (glTF uses jpg and png, so we need to create this manually)
	//	device->immediateSubmit([&](VkCommandBuffer cmd)
	//		{
	//			for (uint32_t i = 1; i < mipLevels; i++)
	//			{
	//				VkImageBlit imageBlit{};

	//				imageBlit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	//				imageBlit.srcSubresource.layerCount = 1;
	//				imageBlit.srcSubresource.mipLevel = i - 1;
	//				imageBlit.srcOffsets[1].x = int32_t(width >> (i - 1));
	//				imageBlit.srcOffsets[1].y = int32_t(height >> (i - 1));
	//				imageBlit.srcOffsets[1].z = 1;

	//				imageBlit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	//				imageBlit.dstSubresource.layerCount = 1;
	//				imageBlit.dstSubresource.mipLevel = i;
	//				imageBlit.dstOffsets[1].x = int32_t(width >> i);
	//				imageBlit.dstOffsets[1].y = int32_t(height >> i);
	//				imageBlit.dstOffsets[1].z = 1;

	//				VkImageSubresourceRange mipSubRange = {};
	//				mipSubRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	//				mipSubRange.baseMipLevel = i;
	//				mipSubRange.levelCount = 1;
	//				mipSubRange.layerCount = 1;

	//				{
	//					VkImageMemoryBarrier imageMemoryBarrier{};
	//					imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	//					imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	//					imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	//					imageMemoryBarrier.srcAccessMask = 0;
	//					imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	//					imageMemoryBarrier.image = image;
	//					imageMemoryBarrier.subresourceRange = mipSubRange;
	//					vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
	//				}

	//				vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageBlit, VK_FILTER_LINEAR);

	//				{
	//					VkImageMemoryBarrier imageMemoryBarrier{};
	//					imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	//					imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	//					imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	//					imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	//					imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	//					imageMemoryBarrier.image = image;
	//					imageMemoryBarrier.subresourceRange = mipSubRange;
	//					vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
	//				}
	//			}

	//			subresourceRange.levelCount = mipLevels;
	//			imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	//			{
	//				VkImageMemoryBarrier imageMemoryBarrier{};
	//				imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	//				imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	//				imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	//				imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	//				imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	//				imageMemoryBarrier.image = image;
	//				imageMemoryBarrier.subresourceRange = subresourceRange;
	//				vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
	//			}
	//		});

	//	VkSamplerCreateInfo samplerInfo{};
	//	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	//	samplerInfo.magFilter = textureSampler.magFilter;
	//	samplerInfo.minFilter = textureSampler.minFilter;
	//	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	//	samplerInfo.addressModeU = textureSampler.addressModeU;
	//	samplerInfo.addressModeV = textureSampler.addressModeV;
	//	samplerInfo.addressModeW = textureSampler.addressModeW;
	//	samplerInfo.compareOp = VK_COMPARE_OP_NEVER;
	//	samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	//	samplerInfo.maxAnisotropy = 1.0;
	//	samplerInfo.anisotropyEnable = VK_FALSE;
	//	samplerInfo.maxLod = (float)mipLevels;
	//	samplerInfo.maxAnisotropy = 8.0f;
	//	samplerInfo.anisotropyEnable = VK_TRUE;
	//	VK_CHECK(vkCreateSampler(device->_device, &samplerInfo, nullptr, &sampler));

	//	VkImageViewCreateInfo viewInfo{};
	//	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	//	viewInfo.image = image;
	//	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	//	viewInfo.format = format;
	//	viewInfo.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
	//	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	//	viewInfo.subresourceRange.layerCount = 1;
	//	viewInfo.subresourceRange.levelCount = mipLevels;
	//	VK_CHECK(vkCreateImageView(device->_device, &viewInfo, nullptr, &view));

	//	descriptor.sampler = sampler;
	//	descriptor.imageView = view;
	//	descriptor.imageLayout = imageLayout;

	//	if (deleteBuffer)
	//		delete[] buffer;

	//}

	//
	// Primitive
	//
	Primitive::Primitive(uint32_t firstIndex, uint32_t indexCount, uint32_t vertexCount/*, Material& material*/) : firstIndex(firstIndex), indexCount(indexCount), vertexCount(vertexCount)/*, material(material)*/
	{
		hasIndices = indexCount > 0;
	};

	void Primitive::setBoundingBox(glm::vec3 min, glm::vec3 max)
	{
		bb.min = min;
		bb.max = max;
		bb.valid = true;
	}

	//
	// Mesh
	//
	Mesh::Mesh(VulkanEngine* engine, glm::mat4 matrix)
	{
		this->engine = engine;
		this->uniformBlock.matrix = matrix;

		//
		// @TODO: make this sync up when recreateSwapchain() is executed on vkengine... this will get
		//        destroyed since the descriptorpool gets destroyed there.... maybe make some kind of init
		//        function that sets up these descriptorbuffers? That could be good.  -Timo
		//
		uniformBuffer.descriptorBuffer =
			engine->createBuffer(
				sizeof(uniformBlock),
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VMA_MEMORY_USAGE_CPU_TO_GPU
			);
		vmaMapMemory(engine->_allocator, uniformBuffer.descriptorBuffer._allocation, &uniformBuffer.mapped);    // So we can just grab a pointer and hit memcpy() tons of times per frame!

		VkDescriptorSetAllocateInfo skeletalAnimationSetAllocInfo = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.pNext = nullptr,
			.descriptorPool = engine->_descriptorPool,
			.descriptorSetCount = 1,
			.pSetLayouts = &engine->_skeletalAnimationSetLayout,
		};
		vkAllocateDescriptorSets(engine->_device, &skeletalAnimationSetAllocInfo, &uniformBuffer.descriptorSet);

		VkDescriptorBufferInfo bufferInfo = {
			.buffer = uniformBuffer.descriptorBuffer._buffer,
			.offset = 0,
			.range = sizeof(uniformBlock),
		};
		VkWriteDescriptorSet writeDescriptorSet = vkinit::writeDescriptorBuffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uniformBuffer.descriptorSet, &bufferInfo, 0);
		vkUpdateDescriptorSets(engine->_device, 1, &writeDescriptorSet, 0, nullptr);


		// @TODO: make sure all this stuff is in the descructor too!!!
	};

	Mesh::~Mesh()
	{
		vmaUnmapMemory(engine->_allocator, uniformBuffer.descriptorBuffer._allocation);
		vmaDestroyBuffer(engine->_allocator, uniformBuffer.descriptorBuffer._buffer, uniformBuffer.descriptorBuffer._allocation);

		for (Primitive* p : primitives)
			delete p;
	}

	void Mesh::setBoundingBox(glm::vec3 min, glm::vec3 max)
	{
		bb.min = min;
		bb.max = max;
		bb.valid = true;
	}

	//
	// Node
	//
	void Node::generateCalculateJointMatrixTaskflow(tf::Taskflow& taskflow, tf::Task* taskPrerequisite)
	{
		auto smolTask = taskflow.emplace([&]() {
			update();
			});

		if (taskPrerequisite != nullptr)
			smolTask.succeed(*taskPrerequisite);

		for (auto& child : children)
		{
			child->generateCalculateJointMatrixTaskflow(taskflow, &smolTask);
		}
	}

	glm::mat4 Node::localMatrix()
	{
		return glm::translate(glm::mat4(1.0f), translation) * glm::mat4(rotation) * glm::scale(glm::mat4(1.0f), scale) * matrix;
	}

	glm::mat4 Node::getMatrix()
	{
		glm::mat4 m = localMatrix();
		vkglTF::Node* p = parent;
		while (p)
		{
			m = p->localMatrix() * m;
			p = p->parent;
		}
		return m;
	}

	void Node::update()
	{
		if (mesh)
		{
			glm::mat4 m = getMatrix();
			if (skin)
			{
				mesh->uniformBlock.matrix = m;
				// Update join matrices
				glm::mat4 inverseTransform = glm::inverse(m);
				size_t numJoints = std::min((uint32_t)skin->joints.size(), MAX_NUM_JOINTS);
				for (size_t i = 0; i < numJoints; i++)
				{
					vkglTF::Node* jointNode = skin->joints[i];
					glm::mat4 jointMat = jointNode->getMatrix() * skin->inverseBindMatrices[i];
					jointMat = inverseTransform * jointMat;
					mesh->uniformBlock.jointMatrix[i] = jointMat;
				}
				mesh->uniformBlock.jointcount = (float)numJoints;
				memcpy(mesh->uniformBuffer.mapped, &mesh->uniformBlock, sizeof(mesh->uniformBlock));
			}
			else
			{
				memcpy(mesh->uniformBuffer.mapped, &m, sizeof(glm::mat4));
			}
		}
	}

	Node::~Node()
	{
		if (mesh)
		{
			delete mesh;
		}
		for (auto& child : children)
		{
			delete child;
		}
	}

	//
	// Vertex
	//
	VertexInputDescription Model::Vertex::getVertexDescription()
	{
		VertexInputDescription description;

		//
		// 1 vertex buffer binding
		//
		VkVertexInputBindingDescription mainBinding = {
			.binding = 0,
			.stride = sizeof(Vertex),
			.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
		};
		description.bindings.push_back(mainBinding);

		//
		// Attributes
		//
		VkVertexInputAttributeDescription posAttribute = {
			.location = 0,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(Vertex, pos),
		};
		VkVertexInputAttributeDescription normalAttribute = {
			.location = 1,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(Vertex, normal),
		};
		VkVertexInputAttributeDescription uv0Attribute = {
			.location = 2,
			.binding = 0,
			.format = VK_FORMAT_R32G32_SFLOAT,
			.offset = offsetof(Vertex, uv0),
		};
		VkVertexInputAttributeDescription uv1Attribute = {
			.location = 3,
			.binding = 0,
			.format = VK_FORMAT_R32G32_SFLOAT,
			.offset = offsetof(Vertex, uv1),
		};
		VkVertexInputAttributeDescription joint0Attribute = {
			.location = 4,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32A32_SFLOAT,
			.offset = offsetof(Vertex, joint0),
		};
		VkVertexInputAttributeDescription weight0Attribute = {
			.location = 5,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32A32_SFLOAT,
			.offset = offsetof(Vertex, weight0),
		};
		VkVertexInputAttributeDescription colorAttribute = {
			.location = 6,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32A32_SFLOAT,
			.offset = offsetof(Vertex, color),
		};

		description.attributes.push_back(posAttribute);
		description.attributes.push_back(normalAttribute);
		description.attributes.push_back(uv0Attribute);
		description.attributes.push_back(uv1Attribute);
		description.attributes.push_back(joint0Attribute);
		description.attributes.push_back(weight0Attribute);
		description.attributes.push_back(colorAttribute);
		return description;
	}

	//
	// Model
	//
	void Model::destroy(VmaAllocator allocator)
	{
		if (vertices.buffer != VK_NULL_HANDLE)
		{
			vmaDestroyBuffer(allocator, vertices.buffer, vertices.allocation);
			vertices.buffer = VK_NULL_HANDLE;
		}
		if (indices.buffer != VK_NULL_HANDLE)
		{
			vmaDestroyBuffer(allocator, indices.buffer, indices.allocation);
			indices.buffer = VK_NULL_HANDLE;
		}
		/*for (auto texture : textures)    // @TODO: have some kind of texture deletion routine... maybe similar to what's going on with the _mainDeletionQueue???
			texture.destroy();
		textures.resize(0);
		textureSamplers.resize(0);*/
		for (auto node : nodes)
		{
			delete node;
		}
		//materials.resize(0);
		animations.resize(0);
		nodes.resize(0);
		linearNodes.resize(0);
		extensions.resize(0);
		for (auto skin : skins)
		{
			delete skin;
		}
		skins.resize(0);
	};

	void Model::loadNode(VulkanEngine* engine, vkglTF::Node* parent, const tinygltf::Node& node, uint32_t nodeIndex, const tinygltf::Model& model, LoaderInfo& loaderInfo, float globalscale)
	{
		vkglTF::Node* newNode = new Node{};
		newNode->index = nodeIndex;
		newNode->parent = parent;
		newNode->name = node.name;
		newNode->skinIndex = node.skin;
		newNode->matrix = glm::mat4(1.0f);

		// Generate local node matrix
		glm::vec3 translation = glm::vec3(0.0f);
		if (node.translation.size() == 3)
		{
			translation = glm::make_vec3(node.translation.data());
			newNode->translation = translation;
		}
		glm::mat4 rotation = glm::mat4(1.0f);
		if (node.rotation.size() == 4)
		{
			glm::quat q = glm::make_quat(node.rotation.data());
			newNode->rotation = glm::mat4(q);
		}
		glm::vec3 scale = glm::vec3(1.0f);
		if (node.scale.size() == 3)
		{
			scale = glm::make_vec3(node.scale.data());
			newNode->scale = scale;
		}
		if (node.matrix.size() == 16)
		{
			newNode->matrix = glm::make_mat4x4(node.matrix.data());
		};

		// Node with children
		if (node.children.size() > 0)
		{
			for (size_t i = 0; i < node.children.size(); i++)
			{
				loadNode(engine, newNode, model.nodes[node.children[i]], node.children[i], model, loaderInfo, globalscale);
			}
		}

		// Node contains mesh data
		if (node.mesh > -1)
		{
			const tinygltf::Mesh mesh = model.meshes[node.mesh];
			Mesh* newMesh = new Mesh(engine, newNode->matrix);
			for (size_t j = 0; j < mesh.primitives.size(); j++)
			{
				const tinygltf::Primitive& primitive = mesh.primitives[j];
				uint32_t vertexStart = loaderInfo.vertexPos;
				uint32_t indexStart = loaderInfo.indexPos;
				uint32_t indexCount = 0;
				uint32_t vertexCount = 0;
				glm::vec3 posMin{};
				glm::vec3 posMax{};
				bool hasSkin = false;
				bool hasIndices = primitive.indices > -1;
				// Vertices
				{
					const float* bufferPos = nullptr;
					const float* bufferNormals = nullptr;
					const float* bufferTexCoordSet0 = nullptr;
					const float* bufferTexCoordSet1 = nullptr;
					const float* bufferColorSet0 = nullptr;
					const void* bufferJoints = nullptr;
					const float* bufferWeights = nullptr;

					int posByteStride;
					int normByteStride;
					int uv0ByteStride;
					int uv1ByteStride;
					int color0ByteStride;
					int jointByteStride;
					int weightByteStride;

					int jointComponentType;

					// Position attribute is required
					assert(primitive.attributes.find("POSITION") != primitive.attributes.end());

					const tinygltf::Accessor& posAccessor = model.accessors[primitive.attributes.find("POSITION")->second];
					const tinygltf::BufferView& posView = model.bufferViews[posAccessor.bufferView];
					bufferPos = reinterpret_cast<const float*>(&(model.buffers[posView.buffer].data[posAccessor.byteOffset + posView.byteOffset]));
					posMin = glm::vec3(posAccessor.minValues[0], posAccessor.minValues[1], posAccessor.minValues[2]);
					posMax = glm::vec3(posAccessor.maxValues[0], posAccessor.maxValues[1], posAccessor.maxValues[2]);
					vertexCount = static_cast<uint32_t>(posAccessor.count);
					posByteStride = posAccessor.ByteStride(posView) ? (posAccessor.ByteStride(posView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC3);

					if (primitive.attributes.find("NORMAL") != primitive.attributes.end())
					{
						const tinygltf::Accessor& normAccessor = model.accessors[primitive.attributes.find("NORMAL")->second];
						const tinygltf::BufferView& normView = model.bufferViews[normAccessor.bufferView];
						bufferNormals = reinterpret_cast<const float*>(&(model.buffers[normView.buffer].data[normAccessor.byteOffset + normView.byteOffset]));
						normByteStride = normAccessor.ByteStride(normView) ? (normAccessor.ByteStride(normView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC3);
					}

					// UVs
					if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end())
					{
						const tinygltf::Accessor& uvAccessor = model.accessors[primitive.attributes.find("TEXCOORD_0")->second];
						const tinygltf::BufferView& uvView = model.bufferViews[uvAccessor.bufferView];
						bufferTexCoordSet0 = reinterpret_cast<const float*>(&(model.buffers[uvView.buffer].data[uvAccessor.byteOffset + uvView.byteOffset]));
						uv0ByteStride = uvAccessor.ByteStride(uvView) ? (uvAccessor.ByteStride(uvView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC2);
					}
					if (primitive.attributes.find("TEXCOORD_1") != primitive.attributes.end())
					{
						const tinygltf::Accessor& uvAccessor = model.accessors[primitive.attributes.find("TEXCOORD_1")->second];
						const tinygltf::BufferView& uvView = model.bufferViews[uvAccessor.bufferView];
						bufferTexCoordSet1 = reinterpret_cast<const float*>(&(model.buffers[uvView.buffer].data[uvAccessor.byteOffset + uvView.byteOffset]));
						uv1ByteStride = uvAccessor.ByteStride(uvView) ? (uvAccessor.ByteStride(uvView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC2);
					}

					// Vertex colors
					if (primitive.attributes.find("COLOR_0") != primitive.attributes.end())
					{
						const tinygltf::Accessor& accessor = model.accessors[primitive.attributes.find("COLOR_0")->second];
						const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
						bufferColorSet0 = reinterpret_cast<const float*>(&(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
						color0ByteStride = accessor.ByteStride(view) ? (accessor.ByteStride(view) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC3);
					}

					// Skinning
					// Joints
					if (primitive.attributes.find("JOINTS_0") != primitive.attributes.end())
					{
						const tinygltf::Accessor& jointAccessor = model.accessors[primitive.attributes.find("JOINTS_0")->second];
						const tinygltf::BufferView& jointView = model.bufferViews[jointAccessor.bufferView];
						bufferJoints = &(model.buffers[jointView.buffer].data[jointAccessor.byteOffset + jointView.byteOffset]);
						jointComponentType = jointAccessor.componentType;
						jointByteStride = jointAccessor.ByteStride(jointView) ? (jointAccessor.ByteStride(jointView) / tinygltf::GetComponentSizeInBytes(jointComponentType)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC4);
					}

					if (primitive.attributes.find("WEIGHTS_0") != primitive.attributes.end())
					{
						const tinygltf::Accessor& weightAccessor = model.accessors[primitive.attributes.find("WEIGHTS_0")->second];
						const tinygltf::BufferView& weightView = model.bufferViews[weightAccessor.bufferView];
						bufferWeights = reinterpret_cast<const float*>(&(model.buffers[weightView.buffer].data[weightAccessor.byteOffset + weightView.byteOffset]));
						weightByteStride = weightAccessor.ByteStride(weightView) ? (weightAccessor.ByteStride(weightView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC4);
					}

					hasSkin = (bufferJoints && bufferWeights);

					for (size_t v = 0; v < posAccessor.count; v++)
					{
						Vertex& vert = loaderInfo.vertexBuffer[loaderInfo.vertexPos];
						vert.pos = glm::vec4(glm::make_vec3(&bufferPos[v * posByteStride]), 1.0f);
						vert.normal = glm::normalize(glm::vec3(bufferNormals ? glm::make_vec3(&bufferNormals[v * normByteStride]) : glm::vec3(0.0f)));
						vert.uv0 = bufferTexCoordSet0 ? glm::make_vec2(&bufferTexCoordSet0[v * uv0ByteStride]) : glm::vec3(0.0f);
						vert.uv1 = bufferTexCoordSet1 ? glm::make_vec2(&bufferTexCoordSet1[v * uv1ByteStride]) : glm::vec3(0.0f);
						vert.color = bufferColorSet0 ? glm::make_vec4(&bufferColorSet0[v * color0ByteStride]) : glm::vec4(1.0f);

						if (hasSkin)
						{
							switch (jointComponentType)
							{
							case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
							{
								const uint16_t* buf = static_cast<const uint16_t*>(bufferJoints);
								vert.joint0 = glm::vec4(glm::make_vec4(&buf[v * jointByteStride]));
								break;
							}
							case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
							{
								const uint8_t* buf = static_cast<const uint8_t*>(bufferJoints);
								vert.joint0 = glm::vec4(glm::make_vec4(&buf[v * jointByteStride]));
								break;
							}
							default:
								// Not supported by spec
								std::cerr << "Joint component type " << jointComponentType << " not supported!" << std::endl;
								break;
							}
						}
						else
						{
							vert.joint0 = glm::vec4(0.0f);
						}
						vert.weight0 = hasSkin ? glm::make_vec4(&bufferWeights[v * weightByteStride]) : glm::vec4(0.0f);
						// Fix for all zero weights
						if (glm::length(vert.weight0) == 0.0f)
						{
							vert.weight0 = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
						}
						loaderInfo.vertexPos++;
					}
				}
				// Indices
				if (hasIndices)
				{
					const tinygltf::Accessor& accessor = model.accessors[primitive.indices > -1 ? primitive.indices : 0];
					const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
					const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];

					indexCount = static_cast<uint32_t>(accessor.count);
					const void* dataPtr = &(buffer.data[accessor.byteOffset + bufferView.byteOffset]);

					switch (accessor.componentType)
					{
					case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT:
					{
						const uint32_t* buf = static_cast<const uint32_t*>(dataPtr);
						for (size_t index = 0; index < accessor.count; index++)
						{
							loaderInfo.indexBuffer[loaderInfo.indexPos] = buf[index] + vertexStart;
							loaderInfo.indexPos++;
						}
						break;
					}
					case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT:
					{
						const uint16_t* buf = static_cast<const uint16_t*>(dataPtr);
						for (size_t index = 0; index < accessor.count; index++)
						{
							loaderInfo.indexBuffer[loaderInfo.indexPos] = buf[index] + vertexStart;
							loaderInfo.indexPos++;
						}
						break;
					}
					case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE:
					{
						const uint8_t* buf = static_cast<const uint8_t*>(dataPtr);
						for (size_t index = 0; index < accessor.count; index++)
						{
							loaderInfo.indexBuffer[loaderInfo.indexPos] = buf[index] + vertexStart;
							loaderInfo.indexPos++;
						}
						break;
					}
					default:
						std::cerr << "Index component type " << accessor.componentType << " not supported!" << std::endl;
						return;
					}
				}
				Primitive* newPrimitive = new Primitive(indexStart, indexCount, vertexCount/*, primitive.material > -1 ? materials[primitive.material] : materials.back()*/);
				newPrimitive->setBoundingBox(posMin, posMax);
				newMesh->primitives.push_back(newPrimitive);
			}
			// Mesh BB from BBs of primitives
			for (auto p : newMesh->primitives)
			{
				if (p->bb.valid && !newMesh->bb.valid)
				{
					newMesh->bb = p->bb;
					newMesh->bb.valid = true;
				}
				newMesh->bb.min = glm::min(newMesh->bb.min, p->bb.min);
				newMesh->bb.max = glm::max(newMesh->bb.max, p->bb.max);
			}
			newNode->mesh = newMesh;
		}
		if (parent)
		{
			parent->children.push_back(newNode);
		}
		else
		{
			nodes.push_back(newNode);
		}
		linearNodes.push_back(newNode);
	}

	void Model::getNodeProps(const tinygltf::Node& node, const tinygltf::Model& model, size_t& vertexCount, size_t& indexCount)
	{
		if (node.children.size() > 0)
		{
			for (size_t i = 0; i < node.children.size(); i++)
			{
				getNodeProps(model.nodes[node.children[i]], model, vertexCount, indexCount);
			}
		}
		if (node.mesh > -1)
		{
			const tinygltf::Mesh mesh = model.meshes[node.mesh];
			for (size_t i = 0; i < mesh.primitives.size(); i++)
			{
				auto primitive = mesh.primitives[i];
				vertexCount += model.accessors[primitive.attributes.find("POSITION")->second].count;
				if (primitive.indices > -1)
				{
					indexCount += model.accessors[primitive.indices].count;
				}
			}
		}
	}

	void Model::loadSkins(tinygltf::Model& gltfModel)
	{
		for (tinygltf::Skin& source : gltfModel.skins)
		{
			Skin* newSkin = new Skin{};
			newSkin->name = source.name;

			// Find skeleton root node
			if (source.skeleton > -1)
			{
				newSkin->skeletonRoot = nodeFromIndex(source.skeleton);
			}

			// Find joint nodes
			for (int jointIndex : source.joints)
			{
				Node* node = nodeFromIndex(jointIndex);
				if (node)
				{
					newSkin->joints.push_back(nodeFromIndex(jointIndex));
				}
			}

			// Get inverse bind matrices from buffer
			if (source.inverseBindMatrices > -1)
			{
				const tinygltf::Accessor& accessor = gltfModel.accessors[source.inverseBindMatrices];
				const tinygltf::BufferView& bufferView = gltfModel.bufferViews[accessor.bufferView];
				const tinygltf::Buffer& buffer = gltfModel.buffers[bufferView.buffer];
				newSkin->inverseBindMatrices.resize(accessor.count);
				memcpy(newSkin->inverseBindMatrices.data(), &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(glm::mat4));
			}

			skins.push_back(newSkin);
		}
	}

	void Model::loadTextures(tinygltf::Model& gltfModel, VulkanEngine* engine)
	{
		for (tinygltf::Texture& tex : gltfModel.textures)
		{
			tinygltf::Image image = gltfModel.images[tex.source];
			vkglTF::TextureSampler textureSampler;
			if (tex.sampler > -1)
			{
				textureSampler = textureSamplers[tex.sampler];
			}
			else
			{
				// No sampler specified, use a default one
				textureSampler.magFilter = VK_FILTER_LINEAR;
				textureSampler.minFilter = VK_FILTER_LINEAR;
				textureSampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
				textureSampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
				textureSampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			}

			//
			// Load texture
			//
			unsigned char* buffer = nullptr;
			VkDeviceSize bufferSize = 0;
			bool deleteBuffer = false;
			if (image.component == 3)
			{
				// Most devices don't support RGB only on Vulkan so convert
				// TODO: Check actual format support and transform only if required
				bufferSize = image.width * image.height * 4;
				buffer = new unsigned char[bufferSize];
				unsigned char* rgba = buffer;
				unsigned char* rgb = &image.image[0];
				for (int32_t i = 0; i < image.width * image.height; i++)
				{
					for (int32_t j = 0; j < 3; ++j)
					{
						rgba[j] = rgb[j];
					}
					rgba += 4;
					rgb += 3;
				}
				deleteBuffer = true;
			}
			else
			{
				buffer = &image.image[0];
				bufferSize = image.image.size();
			}

			VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;

			Texture texture;
			vkutil::loadImageFromBuffer(*engine, image.width, image.height, bufferSize, format, buffer, 0, texture.image);

			if (deleteBuffer)
				delete[] buffer;

			VkImageViewCreateInfo imageInfo = vkinit::imageviewCreateInfo(format, texture.image._image, VK_IMAGE_ASPECT_COLOR_BIT, texture.image._mipLevels);
			vkCreateImageView(engine->_device, &imageInfo, nullptr, &texture.imageView);

			engine->_mainDeletionQueue.pushFunction([=]() {
				vkDestroyImageView(engine->_device, texture.imageView, nullptr);
				});

			textures.push_back(texture);
		}
	}

	VkSamplerAddressMode Model::getVkWrapMode(int32_t wrapMode)
	{
		switch (wrapMode)
		{
		case 10497:
			return VK_SAMPLER_ADDRESS_MODE_REPEAT;
		case 33071:
			return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		case 33648:
			return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
		}
	}

	VkFilter Model::getVkFilterMode(int32_t filterMode)
	{
		switch (filterMode)
		{
		case 9728:
			return VK_FILTER_NEAREST;
		case 9729:
			return VK_FILTER_LINEAR;
		case 9984:
			return VK_FILTER_NEAREST;
		case 9985:
			return VK_FILTER_NEAREST;
		case 9986:
			return VK_FILTER_LINEAR;
		case 9987:
			return VK_FILTER_LINEAR;
		}
	}

	void Model::loadTextureSamplers(tinygltf::Model& gltfModel)
	{
		for (tinygltf::Sampler smpl : gltfModel.samplers)
		{
			vkglTF::TextureSampler sampler = {
				.magFilter = getVkFilterMode(smpl.magFilter),
				.minFilter = getVkFilterMode(smpl.minFilter),
				.addressModeU = getVkWrapMode(smpl.wrapS),
				.addressModeV = getVkWrapMode(smpl.wrapT),
				.addressModeW = sampler.addressModeV,
			};
			textureSamplers.push_back(sampler);
		}
	}

	//void Model::loadMaterials(tinygltf::Model& gltfModel)
	//{
	//	for (tinygltf::Material& mat : gltfModel.materials)
	//	{
	//		vkglTF::Material material = {};
	//		material.doubleSided = mat.doubleSided;

	//		if (mat.values.find("baseColorTexture") != mat.values.end())
	//		{
	//			material.baseColorTexture = &textures[mat.values["baseColorTexture"].TextureIndex()];
	//			material.texCoordSets.baseColor = mat.values["baseColorTexture"].TextureTexCoord();
	//		}

	//		if (mat.values.find("metallicRoughnessTexture") != mat.values.end())
	//		{
	//			material.metallicRoughnessTexture = &textures[mat.values["metallicRoughnessTexture"].TextureIndex()];
	//			material.texCoordSets.metallicRoughness = mat.values["metallicRoughnessTexture"].TextureTexCoord();
	//		}

	//		if (mat.values.find("roughnessFactor") != mat.values.end())
	//		{
	//			material.roughnessFactor = static_cast<float>(mat.values["roughnessFactor"].Factor());
	//		}

	//		if (mat.values.find("metallicFactor") != mat.values.end())
	//		{
	//			material.metallicFactor = static_cast<float>(mat.values["metallicFactor"].Factor());
	//		}

	//		if (mat.values.find("baseColorFactor") != mat.values.end())
	//		{
	//			material.baseColorFactor = glm::make_vec4(mat.values["baseColorFactor"].ColorFactor().data());
	//		}

	//		if (mat.additionalValues.find("normalTexture") != mat.additionalValues.end())
	//		{
	//			material.normalTexture = &textures[mat.additionalValues["normalTexture"].TextureIndex()];
	//			material.texCoordSets.normal = mat.additionalValues["normalTexture"].TextureTexCoord();
	//		}

	//		if (mat.additionalValues.find("emissiveTexture") != mat.additionalValues.end())
	//		{
	//			material.emissiveTexture = &textures[mat.additionalValues["emissiveTexture"].TextureIndex()];
	//			material.texCoordSets.emissive = mat.additionalValues["emissiveTexture"].TextureTexCoord();
	//		}

	//		if (mat.additionalValues.find("occlusionTexture") != mat.additionalValues.end())
	//		{
	//			material.occlusionTexture = &textures[mat.additionalValues["occlusionTexture"].TextureIndex()];
	//			material.texCoordSets.occlusion = mat.additionalValues["occlusionTexture"].TextureTexCoord();
	//		}

	//		if (mat.additionalValues.find("alphaMode") != mat.additionalValues.end())
	//		{
	//			tinygltf::Parameter param = mat.additionalValues["alphaMode"];
	//			if (param.string_value == "BLEND")
	//			{
	//				material.alphaMode = Material::ALPHAMODE_BLEND;
	//			}
	//			if (param.string_value == "MASK")
	//			{
	//				material.alphaCutoff = 0.5f;
	//				material.alphaMode = Material::ALPHAMODE_MASK;
	//			}
	//		}

	//		if (mat.additionalValues.find("alphaCutoff") != mat.additionalValues.end())
	//		{
	//			material.alphaCutoff = static_cast<float>(mat.additionalValues["alphaCutoff"].Factor());
	//		}

	//		if (mat.additionalValues.find("emissiveFactor") != mat.additionalValues.end())
	//		{
	//			material.emissiveFactor = glm::vec4(glm::make_vec3(mat.additionalValues["emissiveFactor"].ColorFactor().data()), 1.0);
	//		}

	//		// Extensions
	//		// @TODO: Find out if there is a nicer way of reading these properties with recent tinygltf headers
	//		if (mat.extensions.find("KHR_materials_pbrSpecularGlossiness") != mat.extensions.end())
	//		{
	//			auto ext = mat.extensions.find("KHR_materials_pbrSpecularGlossiness");
	//			if (ext->second.Has("specularGlossinessTexture"))
	//			{
	//				auto index = ext->second.Get("specularGlossinessTexture").Get("index");
	//				material.extension.specularGlossinessTexture = &textures[index.Get<int>()];
	//				auto texCoordSet = ext->second.Get("specularGlossinessTexture").Get("texCoord");
	//				material.texCoordSets.specularGlossiness = texCoordSet.Get<int>();
	//				material.pbrWorkflows.specularGlossiness = true;
	//			}
	//			if (ext->second.Has("diffuseTexture"))
	//			{
	//				auto index = ext->second.Get("diffuseTexture").Get("index");
	//				material.extension.diffuseTexture = &textures[index.Get<int>()];
	//			}
	//			if (ext->second.Has("diffuseFactor"))
	//			{
	//				auto factor = ext->second.Get("diffuseFactor");
	//				for (uint32_t i = 0; i < factor.ArrayLen(); i++)
	//				{
	//					auto val = factor.Get(i);
	//					material.extension.diffuseFactor[i] = val.IsNumber() ? (float)val.Get<double>() : (float)val.Get<int>();
	//				}
	//			}
	//			if (ext->second.Has("specularFactor"))
	//			{
	//				auto factor = ext->second.Get("specularFactor");
	//				for (uint32_t i = 0; i < factor.ArrayLen(); i++)
	//				{
	//					auto val = factor.Get(i);
	//					material.extension.specularFactor[i] = val.IsNumber() ? (float)val.Get<double>() : (float)val.Get<int>();
	//				}
	//			}
	//		}

	//		materials.push_back(material);
	//	}
	//	// Push a default material at the end of the list for meshes with no material assigned
	//	materials.push_back(Material());
	//}

	void Model::loadAnimations(tinygltf::Model& gltfModel)
	{
		for (tinygltf::Animation& anim : gltfModel.animations)
		{
			vkglTF::Animation animation{};
			animation.name = anim.name;
			if (anim.name.empty())
			{
				animation.name = std::to_string(animations.size());
			}

			// Samplers
			for (auto& samp : anim.samplers)
			{
				vkglTF::AnimationSampler sampler{};

				if (samp.interpolation == "LINEAR")
				{
					sampler.interpolation = AnimationSampler::InterpolationType::LINEAR;
				}
				if (samp.interpolation == "STEP")
				{
					sampler.interpolation = AnimationSampler::InterpolationType::STEP;
				}
				if (samp.interpolation == "CUBICSPLINE")
				{
					sampler.interpolation = AnimationSampler::InterpolationType::CUBICSPLINE;
				}

				// Read sampler input time values
				{
					const tinygltf::Accessor& accessor = gltfModel.accessors[samp.input];
					const tinygltf::BufferView& bufferView = gltfModel.bufferViews[accessor.bufferView];
					const tinygltf::Buffer& buffer = gltfModel.buffers[bufferView.buffer];

					assert(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);

					const void* dataPtr = &buffer.data[accessor.byteOffset + bufferView.byteOffset];
					const float* buf = static_cast<const float*>(dataPtr);
					for (size_t index = 0; index < accessor.count; index++)
					{
						sampler.inputs.push_back(buf[index]);
					}

					for (auto input : sampler.inputs)
					{
						if (input < animation.start)
						{
							animation.start = input;
						};
						if (input > animation.end)
						{
							animation.end = input;
						}
					}
				}

				// Read sampler output T/R/S values 
				{
					const tinygltf::Accessor& accessor = gltfModel.accessors[samp.output];
					const tinygltf::BufferView& bufferView = gltfModel.bufferViews[accessor.bufferView];
					const tinygltf::Buffer& buffer = gltfModel.buffers[bufferView.buffer];

					assert(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);

					const void* dataPtr = &buffer.data[accessor.byteOffset + bufferView.byteOffset];

					switch (accessor.type)
					{
					case TINYGLTF_TYPE_VEC3:
					{
						const glm::vec3* buf = static_cast<const glm::vec3*>(dataPtr);
						for (size_t index = 0; index < accessor.count; index++)
						{
							sampler.outputsVec4.push_back(glm::vec4(buf[index], 0.0f));
						}
						break;
					}
					case TINYGLTF_TYPE_VEC4:
					{
						const glm::vec4* buf = static_cast<const glm::vec4*>(dataPtr);
						for (size_t index = 0; index < accessor.count; index++)
						{
							sampler.outputsVec4.push_back(buf[index]);
						}
						break;
					}
					default:
					{
						std::cout << "unknown type" << std::endl;
						break;
					}
					}
				}

				animation.samplers.push_back(sampler);
			}

			// Channels
			for (auto& source : anim.channels)
			{
				vkglTF::AnimationChannel channel{};

				if (source.target_path == "rotation")
				{
					channel.path = AnimationChannel::PathType::ROTATION;
				}
				if (source.target_path == "translation")
				{
					channel.path = AnimationChannel::PathType::TRANSLATION;
				}
				if (source.target_path == "scale")
				{
					channel.path = AnimationChannel::PathType::SCALE;
				}
				if (source.target_path == "weights")
				{
					std::cout << "weights not yet supported, skipping channel" << std::endl;
					continue;
				}
				channel.samplerIndex = source.sampler;
				channel.node = nodeFromIndex(source.target_node);
				if (!channel.node)
				{
					continue;
				}

				animation.channels.push_back(channel);
			}

			animations.push_back(animation);
		}
	}

	void Model::loadFromFile(VulkanEngine* engine, std::string filename, float scale)
	{
		auto tStart = std::chrono::high_resolution_clock::now();

		//
		// Load in data from file
		//
		tinygltf::Model gltfModel;
		tinygltf::TinyGLTF gltfContext;

		std::string error;
		std::string warning;

		bool binary = false;
		size_t extpos = filename.rfind('.', filename.length());
		if (extpos != std::string::npos)
			binary = (filename.substr(extpos + 1, filename.length() - extpos) == "glb");

		bool fileLoaded = binary ? gltfContext.LoadBinaryFromFile(&gltfModel, &error, &warning, filename.c_str()) : gltfContext.LoadASCIIFromFile(&gltfModel, &error, &warning, filename.c_str());

		LoaderInfo loaderInfo{ };
		size_t vertexCount = 0;
		size_t indexCount = 0;

		if (!fileLoaded)
		{
			std::cerr << "Could not load gltf file: " << error << std::endl;
			return;
		}

		//
		// Load gltf data into data structures
		//
		loadTextureSamplers(gltfModel);		// @TODO: RE-ENABLE THESE. THESE ARE ALREADY IMPLEMENTED BUT THEY ARE SLOW
		loadTextures(gltfModel, engine);		// @TODO: RE-ENABLE THESE. THESE ARE ALREADY IMPLEMENTED BUT THEY ARE SLOW
		//loadMaterials(gltfModel);

		const tinygltf::Scene& scene = gltfModel.scenes[gltfModel.defaultScene > -1 ? gltfModel.defaultScene : 0];		// TODO: scene handling with no default scene

		// Get vertex and index buffer sizes
		for (size_t i = 0; i < scene.nodes.size(); i++)
			getNodeProps(gltfModel.nodes[scene.nodes[i]], gltfModel, vertexCount, indexCount);

		loaderInfo.vertexBuffer = new Vertex[vertexCount];
		loaderInfo.indexBuffer = new uint32_t[indexCount];

		// Load in vertices and indices
		for (size_t i = 0; i < scene.nodes.size(); i++)
		{
			const tinygltf::Node node = gltfModel.nodes[scene.nodes[i]];
			loadNode(engine, nullptr, node, scene.nodes[i], gltfModel, loaderInfo, scale);
		}

		// Load in animations
		if (gltfModel.animations.size() > 0)
			loadAnimations(gltfModel);
		loadSkins(gltfModel);

		for (auto node : linearNodes)
		{
			// Assign skins
			if (node->skinIndex > -1)
				node->skin = skins[node->skinIndex];
		}

		// Build animation joint matrices calculation taskflow
		_calculateJointMatricesTaskflow.clear();
		for (auto& node : nodes)
		{
			node->generateCalculateJointMatrixTaskflow(_calculateJointMatricesTaskflow, nullptr);
		}
		_taskflowExecutor.run(_calculateJointMatricesTaskflow).wait();    // Calculate Initial Pose

		extensions = gltfModel.extensionsUsed;

		size_t vertexBufferSize = vertexCount * sizeof(Vertex);
		size_t indexBufferSize = indexCount * sizeof(uint32_t);
		indices.count = indexCount;

		assert(vertexBufferSize > 0);

		//
		// Upload vertices and indices to GPU
		//
		AllocatedBuffer vertexStaging, indexStaging;

		// Create staging buffers
		// Vertex data
		vertexStaging =
			engine->createBuffer(
				vertexBufferSize,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VMA_MEMORY_USAGE_CPU_ONLY
			);

		// Copy mesh to vertex staging buffer
		void* data;
		vmaMapMemory(engine->_allocator, vertexStaging._allocation, &data);
		memcpy(data, loaderInfo.vertexBuffer, vertexBufferSize);
		vmaUnmapMemory(engine->_allocator, vertexStaging._allocation);

		// Index data
		if (indexBufferSize > 0)
		{
			indexStaging =
				engine->createBuffer(
					indexBufferSize,
					VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
					VMA_MEMORY_USAGE_CPU_ONLY
				);

			// Copy indices to index staging buffer
			vmaMapMemory(engine->_allocator, indexStaging._allocation, &data);
			memcpy(data, loaderInfo.indexBuffer, indexBufferSize);
			vmaUnmapMemory(engine->_allocator, indexStaging._allocation);
		}

		// Create GPU side buffers
		// Vertex buffer
		AllocatedBuffer vertexGPUSide =
			engine->createBuffer(
				vertexBufferSize,
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VMA_MEMORY_USAGE_GPU_ONLY
			);
		vertices.buffer = vertexGPUSide._buffer;
		vertices.allocation = vertexGPUSide._allocation;

		// Index buffer
		if (indexBufferSize > 0)
		{
			AllocatedBuffer indexGPUSide =
				engine->createBuffer(
					indexBufferSize,
					VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
					VMA_MEMORY_USAGE_GPU_ONLY
				);
			indices.buffer = indexGPUSide._buffer;
			indices.allocation = indexGPUSide._allocation;
		}

		// Copy from staging buffers to GPU
		engine->immediateSubmit([&](VkCommandBuffer cmd)
			{
				VkBufferCopy copyRegion = {
					.srcOffset = 0,
					.dstOffset = 0,
					.size = vertexBufferSize,
				};
				vkCmdCopyBuffer(cmd, vertexStaging._buffer, vertices.buffer, 1, &copyRegion);

				if (indexBufferSize > 0)
				{
					copyRegion.size = indexBufferSize;
					vkCmdCopyBuffer(cmd, indexStaging._buffer, indices.buffer, 1, &copyRegion);
				}
			});

		vmaDestroyBuffer(engine->_allocator, vertexStaging._buffer, vertexStaging._allocation);
		if (indexBufferSize > 0)
			vmaDestroyBuffer(engine->_allocator, indexStaging._buffer, indexStaging._allocation);

		delete[] loaderInfo.vertexBuffer;
		delete[] loaderInfo.indexBuffer;

		getSceneDimensions();

		// Report time it took to load
		auto tEnd = std::chrono::high_resolution_clock::now();
		auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
		std::cout << "[LOAD glTF MODEL FROM FILE]" << std::endl
			<< "filename:           " << filename << std::endl
			<< "meshes:             " << gltfModel.meshes.size() << std::endl
			<< "animations:         " << gltfModel.animations.size() << std::endl
			<< "materials:          " << gltfModel.materials.size() << std::endl
			<< "images:             " << gltfModel.images.size() << std::endl
			<< "total vertices:     " << vertexCount << std::endl
			<< "total indices:      " << indexCount << std::endl
			<< "execution duration: " << tDiff << " ms" << std::endl;
	}

	void Model::bind(VkCommandBuffer commandBuffer)
	{
		const VkDeviceSize offsets[1] = { 0 };
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertices.buffer, offsets);
		vkCmdBindIndexBuffer(commandBuffer, indices.buffer, 0, VK_INDEX_TYPE_UINT32);
	}

	void Model::draw(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, uint32_t transformID)
	{
		for (auto& node : nodes)
		{
			drawNode(node, commandBuffer, pipelineLayout, transformID);
		}
	}

	void Model::drawNode(Node* node, VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, uint32_t transformID)
	{
		if (node->mesh)
		{
			// @TEMPORARY: Bind joint descriptor set
			if (pipelineLayout)
				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 3, 1, &node->mesh->uniformBuffer.descriptorSet, 0, nullptr);

			for (Primitive* primitive : node->mesh->primitives)
			{
				vkCmdDrawIndexed(commandBuffer, primitive->indexCount, 1, primitive->firstIndex, 0, transformID);
			}
		}
		for (auto& child : node->children)
		{
			drawNode(child, commandBuffer, pipelineLayout, transformID);
		}
	}

	void Model::calculateBoundingBox(Node* node, Node* parent)
	{
		BoundingBox parentBvh = parent ? parent->bvh : BoundingBox(dimensions.min, dimensions.max);

		if (node->mesh)
		{
			if (node->mesh->bb.valid)
			{
				node->aabb = node->mesh->bb.getAABB(node->getMatrix());
				if (node->children.size() == 0)
				{
					node->bvh.min = node->aabb.min;
					node->bvh.max = node->aabb.max;
					node->bvh.valid = true;
				}
			}
		}

		parentBvh.min = glm::min(parentBvh.min, node->bvh.min);
		parentBvh.max = glm::min(parentBvh.max, node->bvh.max);

		for (auto& child : node->children)
		{
			calculateBoundingBox(child, node);
		}
	}

	void Model::getSceneDimensions()
	{
		// Calculate binary volume hierarchy for all nodes in the scene
		for (auto node : linearNodes)
		{
			calculateBoundingBox(node, nullptr);
		}

		dimensions.min = glm::vec3(FLT_MAX);
		dimensions.max = glm::vec3(-FLT_MAX);

		for (auto node : linearNodes)
		{
			if (node->bvh.valid)
			{
				dimensions.min = glm::min(dimensions.min, node->bvh.min);
				dimensions.max = glm::max(dimensions.max, node->bvh.max);
			}
		}

		// Calculate scene aabb
		aabb = glm::scale(glm::mat4(1.0f), glm::vec3(dimensions.max[0] - dimensions.min[0], dimensions.max[1] - dimensions.min[1], dimensions.max[2] - dimensions.min[2]));
		aabb[3][0] = dimensions.min[0];
		aabb[3][1] = dimensions.min[1];
		aabb[3][2] = dimensions.min[2];
	}

	void Model::updateAnimation(uint32_t index, float time)
	{
		if (animations.empty())
		{
			std::cout << ".glTF does not contain animation." << std::endl;
			return;
		}
		if (index > static_cast<uint32_t>(animations.size()) - 1)
		{
			std::cout << "No animation with index " << index << std::endl;
			return;
		}

		Animation& animation = animations[index];

		bool updated = false;
		for (auto& channel : animation.channels)
		{
			vkglTF::AnimationSampler& sampler = animation.samplers[channel.samplerIndex];
			if (sampler.inputs.size() > sampler.outputsVec4.size())
			{
				continue;
			}

			for (size_t i = 0; i < sampler.inputs.size() - 1; i++)
			{
				if (time >= sampler.inputs[i] && time <= sampler.inputs[i + 1])
				{
					float u = std::max(0.0f, time - sampler.inputs[i]) / (sampler.inputs[i + 1] - sampler.inputs[i]);
					if (u <= 1.0f)
					{
						switch (channel.path)
						{
						case vkglTF::AnimationChannel::PathType::TRANSLATION:
						{
							glm::vec4 translation = glm::mix(sampler.outputsVec4[i], sampler.outputsVec4[i + 1], u);
							channel.node->translation = glm::vec3(translation);
							break;
						}
						case vkglTF::AnimationChannel::PathType::SCALE:
						{
							glm::vec4 scale = glm::mix(sampler.outputsVec4[i], sampler.outputsVec4[i + 1], u);
							channel.node->scale = glm::vec3(scale);
							break;
						}
						case vkglTF::AnimationChannel::PathType::ROTATION:
						{
							glm::quat q1;
							q1.x = sampler.outputsVec4[i].x;
							q1.y = sampler.outputsVec4[i].y;
							q1.z = sampler.outputsVec4[i].z;
							q1.w = sampler.outputsVec4[i].w;
							glm::quat q2;
							q2.x = sampler.outputsVec4[i + 1].x;
							q2.y = sampler.outputsVec4[i + 1].y;
							q2.z = sampler.outputsVec4[i + 1].z;
							q2.w = sampler.outputsVec4[i + 1].w;
							channel.node->rotation = glm::normalize(glm::slerp(q1, q2, u));    // @NOTE: by using slerp instead of nlerp, you eat tenth's of a millisecond. So take from it what you will. This is more expensive, HOWEVER, I don't know how to implement nlerp correctly atm so that's something to possibly change in the future bc there's a way to do it that I don't really understand  -Timo
							break;
						}
						}
						updated = true;
					}
				}
			}
		}
		if (updated)
		{
			_taskflowExecutor.run(_calculateJointMatricesTaskflow).wait();
		}
	}

	Node* Model::findNode(Node* parent, uint32_t index)
	{
		Node* nodeFound = nullptr;
		if (parent->index == index)
		{
			return parent;
		}
		for (auto& child : parent->children)
		{
			nodeFound = findNode(child, index);
			if (nodeFound)
			{
				break;
			}
		}
		return nodeFound;
	}

	Node* Model::nodeFromIndex(uint32_t index)
	{
		Node* nodeFound = nullptr;
		for (auto& node : nodes)
		{
			nodeFound = findNode(node, index);
			if (nodeFound)
			{
				break;
			}
		}
		return nodeFound;
	}

}
