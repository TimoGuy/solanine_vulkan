#include "pch.h"

#include "RenderObject.h"

#include "VkglTFModel.h"
#include "MaterialOrganizer.h"
#include "PhysicsEngine.h"


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
		renderObjectData.perPrimitiveUniqueMaterialBaseIndices.clear();

		auto primitives = renderObjectData.model->getAllPrimitivesInOrder();
		for (auto& primitive : primitives)
		{
			// @NOTE: Welcome to erroring out right here! This means that a model got consumed that
			//        does not have a proper material. Go back to the model and re-export with the settings
			//        Geometry>Materials set to "Placeholder", and Geometry>Images set to "None". Also, make sure
			//        there is actually a material assigned to all the faces. Thanks!  -Timo 2023/12/2
			std::string derivedMatName = "missing_material";
			if (primitive->materialID != (uint32_t)-1 &&
				primitive->materialID < renderObjectData.model->materials.size() &&
				materialorganizer::checkDerivedMaterialNameExists(renderObjectData.model->materials[primitive->materialID].name + ".hderriere"))
				derivedMatName = renderObjectData.model->materials[primitive->materialID].name;

			renderObjectData.calculatedModelInstances.push_back({
				.objectID = (uint32_t)registerIndex,
				.materialID = (uint32_t)materialorganizer::derivedMaterialNameToDMPSIdx(derivedMatName + ".hderriere"),
				.animatorNodeID =
					(uint32_t)(renderObjectData.animator == nullptr ?
					0 :
					renderObjectData.animator->skinIndexToGlobalReservedNodeIndex(primitive->animatorSkinIndexPropagatedCopy)),
				.voxelFieldLightingGridID = 0,  // @NOTE: the default lightmap is blank 1.0f with identity transform, so set 0 to use the default lightmap.
			});

			renderObjectData.perPrimitiveUniqueMaterialBaseIndices.push_back(
				materialorganizer::derivedMaterialNameToUMBIdx(derivedMatName + ".hderriere")
			);
		}

		// Register object
		_renderObjectPool[registerIndex] = renderObjectData;
		_renderObjectsIsRegistered[registerIndex] = true;
		_renderObjectsIndices.push_back(registerIndex);

		*outRenderObjectDatas[i] = &_renderObjectPool[registerIndex];
	}

	// Sort pool indices so that materials and then models are next to each other (helps with compacting render objects in the rendering stage).
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
	recalculateSpecialCaseIndices();
	_isMetaMeshListUnoptimized = true;

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
				recalculateSpecialCaseIndices();
				_isMetaMeshListUnoptimized = true;

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

bool RenderObjectManager::checkIsMetaMeshListUnoptimized()
{
	return _isMetaMeshListUnoptimized;
}

void RenderObjectManager::flagMetaMeshListAsUnoptimized()
{
	_isMetaMeshListUnoptimized = true;
}

