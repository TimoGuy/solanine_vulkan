#pragma once

#include <vector>
#include <array>
#include <string>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <vma/vk_mem_alloc.h>
#include "ImportGLM.h"
#include "Settings.h"

namespace vkglTF { struct Model; struct Animator; }

#ifdef _DEVELOP
class VulkanEngine;
#endif


struct GPUInstancePointer
{
	uint32_t objectID;
	uint32_t materialID;
	uint32_t animatorNodeID;
	uint32_t voxelFieldLightingGridID;
};

enum class RenderLayer
{
	VISIBLE, INVISIBLE, BUILDER
};

struct RenderObject
{
	vkglTF::Model* model       = nullptr;
	vkglTF::Animator* animator = nullptr;
	mat4 transformMatrix       = GLM_MAT4_IDENTITY_INIT;
	RenderLayer renderLayer    = RenderLayer::VISIBLE;
	std::string attachedEntityGuid;  // @NOTE: this is just for @DEBUG purposes for the imgui property panel
	std::vector<GPUInstancePointer> calculatedModelInstances;
};

class RenderObjectManager
{
public:
	bool registerRenderObjects(std::vector<RenderObject> inRenderObjectDatas, std::vector<RenderObject**> outRenderObjectDatas);
	void unregisterRenderObjects(std::vector<RenderObject*> objRegistrations);

#ifdef _DEVELOP
	vkglTF::Model* getModel(const std::string& name, void* owner, std::function<void()>&& reloadCallback);  // This is to support model hot-reloading via a callback lambda
	void           removeModelCallbacks(void* owner);
	void           reloadModelAndTriggerCallbacks(VulkanEngine* engine, const std::string& name, const std::string& modelPath);
#else
	vkglTF::Model* getModel(const std::string& name);
#endif

private:
	RenderObjectManager(VmaAllocator& allocator);
	~RenderObjectManager();

	std::mutex renderObjectIndicesAndPoolMutex;  // For locking control of the render object pool and indices.

	std::vector<bool*> _sendInstancePtrDataToGPU_refs;

	std::vector<size_t> _renderObjectsWithAnimatorIndices;
	void recalculateAnimatorIndices();
	void updateAnimators(const float_t& deltaTime);

	std::vector<size_t>                                   _renderObjectsIndices;
    std::array<bool,         RENDER_OBJECTS_MAX_CAPACITY> _renderObjectsIsRegistered;  // @NOTE: this will be filled with `false` on init  (https://stackoverflow.com/questions/67648693/safely-initializing-a-stdarray-of-bools)
    std::array<RenderObject, RENDER_OBJECTS_MAX_CAPACITY> _renderObjectPool;
	bool*                                                 _renderObjectLayersEnabled = new bool[] { true, false, true };

	std::unordered_map<std::string, vkglTF::Model*>       _renderObjectModels;
#ifdef _DEVELOP
	struct ReloadCallback
	{
		void* owner;  // @NOTE: the `owner` void ptr is used to reference which callbacks need to be deleted
		std::function<void()> callback;
	};
	std::unordered_map<std::string, std::vector<ReloadCallback>> _renderObjectModelCallbacks;
#endif
	vkglTF::Model* createModel(vkglTF::Model* model, const std::string& name);

	VmaAllocator& _allocator;

	friend class VulkanEngine;
};