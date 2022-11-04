#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <glm/glm.hpp>
#include <vma/vk_mem_alloc.h>

namespace vkglTF { class Model; }


enum class RenderLayer
{
	VISIBLE, INVISIBLE, BUILDER
};

struct RenderObject
{
	vkglTF::Model* model;
	glm::mat4 transformMatrix;
	RenderLayer renderLayer;
	std::string attachedEntityGuid;  // @NOTE: this is just for @DEBUG purposes for the imgui property panel
};


constexpr size_t RENDER_OBJECTS_MAX_CAPACITY = 10000;

class RenderObjectManager
{
public:
	RenderObject* registerRenderObject(RenderObject renderObjectData);
	void unregisterRenderObject(RenderObject* objRegistration);
	vkglTF::Model* getModel(const std::string& name);

private:
	RenderObjectManager(VmaAllocator& allocator);
	~RenderObjectManager();

    std::vector<RenderObject> _renderObjects;
	std::vector<bool> _renderObjectLayersEnabled = { true, false, false };

	std::unordered_map<std::string, vkglTF::Model*> _renderObjectModels;
	vkglTF::Model* createModel(vkglTF::Model* model, const std::string& name);

	VmaAllocator& _allocator;

	friend class VulkanEngine;
};