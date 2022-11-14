#include "RenderObject.h"

#include <iostream>
#include "VkglTFModel.h"


RenderObject* RenderObjectManager::registerRenderObject(RenderObject renderObjectData)
{
    // @NOTE: using a pool system bc of pointers losing information when stuff gets deleted.  -Timo 2022/11/06
	if (_renderObjectsIndices.size() >= RENDER_OBJECTS_MAX_CAPACITY)
	{
		std::cerr << "[REGISTER RENDER OBJECT]" << std::endl
			<< "ERROR: trying to register render object when capacity is at maximum." << std::endl
			<< "       Current capacity: " << _renderObjectsIndices.size() << std::endl
			<< "       Maximum capacity: " << _renderObjectPool.size() << std::endl;
		return nullptr;
	}

	// Find next open spot in pool
	// @COPYPASTA
	size_t registerIndex = 0;
	for (size_t i = 0; i < _renderObjectsIsRegistered.size(); i++)
	{
		if (!_renderObjectsIsRegistered[i])
		{
			registerIndex = i;
			break;
		}
	}

	// Register object
	_renderObjectPool[registerIndex] = renderObjectData;
	_renderObjectsIsRegistered[registerIndex] = true;
	_renderObjectsIndices.push_back(registerIndex);

	recalculateAnimatorIndices();

	return &_renderObjectPool[registerIndex];
}

void RenderObjectManager::unregisterRenderObject(RenderObject* objRegistration)
{
	// @COPYPASTA
	size_t indicesIndex = 0;
	for (size_t poolIndex : _renderObjectsIndices)
	{
		auto& rod = _renderObjectPool[poolIndex];
		if (&rod == objRegistration)
		{
			// Unregister object
			_renderObjectsIsRegistered[poolIndex] = false;
			_renderObjectsIndices.erase(_renderObjectsIndices.begin() + indicesIndex);

			recalculateAnimatorIndices();

			return;
		}

		indicesIndex++;
	}

	std::cerr << "[UNREGISTER RENDER OBJECT]" << std::endl
		<< "ERROR: render object " << objRegistration << " was not found. Nothing unregistered." << std::endl;
}

vkglTF::Model* RenderObjectManager::getModel(const std::string& name)
{
	auto it = _renderObjectModels.find(name);
	if (it == _renderObjectModels.end())
	{
		std::cerr << "[GET MODEL]" << std::endl
			<< "ERROR: requested model \"" << name << "\" was not found. Returning nullptr" << std::endl;
		return nullptr;
	}
	return it->second;
}

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
