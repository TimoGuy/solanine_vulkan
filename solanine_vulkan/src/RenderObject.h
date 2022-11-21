#pragma once

#include <vector>
#include <array>
#include <string>
#include <unordered_map>
#include <vma/vk_mem_alloc.h>
#include "ImportGLM.h"
#include "Settings.h"

namespace vkglTF { struct Model; struct Animator; }


enum class RenderLayer
{
	VISIBLE, INVISIBLE, BUILDER
};

struct RenderObject
{
	vkglTF::Model* model       = nullptr;
	vkglTF::Animator* animator = nullptr;
	glm::mat4 transformMatrix  = glm::mat4(1.0f);
	RenderLayer renderLayer    = RenderLayer::VISIBLE;
	std::string attachedEntityGuid;  // @NOTE: this is just for @DEBUG purposes for the imgui property panel
};

class RenderObjectManager
{
public:
	RenderObject* registerRenderObject(RenderObject renderObjectData);
	void unregisterRenderObject(RenderObject* objRegistration);
	vkglTF::Model* getModel(const std::string& name);

private:
	RenderObjectManager(VmaAllocator& allocator);
	~RenderObjectManager();

	std::vector<size_t> _renderObjectsWithAnimatorIndices;
	void recalculateAnimatorIndices();
	void updateAnimators(const float_t& deltaTime);

	std::vector<size_t>                                   _renderObjectsIndices;
    std::array<bool,         RENDER_OBJECTS_MAX_CAPACITY> _renderObjectsIsRegistered;  // @NOTE: this will be filled with `false` on init  (https://stackoverflow.com/questions/67648693/safely-initializing-a-stdarray-of-bools)
    std::array<RenderObject, RENDER_OBJECTS_MAX_CAPACITY> _renderObjectPool;
	std::vector<bool>                                     _renderObjectLayersEnabled = { true, false, true };

	std::unordered_map<std::string, vkglTF::Model*> _renderObjectModels;
	vkglTF::Model* createModel(vkglTF::Model* model, const std::string& name);

	VmaAllocator& _allocator;

	friend class VulkanEngine;
};