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
	struct GPUSDFFontPushConstants
	{
		mat4 modelMatrix;
		float_t renderInScreenspace;
	};

	struct GPUSDFFontSettings
	{
		vec4 outlineColor;
		float_t outlineWidth;
		float_t outline;  // Boolean
	};

	VulkanEngine* engine;

	VkDescriptorSetLayout textMeshSetLayout;
	VkPipeline textMeshPipeline;
	VkPipelineLayout textMeshPipelineLayout;

	std::unordered_map<std::string, TypeFace> fontNameToTypeFace;
	std::vector<TextMesh> textmeshes;


	void init(VulkanEngine* engineRef)
	{
		engine = engineRef;
		textmeshes.reserve(RENDER_OBJECTS_MAX_CAPACITY);  // @NOTE: this protects pointers from going stale if new space needs to be reallocated.
	}

	void cleanup()
	{
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
		blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
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
			{ engine->_globalSetLayout, textMeshSetLayout },
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
			vkinit::depthStencilCreateInfo(false, false, VK_COMPARE_OP_LESS_OR_EQUAL),
			{},
			engine->_mainRenderPass,
			textMeshPipeline,
			textMeshPipelineLayout
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

	void generateTextMeshMesh(TextMesh& tm, TypeFace& tf, std::string text)
	{
		if (tm.indexCount > 0)
		{
			// Cleanup previously created vertex index buffers.
			vmaDestroyBuffer(engine->_allocator, tm.vertexBuffer._buffer, tm.vertexBuffer._allocation);
			vmaDestroyBuffer(engine->_allocator, tm.indexBuffer._buffer, tm.indexBuffer._allocation);
		}

		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		uint32_t indexOffset = 0;

		float_t w = tf.textureSize[0];

		float_t posx = 0.0f;
		float_t posy = 0.0f;

		for (uint32_t i = 0; i < text.size(); i++)
		{
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

			posy = yo;

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
		}
		tm.indexCount = indices.size();
		tm.typeFace = &tf;

		// Center
		for (auto& v : vertices)
		{
			v.pos[0] -= posx / 2.0f;
			v.pos[1] += 0.5f;
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
		generateTextMeshMesh(textmeshes.back(), *tf, text);
		//sortTextMeshesByTypeFace();  // To keep descriptor set switches to a minimum.
		return &textmeshes.back();
	}

	void destroyAndUnregisterTextMesh(TextMesh* tm)
	{
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
		//sortTextMeshesByTypeFace();  // To keep descriptor set switches to a minimum.
	}

	void bindTextFont(VkCommandBuffer cmd, const VkDescriptorSet* globalDescriptor, const TypeFace& tf)
	{
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, textMeshPipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, textMeshPipelineLayout, 0, 1, globalDescriptor, 0, nullptr);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, textMeshPipelineLayout, 1, 1, &tf.fontSDFDescriptorSet, 0, nullptr);
	}

	void renderTextMesh(VkCommandBuffer cmd, TextMesh& tm)
	{
		GPUSDFFontPushConstants pc = {
			.modelMatrix = GLM_MAT4_IDENTITY_INIT,
			.renderInScreenspace = tm.isPositionScreenspace,
		};

		if (tm.isPositionScreenspace)
		{
			// Resize width of all screenspace meshes to keep in line with screen aspect ratio.
			glm_scale(pc.modelMatrix, vec3{ engine->_camera->sceneCamera.aspect, 1.0f, 1.0f });  // @TODO: @RESEARCH: do a ui pass, and when loading everything up just add this into the mix too!
		}
		
		vec3 trans;
		glm_vec3_sub(tm.renderPosition, engine->_camera->sceneCamera.gpuCameraData.cameraPosition, trans);
		glm_translate(pc.modelMatrix, trans);
		mat4 invCameraView;
		glm_mat4_inv(engine->_camera->sceneCamera.gpuCameraData.view, invCameraView);
		glm_mat4_mul(pc.modelMatrix, invCameraView, pc.modelMatrix);
		glm_scale(pc.modelMatrix, vec3{ 0.5f, 0.5f, 0.5f });

		vkCmdPushConstants(cmd, textMeshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUSDFFontPushConstants), &pc);

		const VkDeviceSize offsets[1] = { 0 };
		vkCmdBindVertexBuffers(cmd, 0, 1, &tm.vertexBuffer._buffer, offsets);
		vkCmdBindIndexBuffer(cmd, tm.indexBuffer._buffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(cmd, tm.indexCount, 1, 0, 0, 0);
	}

	void renderTextMeshes(VkCommandBuffer cmd, const VkDescriptorSet* globalDescriptor)
	{
		TypeFace* lastTypeFace = nullptr;
		for (TextMesh& tm : textmeshes)
		{
			if (!tm.isVisible)
				continue;

			if (tm.typeFace != lastTypeFace)
			{
				bindTextFont(cmd, globalDescriptor, *tm.typeFace);
				lastTypeFace = tm.typeFace;
			}

			renderTextMesh(cmd, tm);
		}
	}
}