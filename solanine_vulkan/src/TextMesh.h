#pragma once

#include <vulkan/vulkan.h>
#include "ImportGLM.h"
#include "VkDataStructures.h"


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
		VkDescriptorSet fontSDFTexture;
		vec2 textureSize;
	};

	struct TextMesh
	{
		TypeFace* typeFace;
		AllocatedBuffer vertexBuffer;
		AllocatedBuffer indexBuffer;
		uint32_t indexCount = 0;
	};

	void init(VkDevice device);
	void cleanup();

	void parsebmFont(TypeFace& tf, std::string filePath);
	void loadFontSDFTexture(TypeFace& tf, std::string filePath);
}