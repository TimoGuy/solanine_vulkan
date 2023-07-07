#include "RenderObject.h"

#include <iostream>
#include "VkglTFModel.h"


bool RenderObjectManager::registerRenderObjects(std::vector<RenderObject> inRenderObjectDatas, std::vector<RenderObject**> outRenderObjectDatas)
{
	// @NOTE: using a pool system bc of pointers losing information when stuff gets deleted.  -Timo 2022/11/06
	// @NOTE: I think past me is talking about when a std::vector gets to a certain capacity, it
	//        has to recreate a new array and insert all of the information into the array. Since
	//        the array contains information that exists on the stack, it has to get moved, breaking
	//        pointers and breaking my heart along the way.  -Timo 2023/05/27
	if (_renderObjectsIndices.size() + inRenderObjectDatas.size() > RENDER_OBJECTS_MAX_CAPACITY)
	{
		std::cerr << "[REGISTER RENDER OBJECT]" << std::endl
			<< "ERROR: trying to register render object when capacity will overflow maximum." << std::endl
			<< "       Current capacity: " << _renderObjectsIndices.size() << std::endl
			<< "       Maximum capacity: " << _renderObjectPool.size() << std::endl
			<< "       Amount requesting to register: " << inRenderObjectDatas.size() << std::endl;
		return false;
	}

	std::lock_guard<std::mutex> lg(renderObjectIndicesAndPoolMutex);

	// Register each render object in the batch.
	for (size_t i = 0; i < inRenderObjectDatas.size(); i++)
	{
		size_t registerIndex = 0;
		for (size_t i = 0; i < _renderObjectsIsRegistered.size(); i++)
		{
			if (!_renderObjectsIsRegistered[i])
			{
				registerIndex = i;
				break;
			}
		}

		// Calculate instance pointers
		RenderObject& renderObjectData = inRenderObjectDatas[i];
		renderObjectData.calculatedModelInstances.clear();
		auto primitives = renderObjectData.model->getAllPrimitivesInOrder();
		for (auto& primitive : primitives)
			renderObjectData.calculatedModelInstances.push_back({
				.objectID = (uint32_t)registerIndex,
				.materialID = primitive->materialID,
				.animatorNodeID =
					(uint32_t)(renderObjectData.animator == nullptr ?
					0 :
					renderObjectData.animator->skinIndexToGlobalReservedNodeIndex(primitive->animatorSkinIndexPropagatedCopy)),
				.voxelFieldLightingGridID = 0,  // @NOTE: the default lightmap is blank 1.0f with identity transform, so set 0 to use the default lightmap.
				});

		// Register object
		_renderObjectPool[registerIndex] = renderObjectData;
		_renderObjectsIsRegistered[registerIndex] = true;
		_renderObjectsIndices.push_back(registerIndex);

		*outRenderObjectDatas[i] = &_renderObjectPool[registerIndex];
	}

	// Sort pool indices so that models are next to each other (helps with model compacting in the rendering stage).
	std::sort(
		_renderObjectsIndices.begin(),
		_renderObjectsIndices.end(),
		[&](size_t a, size_t b) {
			return _renderObjectPool[a].model < _renderObjectPool[b].model;
		}
	);

	for (bool* sendFlag : _sendInstancePtrDataToGPU_refs)
		*sendFlag = true;

	// Recalculate what indices animated render objects are at
	recalculateAnimatorIndices();

	return true;
}

