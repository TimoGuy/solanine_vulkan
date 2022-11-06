#include "RenderObject.h"

#include <iostream>
#include "VkglTFModel.h"


RenderObject* RenderObjectManager::registerRenderObject(RenderObject renderObjectData)
{
    // @NOTE: this is required to be here (as well as the .reserve() on init)
	//        bc if the capacity is overcome, then a new array with a larger
	//        capacity is allocated, and then the pointer to the part in the
	//        vector is lost to garbage memory that got deallocated.  -Timo 2022/10/24
	if (_renderObjects.size() >= RENDER_OBJECTS_MAX_CAPACITY)
	{
		std::cerr << "[REGISTER RENDER OBJECT]" << std::endl
			<< "ERROR: trying to register render object when capacity is at maximum." << std::endl
			<< "       Current capacity: " << _renderObjects.size() << std::endl
			<< "       Maximum capacity: " << RENDER_OBJECTS_MAX_CAPACITY << std::endl;
		return nullptr;
	}
	_renderObjects.push_back(renderObjectData);
	recalculateAnimatorIndices();

	return &_renderObjects.back();
}

void RenderObjectManager::unregisterRenderObject(RenderObject* objRegistration)
{
	std::erase_if(_renderObjects,
		[=](RenderObject& x) {
			return &x == objRegistration;
		}
	);
	recalculateAnimatorIndices();
}

vkglTF::Model* RenderObjectManager::getModel(const std::string& name)
{
	auto it = _renderObjectModels.find(name);
	if (it == _renderObjectModels.end())
		return nullptr;
	return it->second;
}

RenderObjectManager::RenderObjectManager(VmaAllocator& allocator) : _allocator(allocator)
{
    _renderObjects.reserve(RENDER_OBJECTS_MAX_CAPACITY);
}

RenderObjectManager::~RenderObjectManager()
{
    for (auto it = _renderObjectModels.begin(); it != _renderObjectModels.end(); it++)
		it->second->destroy(_allocator);
}

void RenderObjectManager::recalculateAnimatorIndices()
{
	_renderObjectsWithAnimatorIndices.clear();
	for (size_t i = 0; i < _renderObjects.size(); i++)
	{
		if (_renderObjects[i].animator == nullptr)
			continue;

		_renderObjectsWithAnimatorIndices.push_back(i);
	}
}

void RenderObjectManager::updateAnimators(const float_t& deltaTime)
{
	// @TODO: make this multithreaded....
	for (size_t& i : _renderObjectsWithAnimatorIndices)
		_renderObjects[i].animator->update(deltaTime);
}

vkglTF::Model* RenderObjectManager::createModel(vkglTF::Model* model, const std::string& name)
{
	// @NOTE: no need to reserve any size of models for this vector, bc we're just giving
	// away the pointer to the model itself instead bc the model is created on the heap
	_renderObjectModels[name] = model;
	return _renderObjectModels[name];  // Ehhh, we could've just sent back the original model pointer
}
