#pragma once

#include <string>
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
		bool isVisible = true;
		mat4 renderTransform = GLM_MAT4_IDENTITY_INIT;
	};

	void init(VulkanEngine* engine);
	void cleanup();

	void initPipeline(VkViewport& screenspaceViewport, VkRect2D& screenspaceScissor);

	void loadFontSDF(std::string sdfTextureFilePath, std::string fontFilePath, std::string fontName);
	TextMesh* createAndRegisterTextMesh(std::string fontName, std::string text);
	void destroyAndUnregisterTextMesh(TextMesh* tm);
	void renderTextMeshes(VkCommandBuffer cmd, const VkDescriptorSet* globalDescriptor);
}