void RenderObjectManager::unregisterRenderObjects(std::vector<RenderObject*> objRegistrations)
{
	std::lock_guard<std::mutex> lg(renderObjectIndicesAndPoolMutex);

	for (RenderObject* objRegistration : objRegistrations)
	{
		// Iterate thru each object registration, unregistering it.
		bool found = false;
		size_t indicesIndex = 0;
		for (size_t poolIndex : _renderObjectsIndices)
		{
			auto& rod = _renderObjectPool[poolIndex];
			if (&rod == objRegistration)
			{
				// Unregister object
				_renderObjectsIsRegistered[poolIndex] = false;
				_renderObjectsIndices.erase(_renderObjectsIndices.begin() + indicesIndex);

				for (bool* sendFlag : _sendInstancePtrDataToGPU_refs)
					*sendFlag = true;

				// Recalculate what indices animated render objects are at
				recalculateAnimatorIndices();

				found = true;
				break;
			}

			indicesIndex++;
		}

		if (!found)
			std::cerr << "[UNREGISTER RENDER OBJECT]" << std::endl
				<< "ERROR: render object " << objRegistration << " was not found. Nothing unregistered." << std::endl;
	}
}

#ifdef _DEVELOP
vkglTF::Model* RenderObjectManager::getModel(const std::string& name, void* owner, std::function<void()>&& reloadCallback)
#else
vkglTF::Model* RenderObjectManager::getModel(const std::string& name)
#endif
{
	auto it = _renderObjectModels.find(name);
	if (it == _renderObjectModels.end())
	{
		std::cerr << "[GET MODEL]" << std::endl
			<< "ERROR: requested model \"" << name << "\" was not found. Returning nullptr" << std::endl;
		return nullptr;
	}

#ifdef _DEVELOP
	ReloadCallback rc = {
		.owner = owner,
		.callback = reloadCallback,
	};
	_renderObjectModelCallbacks[name].push_back(rc);  // Only load in the callback if it's a successful get, and also the lambda could go stale sometime too, so @TODO: there'll have to be a way to remove that lambda
#endif

	return it->second;
}

#ifdef _DEVELOP
void RenderObjectManager::removeModelCallbacks(void* owner)
{
	for (auto it = _renderObjectModelCallbacks.begin(); it != _renderObjectModelCallbacks.end(); it++)
	{
		auto& rcs = it->second;
		std::erase_if(
			rcs,
			[owner](ReloadCallback x) {
				return x.owner == owner;
			}
		);
	}
}

void RenderObjectManager::reloadModelAndTriggerCallbacks(VulkanEngine* engine, const std::string& name, const std::string& modelPath)
{
	auto it = _renderObjectModels.find(name);
	if (it == _renderObjectModels.end())
	{
		std::cerr << "[HOT RELOAD MODEL]" << std::endl
			<< "ERROR: model with name \"" << name << "\" not found." << std::endl;
		return;
	}

	// Reload model
	vkglTF::Model* model = _renderObjectModels[name];
	model->destroy(_allocator);
	model->loadFromFile(engine, modelPath);

	// Trigger Model Callbacks
	for (auto& rc : _renderObjectModelCallbacks[name])
		rc.callback();
}
#endif

RenderObjectManager::RenderObjectManager(VmaAllocator& allocator) : _allocator(allocator)
{
}

RenderObjectManager::~RenderObjectManager()
{
    for (auto it = _renderObjectModels.begin(); it != _renderObjectModels.end(); it++)
		it->second->destroy(_allocator);
}

void RenderObjectManager::recalculateAnimatorIndices()
{
	_renderObjectsWithAnimatorIndices.clear();
	for (size_t poolIndex : _renderObjectsIndices)
	{
		if (_renderObjectPool[poolIndex].animator == nullptr)
			continue;

		_renderObjectsWithAnimatorIndices.push_back(poolIndex);
	}
}

void RenderObjectManager::updateAnimators(const float_t& deltaTime)
{
	// @TODO: make this multithreaded....
	for (size_t& i : _renderObjectsWithAnimatorIndices)
		_renderObjectPool[i].animator->update(deltaTime);
}

vkglTF::Model* RenderObjectManager::createModel(vkglTF::Model* model, const std::string& name)
{
	// @NOTE: no need to reserve any size of models for this vector, bc we're just giving
	// away the pointer to the model itself instead bc the model is created on the heap
	_renderObjectModels[name] = model;
	return _renderObjectModels[name];  // Ehhh, we could've just sent back the original model pointer
}
