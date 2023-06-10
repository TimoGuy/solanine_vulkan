#include "TextMesh.h"

#include <fstream>
#include <sstream>
#include <array>
#include <vector>
#include <algorithm>
#include <assert.h>
#include "VulkanEngine.h"  // @TODO: work to just include the forward declaration of the createBuffer function
#include "VkTextures.h"
#include "VkInitializers.h"
#include "VkDescriptorBuilderUtil.h"
#include "VkPipelineBuilderUtil.h"
#include "Camera.h"


namespace textmesh
{
	struct GPUUICamera
	{
		mat4 projectionView;
		mat4 screenspaceOrthoView;
	} gpuUICamera;
	AllocatedBuffer gpuUICameraBuffer;
	VkDescriptorSet gpuUICameraDescriptorSet;
	VkDescriptorSetLayout gpuUICameraSetLayout;

	VulkanEngine* engine;

	VkDescriptorSetLayout textMeshSetLayout;
	VkPipeline textMeshPipeline = VK_NULL_HANDLE;
	VkPipelineLayout textMeshPipelineLayout;

	std::unordered_map<std::string, TypeFace> fontNameToTypeFace;
	std::vector<TextMesh> textmeshes;


	void init(VulkanEngine* engineRef)
	{
		engine = engineRef;
		textmeshes.reserve(RENDER_OBJECTS_MAX_CAPACITY);  // @NOTE: this protects pointers from going stale if new space needs to be reallocated.

		// Create descriptor for ui camera data
		gpuUICameraBuffer = engine->createBuffer(sizeof(GPUUICamera), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

		VkDescriptorBufferInfo uiCameraBufferInfo = {
			.buffer = gpuUICameraBuffer._buffer,
			.offset = 0,
			.range = sizeof(GPUUICamera),
		};

		vkutil::DescriptorBuilder::begin()
			.bindBuffer(0, &uiCameraBufferInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
			.build(gpuUICameraDescriptorSet, gpuUICameraSetLayout);
	}

	void cleanup()
	{
		vmaDestroyBuffer(engine->_allocator, gpuUICameraBuffer._buffer, gpuUICameraBuffer._allocation);

		// Destroy any lingering allocated buffers
		for (auto& tm : textmeshes)
		{
			if (tm.indexCount > 0)
			{
				// Cleanup previously created vertex index buffers.
				vmaDestroyBuffer(engine->_allocator, tm.vertexBuffer._buffer, tm.vertexBuffer._allocation);
				vmaDestroyBuffer(engine->_allocator, tm.indexBuffer._buffer, tm.indexBuffer._allocation);
			}
		}

		// Destroy all typefaces
		for (auto it = fontNameToTypeFace.begin(); it != fontNameToTypeFace.end(); it++)
		{
			auto& tf = it->second;

			// Texture     @NOTE: images are already destroyed and handled by VkTextures.h/.cpp so only the sampler and imageview get destroyed here.
			vkDestroySampler(engine->_device, tf.fontSDFTexture.sampler, nullptr);
			vkDestroyImageView(engine->_device, tf.fontSDFTexture.imageView, nullptr);

			// Buffer
			vmaDestroyBuffer(engine->_allocator, tf.fontSettingsBuffer._buffer, tf.fontSettingsBuffer._allocation);
		}

		// Destroy pipeline
		vkDestroyPipeline(engine->_device, textMeshPipeline, nullptr);  // @NOTE: pipelinelayouts are already destroyed and handled by VkPipelineBuilderUtil.h/.cpp

		// @NOTE: the descriptorpool gets destroyed automatically, so individual descriptorsets don't have to get destroyed
	}

	void initPipeline(VkViewport& screenspaceViewport, VkRect2D& screenspaceScissor)
	{
		if (textMeshPipeline != VK_NULL_HANDLE)
			vkDestroyPipeline(engine->_device, textMeshPipeline, nullptr);

		// Setup vertex descriptions
		VkVertexInputAttributeDescription posAttribute = {
			.location = 0,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(Vertex, pos),
		};
		VkVertexInputAttributeDescription uvAttribute = {
			.location = 1,
			.binding = 0,
			.format = VK_FORMAT_R32G32_SFLOAT,
			.offset = offsetof(Vertex, uv),
		};
		std::vector<VkVertexInputAttributeDescription> attributes = { posAttribute, uvAttribute };

		VkVertexInputBindingDescription mainBinding = {
			.binding = 0,
			.stride = sizeof(Vertex),
			.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
		};
		std::vector<VkVertexInputBindingDescription> bindings = { mainBinding };

		// Setup color blend attachment state
		VkPipelineColorBlendAttachmentState blendAttachmentState = vkinit::colorBlendAttachmentState();
		blendAttachmentState.blendEnable = VK_TRUE;
		blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
		blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;

		// Build pipeline
		vkutil::pipelinebuilder::build(
			{
				VkPushConstantRange{
					.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
					.offset = 0,
					.size = sizeof(GPUSDFFontPushConstants)
				}
			},
			{ gpuUICameraSetLayout, textMeshSetLayout },
			{
				{ VK_SHADER_STAGE_VERTEX_BIT, "shader/sdf.vert.spv" },
				{ VK_SHADER_STAGE_FRAGMENT_BIT, "shader/sdf.frag.spv" },
			},
			attributes,
			bindings,
			vkinit::inputAssemblyCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
			screenspaceViewport,
			screenspaceScissor,
			vkinit::rasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT),
			{ blendAttachmentState },
			vkinit::multisamplingStateCreateInfo(),
			vkinit::depthStencilCreateInfo(false, false, VK_COMPARE_OP_NEVER),
			{},
			engine->_uiRenderPass,
			textMeshPipeline,
			textMeshPipelineLayout
			);

		// Recreate ui ortho projview
		float_t ratio = screenspaceViewport.width / screenspaceViewport.height;
		float_t width = 1080.0f * ratio;
		float_t height = 1080.0f;
		glm_ortho(
			-width * 0.5f,   width * 0.5f,
			 height * 0.5f, -height * 0.5f,
			screenspaceViewport.minDepth, screenspaceViewport.maxDepth,
			gpuUICamera.screenspaceOrthoView
		);
	}

	int32_t nextValuePair(std::stringstream* stream)
	{
		std::string pair;
		*stream >> pair;
		uint32_t spos = pair.find("=");
		std::string value = pair.substr(spos + 1);
		int32_t val = std::stoi(value);
		return val;
	}

	// Basic parser for AngelCode bitmap font format files
	// See http://www.angelcode.com/products/bmfont/doc/file_format.html for details
	void parsebmFont(TypeFace& tf, std::string filePath)
	{
		std::filebuf fileBuffer;
		fileBuffer.open(filePath, std::ios::in);
		std::istream istream(&fileBuffer);

		assert(istream.good());

		while (!istream.eof())
		{
			std::string line;
			std::stringstream lineStream;
			std::getline(istream, line);
			lineStream << line;

			std::string info;
			lineStream >> info;

			if (info == "char")
			{
				// Char properties
				uint32_t charid = nextValuePair(&lineStream);
				tf.fontChars[charid].x = nextValuePair(&lineStream);
				tf.fontChars[charid].y = nextValuePair(&lineStream);
				tf.fontChars[charid].width = nextValuePair(&lineStream);
				tf.fontChars[charid].height = nextValuePair(&lineStream);
				tf.fontChars[charid].xoffset = nextValuePair(&lineStream);
				tf.fontChars[charid].yoffset = nextValuePair(&lineStream);
				tf.fontChars[charid].xadvance = nextValuePair(&lineStream);
				tf.fontChars[charid].page = nextValuePair(&lineStream);

				if (tf.fontChars[charid].width == 0)
					tf.fontChars[charid].width = 36;
			}
		}
	}

	void loadFontSDF(std::string sdfTextureFilePath, std::string fontFilePath, std::string fontName)
	{
		TypeFace tf;
		parsebmFont(tf, fontFilePath);

		// Load Font Texture
		int32_t texWidth, texHeight;
		vkutil::loadImageFromFile(*engine, sdfTextureFilePath.c_str(), VK_FORMAT_R8G8B8A8_UNORM, 0, texWidth, texHeight, tf.fontSDFTexture.image);

		VkImageViewCreateInfo imageViewInfo = vkinit::imageviewCreateInfo(VK_FORMAT_R8G8B8A8_UNORM, tf.fontSDFTexture.image._image, VK_IMAGE_ASPECT_COLOR_BIT, tf.fontSDFTexture.image._mipLevels);
		vkCreateImageView(engine->_device, &imageViewInfo, nullptr, &tf.fontSDFTexture.imageView);

		VkSamplerCreateInfo samplerInfo = vkinit::samplerCreateInfo(static_cast<float_t>(tf.fontSDFTexture.image._mipLevels), VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
		vkCreateSampler(engine->_device, &samplerInfo, nullptr, &tf.fontSDFTexture.sampler);

		tf.textureSize[0] = (float_t)texWidth;
		tf.textureSize[1] = (float_t)texHeight;

		// Upload font settings
		GPUSDFFontSettings fontSettings = {  // @HARDCODE: for now it's default settings only, but catch me!
			.outlineColor = { 26 / 255.0f, 102 / 255.0f, 50 / 255.0f, 0.0f },
			.outlineWidth = 0.6f,
			.outline = (float_t)true,
		};
		tf.fontSettingsBuffer = engine->createBuffer(sizeof(GPUSDFFontSettings), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

		void* data;
		vmaMapMemory(engine->_allocator, tf.fontSettingsBuffer._allocation, &data);
		memcpy(data, &fontSettings, sizeof(GPUSDFFontSettings));
		vmaUnmapMemory(engine->_allocator, tf.fontSettingsBuffer._allocation);

		// Create descriptor set
		VkDescriptorImageInfo descriptorImageInfo = {
			.sampler = tf.fontSDFTexture.sampler,
			.imageView = tf.fontSDFTexture.imageView,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};

		VkDescriptorBufferInfo fontSettingsBufferInfo = {
			.buffer = tf.fontSettingsBuffer._buffer,
			.offset = 0,
			.range = sizeof(GPUSDFFontSettings),
		};

		vkutil::DescriptorBuilder::begin()
			.bindImage(0, &descriptorImageInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
			.bindBuffer(1, &fontSettingsBufferInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
			.build(tf.fontSDFDescriptorSet, textMeshSetLayout);

		// Add font to font dict
		fontNameToTypeFace[fontName] = tf;
	}

	TypeFace* getTypeFace(std::string fontName)
	{
		if (fontNameToTypeFace.find(fontName) != fontNameToTypeFace.end())
			return &fontNameToTypeFace[fontName];
		return nullptr;
	}

	void generateTextMeshMesh(TextMesh& tm, std::string text)
	{
		TypeFace& tf = *tm.typeFace;

		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		uint32_t indexOffset = 0;

		float_t w = tf.textureSize[0];

		float_t posx = 0.0f;
		float_t posy = 0.0f;

		uint32_t numLines = 1;
		float_t width = 0.0f;

		for (uint32_t i = 0; i < text.size(); i++)
		{
			if (text[i] == '\n')
			{
				// Newline (align left)
				numLines++;
				posx = 0.0f;
				continue;
			}

			const BMChar* charInfo = &tf.fontChars[(int32_t)text[i]];

			float_t charw = ((float_t)(charInfo->width) / 36.0f);
			float_t dimx = 1.0f * charw;
			float_t charh = ((float_t)(charInfo->height) / 36.0f);
			float_t dimy = 1.0f * charh;

			float_t us = charInfo->x / w;
			float_t ue = (charInfo->x + charInfo->width) / w;
			float_t ts = charInfo->y / w;
			float_t te = (charInfo->y + charInfo->height) / w;

			float_t xo = charInfo->xoffset / 36.0f;
			float_t yo = charInfo->yoffset / 36.0f;

			posy = yo + (numLines - 1) * 1.0f;

			vertices.push_back({ { posx + dimx + xo,  -posy - dimy, 0.0f }, { ue, te } });
			vertices.push_back({ { posx + xo,         -posy - dimy, 0.0f }, { us, te } });
			vertices.push_back({ { posx + xo,         -posy,        0.0f }, { us, ts } });
			vertices.push_back({ { posx + dimx + xo,  -posy,        0.0f }, { ue, ts } });

			std::array<uint32_t, 6> letterIndices = { 2,1,0, 0,3,2 };
			for (auto& index : letterIndices)
			{
				indices.push_back(indexOffset + index);
			}
			indexOffset += 4;

			float_t advance = ((float_t)(charInfo->xadvance) / 36.0f);
			posx += advance;
			width = std::max(width, posx);
		}

		// Don't attempt to generate buffers with size 0
		// (@NOTE: VK_ERROR_INITIALIZATION_FAILED was given when attempting this.)
		if (indices.size() == 0)
			return;

		vkDeviceWaitIdle(engine->_device);  // @FIXME: This is an issue when accessed by different threads

		// Cleanup previously created vertex index buffers.
		if (tm.indexCount > 0)
		{
			vmaDestroyBuffer(engine->_allocator, tm.vertexBuffer._buffer, tm.vertexBuffer._allocation);
			vmaDestroyBuffer(engine->_allocator, tm.indexBuffer._buffer, tm.indexBuffer._allocation);
		}
		tm.indexCount = indices.size();

		// Center
		for (auto& v : vertices)
		{
			v.pos[0] -= width / 2.0f;
			v.pos[1] += (float_t)numLines * 0.5f;
		}

		// Generate host accessible buffers for the text vertices and indices and upload the data
		// @TODO: make vertex and index buffers easier to create and upload data to (i.e. create a lib helper function)
		size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
		AllocatedBuffer vertexStaging =
			engine->createBuffer(
				vertexBufferSize,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VMA_MEMORY_USAGE_CPU_ONLY
			);
		tm.vertexBuffer =
			engine->createBuffer(
				vertexBufferSize,
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VMA_MEMORY_USAGE_GPU_ONLY
			);

		size_t indexBufferSize = indices.size() * sizeof(uint32_t);
		AllocatedBuffer indexStaging =
			engine->createBuffer(
				indexBufferSize,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VMA_MEMORY_USAGE_CPU_ONLY
			);
		tm.indexBuffer =
			engine->createBuffer(
				indexBufferSize,
				VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VMA_MEMORY_USAGE_GPU_ONLY
			);

		// Copy vertices and indices to cpu-side buffers
		void* data;
		vmaMapMemory(engine->_allocator, vertexStaging._allocation, &data);
		memcpy(data, vertices.data(), vertexBufferSize);
		vmaUnmapMemory(engine->_allocator, vertexStaging._allocation);
		vmaMapMemory(engine->_allocator, indexStaging._allocation, &data);
		memcpy(data, indices.data(), indexBufferSize);
		vmaUnmapMemory(engine->_allocator, indexStaging._allocation);

		// Transfer cpu-side staging buffers to gpu-side buffers
		engine->immediateSubmit([&](VkCommandBuffer cmd) {
			VkBufferCopy copyRegion = {
				.srcOffset = 0,
				.dstOffset = 0,
				.size = vertexBufferSize,
			};
			vkCmdCopyBuffer(cmd, vertexStaging._buffer, tm.vertexBuffer._buffer, 1, &copyRegion);

			copyRegion.size = indexBufferSize;
			vkCmdCopyBuffer(cmd, indexStaging._buffer, tm.indexBuffer._buffer, 1, &copyRegion);
			});

		// Destroy staging buffers
		vmaDestroyBuffer(engine->_allocator, vertexStaging._buffer, vertexStaging._allocation);
		vmaDestroyBuffer(engine->_allocator, indexStaging._buffer, indexStaging._allocation);
	}

	void sortTextMeshesByTypeFace()
	{
		std::sort(
			textmeshes.begin(),
			textmeshes.end(),
			[&](TextMesh& a, TextMesh& b) {
				return a.typeFace < b.typeFace;
			}
		);
	}

	TextMesh* createAndRegisterTextMesh(std::string fontName, std::string text)
	{
		if (textmeshes.size() >= RENDER_OBJECTS_MAX_CAPACITY)
		{
			std::cerr << "ERROR: New text mesh cannot be created because textmesh list is at capacity (" << RENDER_OBJECTS_MAX_CAPACITY << ")" << std::endl;
			return nullptr;
		}
		textmeshes.push_back(TextMesh());
		TypeFace* tf = getTypeFace(fontName);
		textmeshes.back().typeFace = tf;

		generateTextMeshMesh(textmeshes.back(), text);
		sortTextMeshesByTypeFace();  // To keep descriptor set switches to a minimum.
		return &textmeshes.back();
	}

	void destroyAndUnregisterTextMesh(TextMesh* tm)
	{
		vkDeviceWaitIdle(engine->_device);
		std::erase_if(textmeshes, [&](TextMesh& tml) {
			if (&tml == tm)
			{
				if (tml.indexCount > 0)
				{
					// Cleanup previously created vertex index buffers.
					vmaDestroyBuffer(engine->_allocator, tml.vertexBuffer._buffer, tml.vertexBuffer._allocation);
					vmaDestroyBuffer(engine->_allocator, tml.indexBuffer._buffer, tml.indexBuffer._allocation);
				}
				return true;
			}
			return false;
			});
		sortTextMeshesByTypeFace();  // To keep descriptor set switches to a minimum.
	}

	void regenerateTextMeshMesh(TextMesh* tm, std::string text)
	{
		generateTextMeshMesh(*tm, text);
	}

	void uploadUICameraDataToGPU()
	{
		// Keep UI camera data up to date with main camera.
		// @NOTE: since this buffer isn't double buffered, it will desync as far as the projectionView matrix (for debug stuff afaik), but the ortho projection should be just fine.
		glm_mat4_copy(engine->_camera->sceneCamera.gpuCameraData.projectionView, gpuUICamera.projectionView);

		void* data;
		vmaMapMemory(engine->_allocator, gpuUICameraBuffer._allocation, &data);
		memcpy(data, &gpuUICamera, sizeof(GPUUICamera));
		vmaUnmapMemory(engine->_allocator, gpuUICameraBuffer._allocation);
	}

	void bindTextFont(VkCommandBuffer cmd, const TypeFace& tf)
	{
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, textMeshPipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, textMeshPipelineLayout, 0, 1, &gpuUICameraDescriptorSet, 0, nullptr);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, textMeshPipelineLayout, 1, 1, &tf.fontSDFDescriptorSet, 0, nullptr);
	}

	void renderTextMesh(VkCommandBuffer cmd, TextMesh& tm, bool bindFont)
	{
		if (bindFont)
			bindTextFont(cmd, *tm.typeFace);

		if (tm.indexCount == 0)
			return;  // Don't try to render if there is nothing to render.

		GPUSDFFontPushConstants pc = {
			.modelMatrix = GLM_MAT4_IDENTITY_INIT,
			.renderInScreenspace = (float_t)tm.isPositionScreenspace,
		};
		
		if (tm.isPositionScreenspace)
		{
			glm_translate(pc.modelMatrix, tm.renderPosition);
		}
		else
		{
			vec3 trans;
			glm_vec3_sub(tm.renderPosition, engine->_camera->sceneCamera.gpuCameraData.cameraPosition, trans);
			glm_translate(pc.modelMatrix, trans);

			mat4 invCameraView;
			glm_mat4_inv(engine->_camera->sceneCamera.gpuCameraData.view, invCameraView);
			glm_mat4_mul(pc.modelMatrix, invCameraView, pc.modelMatrix);
		}

		glm_scale(pc.modelMatrix, vec3{ tm.scale, tm.scale, tm.scale });

		vkCmdPushConstants(cmd, textMeshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUSDFFontPushConstants), &pc);

		const VkDeviceSize offsets[1] = { 0 };
		vkCmdBindVertexBuffers(cmd, 0, 1, &tm.vertexBuffer._buffer, offsets);
		vkCmdBindIndexBuffer(cmd, tm.indexBuffer._buffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(cmd, tm.indexCount, 1, 0, 0, 0);
	}

	void renderTextMeshesBulk(VkCommandBuffer cmd)
	{
		TypeFace* lastTypeFace = nullptr;
		for (TextMesh& tm : textmeshes)
		{
			if (tm.excludeFromBulkRender)
				continue;

			bool bindFont = false;
			if (tm.typeFace != lastTypeFace)
			{
				bindFont = true;
				lastTypeFace = tm.typeFace;
			}
			renderTextMesh(cmd, tm, bindFont);
		}
	}
}