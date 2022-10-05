#pragma once

#include "Imports.h"


struct VertexInputDescription
{
	std::vector<VkVertexInputBindingDescription> bindings;
	std::vector<VkVertexInputAttributeDescription> attributes;
	VkPipelineVertexInputStateCreateFlags flags = 0;
};

struct Vertex
{
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec3 color;
	glm::vec2 uv;
	static VertexInputDescription getVertexDescription();
};

struct Mesh
{
	std::vector<Vertex> _vertices;
	std::vector<uint32_t> _indices;
	bool _hasIndices;
	AllocatedBuffer _vertexBuffer;
	AllocatedBuffer _indexBuffer;
};
