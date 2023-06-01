#include "TextMesh.h"

#include <fstream>
#include <string>
#include <sstream>
#include <array>
#include <vector>
#include <assert.h>
#include "VulkanEngine.h"  // @TODO: work to just include the forward declaration of the createBuffer function


namespace textmesh
{
	VkDevice device;

	void init(VkDevice newDevice)
	{
		device = newDevice;
	}

	void cleanup()
	{
		// @TODO
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

	void loadFontSDFTexture(TypeFace& tf, std::string filePath)
	{
		tf.fontSDFTexture = {};
		tf.textureSize = {};
	}

	void generateText(VulkanEngine* engine, TextMesh& tm, const TypeFace& tf, std::string text)
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

			vertices.push_back({ { posx + dimx + xo,  posy + dimy, 0.0f }, { ue, te } });
			vertices.push_back({ { posx + xo,         posy + dimy, 0.0f }, { us, te } });
			vertices.push_back({ { posx + xo,         posy,        0.0f }, { us, ts } });
			vertices.push_back({ { posx + dimx + xo,  posy,        0.0f }, { ue, ts } });

			std::array<uint32_t, 6> letterIndices = { 0,1,2, 2,3,0 };
			for (auto& index : letterIndices)
			{
				indices.push_back(indexOffset + index);
			}
			indexOffset += 4;

			float_t advance = ((float_t)(charInfo->xadvance) / 36.0f);
			posx += advance;
		}
		tm.indexCount = indices.size();

		// Center
		for (auto& v : vertices)
		{
			v.pos[0] -= posx / 2.0f;
			v.pos[1] -= 0.5f;
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
				vertices.size() * sizeof(Vertex),
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
}