void RenderObjectManager::optimizeMetaMeshList()
{
	// @NOTE: since this is a process-intensive operation, it could be good to compile
	//        all of the metameshes into a separate list in a separate thread while using
	//        the stale meta mesh list until this operation finishes. Then, upon finishing,
	//        just assign the memory address of the finished, new meta mesh list to the pointer
	//        of the old one. Just a thought, but this would force some renderobjects
	//        to stay alive but it's not impossible to manage.  -Timo 2023/12/12

	// Delete existing bucket hierarchy using previously fetched volumes.
	if (_umbBuckets != nullptr)
	{
		for (size_t i = 0; i < _numUmbBuckets; i++)
		{
			UniqueMaterialBaseBucket& umbBucket = _umbBuckets[i];
			for (size_t j = 0; j < 2; j++)
			{
				for (size_t k = 0; k < _numModelBuckets; k++)
				{
					ModelBucket& modelBucket = umbBucket.modelBucketSets[j].modelBuckets[k];
					delete[] modelBucket.meshBuckets;
				}
				delete[] umbBucket.modelBucketSets[j].modelBuckets;
			}
		}
		delete[] _umbBuckets;
	}

	// Get bucket sizes.
	_numUmbBuckets = materialorganizer::getNumUniqueMaterialBasesExcludingSpecials();
	_numModelBuckets = _renderObjectModels.size();
	_numMeshBucketsByModelIdx.clear();
	_numMeshBucketsByModelIdx.resize(_numModelBuckets, 0);
	size_t idx = 0;
	for (auto it = _renderObjectModels.begin(); it != _renderObjectModels.end(); it++)
	{
		auto model = it->second;
		model->assignedModelIdx = idx;
		_numMeshBucketsByModelIdx[idx] =
			model->getAllPrimitivesInOrder().size();
		idx++;
	}

	// Create bucket hierarchy.
	_umbBuckets = new UniqueMaterialBaseBucket[_numUmbBuckets];
	for (size_t i = 0; i < _numUmbBuckets; i++)
	{
		UniqueMaterialBaseBucket& umbBucket = _umbBuckets[i];
		for (size_t j = 0; j < 2; j++)
		{
			umbBucket.modelBucketSets[j].modelBuckets = new ModelBucket[_numModelBuckets];
			for (size_t k = 0; k < _numModelBuckets; k++)
			{
				ModelBucket& modelBucket = umbBucket.modelBucketSets[j].modelBuckets[k];
				modelBucket.meshBuckets = new MeshBucket[_numMeshBucketsByModelIdx[k]];
			}
		}
	}
	{
		// @DEBUG show the total size of the allocated bucket hierarchy (of just the containers).
		size_t totalSize = 0;
		for (size_t numMeshBuckets : _numMeshBucketsByModelIdx)
			totalSize += numMeshBuckets * 24;  // 24 is the bytes to create std::vector container.
		totalSize *= 2;
		totalSize *= _numUmbBuckets;
		std::cout << "Allocated bucket hierarchy is " << totalSize << " bytes without data." << std::endl;
	}

	std::lock_guard<std::mutex> lg(renderObjectIndicesAndPoolMutex);

	//
	// Cull out render object indices that are not marked as visible
	//
	std::vector<size_t> visibleIndices;
	for (size_t i = 0; i < _renderObjectsIndices.size(); i++)
	{
		size_t poolIndex = _renderObjectsIndices[i];
		RenderObject& object = _renderObjectPool[poolIndex];

		// See if render object itself is visible
		if (!_renderObjectLayersEnabled[(size_t)object.renderLayer])
			continue;
		if (object.model == nullptr)
			continue;

		// It's visible!!!!
		visibleIndices.push_back(poolIndex);
	}

	// Insert render objects into bucket hierarchy.
	_skinnedMeshEntriesExist = false;
	for (size_t roIdx = 0; roIdx < visibleIndices.size(); roIdx++)
	{
		RenderObject& ro = _renderObjectPool[visibleIndices[roIdx]];
		for (size_t mi = 0; mi < ro.perPrimitiveUniqueMaterialBaseIndices.size(); mi++)
		{
			if (ro.animator != nullptr)
				_skinnedMeshEntriesExist = true;
			size_t umbIdx = ro.perPrimitiveUniqueMaterialBaseIndices[mi];
			size_t skinnedIdx = (ro.animator != nullptr ? 0 : 1);
			size_t modelIdx = ro.model->assignedModelIdx;
			_umbBuckets[umbIdx]
				.modelBucketSets[skinnedIdx]
				.modelBuckets[modelIdx]
				.meshBuckets[mi]
				.renderObjectIndices.push_back(visibleIndices[roIdx]);
		}
	}

	// Add mesh draws into data structure.
	_modelMeshDraws.clear();
	_modelMeshDraws.resize(_renderObjectModels.size(), {});
	size_t mmdIdx = 0;
	for (auto it = _renderObjectModels.begin(); it != _renderObjectModels.end(); it++)
	{
		uint32_t _ = 0;
		it->second->appendPrimitiveDraws(_modelMeshDraws[mmdIdx], _);
		mmdIdx++;
	}

	_isMetaMeshListUnoptimized = false;
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
	std::string pathStringHenema    = "res/models_cooked/" + std::filesystem::path(modelPath).stem().string() + ".henema";
	vkglTF::Model* model = _renderObjectModels[name];
	model->destroy(_allocator);
	model->loadHthrobwoaFromFile(engine, modelPath, pathStringHenema);

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
	delete[] _renderObjectLayersEnabled;
}

void RenderObjectManager::recalculateSpecialCaseIndices()
{
	_renderObjectsWithAnimatorIndices.clear();
	_renderObjectsWithSimTransformIdIndices.clear();
	for (size_t poolIndex : _renderObjectsIndices)
	{
		if (_renderObjectPool[poolIndex].animator != nullptr)
			_renderObjectsWithAnimatorIndices.push_back(poolIndex);
		if (_renderObjectPool[poolIndex].simTransformId != (size_t)-1)
			_renderObjectsWithSimTransformIdIndices.push_back(poolIndex);
	}
}

void RenderObjectManager::updateSimTransforms()
{
	for (size_t i : _renderObjectsWithSimTransformIdIndices)
	{
		vec3 pos;
		versor rot;
		physengine::getInterpSimulationTransformPosition(_renderObjectPool[i].simTransformId, pos);
		physengine::getInterpSimulationTransformRotation(_renderObjectPool[i].simTransformId, rot);
		mat4& transform = _renderObjectPool[i].transformMatrix;
		glm_mat4_identity(transform);
		glm_translate(transform, pos);
		glm_quat_rotate(transform, rot, transform);
		glm_mat4_mul(transform, _renderObjectPool[i].simTransformOffset, transform);
	}
}

void RenderObjectManager::updateAnimators(float_t deltaTime)
{
	// @TODO: make this multithreaded....
	for (size_t& i : _renderObjectsWithAnimatorIndices)
		_renderObjectPool[i].animator->update(deltaTime);
}

vkglTF::Model* RenderObjectManager::createModel(vkglTF::Model* model, const std::string& name)
{
	// @NOTE: no need to reserve any size of models for this vector, bc we're just giving
	// away the pointer to the model itself instead bc the model is created on the heap
	if (name == "SlimeGirl")
		int32_t ian = 69;
	_renderObjectModels[name] = model;
	return _renderObjectModels[name];  // Ehhh, we could've just sent back the original model pointer
}
