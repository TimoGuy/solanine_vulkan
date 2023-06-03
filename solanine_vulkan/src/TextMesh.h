#pragma once

#include <vulkan/vulkan.h>
#include "ImportGLM.h"
#include "VkDataStructures.h"

class VulkanEngine;


namespace textmesh
{
    struct Vertex
    {
        vec3 pos;
        vec2 uv;
    };

	// AngelCode .fnt format structs and classes
	struct BMChar
	{
		uint32_t x, y;
		uint32_t width;
		uint32_t height;
		int32_t xoffset;
		int32_t yoffset;
		int32_t xadvance;
		uint32_t page;
	};

	struct TypeFace
	{
		BMChar fontChars[255];
		Texture fontSDFTexture;
		AllocatedBuffer fontSettingsBuffer;
		VkDescriptorSet fontSDFDescriptorSet;
		vec2 textureSize;
	};

	struct TextMesh
	{
		TypeFace* typeFace;
		AllocatedBuffer vertexBuffer;
		AllocatedBuffer indexBuffer;
		uint32_t indexCount = 0;
	};

	void init();
	void cleanup();

	void initPipeline(VulkanEngine* engine, VkViewport& screenspaceViewport, VkRect2D& screenspaceScissor);

	void loadFontSDF(VulkanEngine* engine, std::string sdfTextureFilePath, std::string fontFilePath, std::string fontName);
	TypeFace* getTypeFace(std::string fontName);
	void generateText(VulkanEngine* engine, TextMesh& tm, const TypeFace& tf, std::string text);
	void bindTextFont(VkCommandBuffer cmd, const VkDescriptorSet* globalDescriptor, const TypeFace& tf);
	void renderTextMesh(VkCommandBuffer cmd, const TextMesh& tm, mat4& modelMatrix);
}