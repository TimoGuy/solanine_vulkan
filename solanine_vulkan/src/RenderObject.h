#pragma once

#include "Settings.h"
#include "VkDataStructures.h"

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
	mat4 transformMatrix       = GLM_MAT4_IDENTITY_INIT;  // Gets overwritten if `simTransformId` is set.
	size_t simTransformId      = (size_t)-1;  // -1 means no attached id.
	mat4 simTransformOffset    = GLM_MAT4_IDENTITY_INIT;
	RenderLayer renderLayer    = RenderLayer::VISIBLE;
	std::string attachedEntityGuid;  // @NOTE: this is just for @DEBUG purposes for the imgui property panel
	std::vector<GPUInstancePointer> calculatedModelInstances;
	std::vector<size_t> perPrimitiveUniqueMaterialBaseIndices;
};

class RenderObjectManager
{
public:
	bool registerRenderObjects(std::vector<RenderObject> inRenderObjectDatas, std::vector<RenderObject**> outRenderObjectDatas);
	void unregisterRenderObjects(std::vector<RenderObject*> objRegistrations);

	bool checkIsMetaMeshListUnoptimized();
	void flagMetaMeshListAsUnoptimized();
	void optimizeMetaMeshList();

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

	std::vector<size_t> _renderObjectsWithSimTransformIdIndices;
	std::vector<size_t> _renderObjectsWithAnimatorIndices;
	void recalculateSpecialCaseIndices();
	void updateSimTransforms();
	void updateAnimators(float_t deltaTime);

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

	struct MetaMesh
	{
		vkglTF::Model* model;
		bool isSkinned;
		std::vector<size_t> meshIndices;  // @HACK: this is a vector so that skinned meshes can access all the individual instance references to get the right material etc. THINK OF A BETTER SYSTEM! @TODO.
		size_t uniqueMaterialBaseId;
		std::vector<size_t> renderObjectIndices;
		size_t cookedMeshDrawIdx;
	};
	std::vector<MetaMesh> _metaMeshes;
	std::vector<MeshCapturedInfo> _cookedMeshDraws;

	struct SkinnedMeshEntry
	{
		vkglTF::Model* model;
		size_t meshIdx;
		size_t uniqueMaterialBaseID;
		size_t animatorNodeID;
		size_t baseInstanceID;
	};
	std::vector<SkinnedMeshEntry> _skinnedMeshEntries;
	uint8_t _skinnedMeshModelMemAddr;

	bool _isMetaMeshListUnoptimized = true;

	struct MeshBucket
	{
		std::vector<size_t> renderObjectIndices;
	};
	struct ModelBucket
	{
		MeshBucket* meshBuckets;
	};
	struct ModelBucketSet
	{
		ModelBucket* modelBuckets;
	};
	struct UniqueMaterialBaseBucket
	{
		ModelBucketSet modelBucketSets[2];  // First one is skinned, second is unskinned.
	};
	UniqueMaterialBaseBucket* umbBuckets = nullptr;
	size_t numUmbBuckets = 0;
	size_t numModelBuckets = 0;
	std::vector<size_t> numMeshBucketsByModelIdx;

	VmaAllocator& _allocator;

	friend class VulkanEngine;
};