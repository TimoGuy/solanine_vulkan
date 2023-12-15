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

	// Decompose render objects into meshes.
	_metaMeshes.clear();
	std::vector<vkglTF::Model*> uniqueModels;

	for (size_t roIdx = 0; roIdx < visibleIndices.size(); roIdx++)
	{
		RenderObject& ro = _renderObjectPool[visibleIndices[roIdx]];
		if (ro.animator != nullptr)
			int ian = 69;
		for (size_t mi = 0; mi < ro.perPrimitiveUniqueMaterialBaseIndices.size(); mi++)
			_metaMeshes.push_back({
				.model = ro.model,
				.isSkinned = (ro.animator != nullptr),
				.meshIdx = mi,
				.uniqueMaterialBaseId = ro.perPrimitiveUniqueMaterialBaseIndices[mi],
				.renderObjectIndices = { visibleIndices[roIdx] },
			});
		if (std::find(uniqueModels.begin(), uniqueModels.end(), ro.model) == uniqueModels.end())
			uniqueModels.push_back(ro.model);
	}

	// // Group by materials used, then whether skinned or not, then by model used, then by mesh index, then by render object index.
	// // @NOTE: this is to reduce the number of times to rebind materials and models.
	// // @PERFORMANCE: this runs at 18ms about (for main level scene)... make this faster... bc we don't want 18ms hitches every time a new render object is switched out.
	// static uint64_t count = 0;
	// static float_t avgTime = 0.0f;
	// if (input::editorInputSet().actionC.onAction)
	// {
	// 	std::cout << "RESET!" << std::endl;
	// 	count = 0;
	// 	avgTime = 0.0f;
	// }
	// uint64_t time = SDL_GetPerformanceCounter();
	// {
	// 	// @DEBUG: see how the meshes are sorted.
	// 	std::string groups[] = {
	// 		"metaMeshes     ",
	// 		"uniqueMatId    ",
	// 		"modelId        ",
	// 		"meshIdx        ",
	// 		"renderObjIdx[0]",
	// 	};

	// 	std::string myStr = "\n\n\n";

	// 	myStr += groups[0] + "\t\t";
	// 	for (size_t i = 0; i < _metaMeshes.size(); i++)
	// 		myStr += std::to_string(i) + "\t\t\t\t";
	// 	myStr += "\n";

	// 	for (size_t i = 0; i < _metaMeshes.size(); i++)
	// 		myStr += "================";
	// 	myStr += "\n";

	// 	myStr += groups[1] + "\t\t";
	// 	for (auto& maa : _metaMeshes)
	// 	{
	// 		myStr += std::to_string(maa.uniqueMaterialBaseId) + "\t\t\t\t";
	// 	}
	// 	myStr += "\n";

	// 	myStr += groups[2] + "\t\t";
	// 	std::vector<void*> foundModels;
	// 	for (auto& maa : _metaMeshes)
	// 	{
	// 		bool found = false;
	// 		for (size_t i = 0; i < foundModels.size(); i++)
	// 			if (foundModels[i] == (void*)maa.model)
	// 			{
	// 				myStr += std::to_string(i) + "\t\t\t\t";
	// 				found = true;
	// 				break;
	// 			}
	// 		if (!found)
	// 		{
	// 			foundModels.push_back((void*)maa.model);
	// 			myStr += std::to_string(foundModels.size() - 1) + "\t\t\t\t";
	// 		}
	// 	}
	// 	myStr += "\n";

	// 	myStr += groups[3] + "\t\t";
	// 	for (auto& maa : _metaMeshes)
	// 	{
	// 		myStr += std::to_string((uint64_t)(void*)maa.meshIdx) + "\t\t\t\t";
	// 	}
	// 	myStr += "\n";

	// 	myStr += groups[4] + "\t\t";
	// 	for (auto& maa : _metaMeshes)
	// 	{
	// 		myStr += std::to_string((uint64_t)(void*)maa.renderObjectIndices[0]) + "\t\t\t\t";
	// 	}
	// 	myStr += "\n";

	// 	myStr += "\n\n\n";

	// 	std::ofstream outfile("hello_debug.txt");
    //     if (outfile.is_open())
	// 		outfile << myStr;
	// }
	std::sort(
		_metaMeshes.begin(),
		_metaMeshes.end(),
		[&](MetaMesh& a, MetaMesh& b) {
			if (a.uniqueMaterialBaseId != b.uniqueMaterialBaseId)
				return a.uniqueMaterialBaseId < b.uniqueMaterialBaseId;
			if (a.isSkinned != b.isSkinned)
				return a.isSkinned;
			if (a.model != b.model)
				return a.model < b.model;
			if (a.meshIdx != b.meshIdx)
				return a.meshIdx < b.meshIdx;
			if (a.renderObjectIndices[0] != b.renderObjectIndices[0])
				return a.renderObjectIndices[0] < b.renderObjectIndices[0];
			return false;
		}
	);
	// time = SDL_GetPerformanceCounter() - time;
	// count++;
	// avgTime = avgTime * ((float_t)count - 1.0f) / (float_t)count + time / (float_t)count;
	// std::cout << "Sort time: " << time << "\tAvg: " << avgTime << std::endl;
	// // @DEBUG: end debug timing.

	// Smoosh meshes together.
	for (size_t i = 0; i < _metaMeshes.size(); i++)
	{
		auto& parentMesh = _metaMeshes[i];
		if (parentMesh.renderObjectIndices.empty())
			continue;  // Already consumed.
		
		for (size_t j = i + 1; j < _metaMeshes.size(); j++)
		{
			auto& siblingMesh = _metaMeshes[j];
			if (siblingMesh.renderObjectIndices.size() != 1)
				continue;  // Already consumed (<1)... or this is a parent mesh (>1)... Though neither should happen at this stage bc of sorting previously.

			bool sameAsParent =
				(parentMesh.isSkinned == siblingMesh.isSkinned &&
				parentMesh.model == siblingMesh.model &&
				parentMesh.meshIdx == siblingMesh.meshIdx &&
				parentMesh.uniqueMaterialBaseId == siblingMesh.uniqueMaterialBaseId);
			if (sameAsParent)
			{
				for (size_t a = 0; a < siblingMesh.renderObjectIndices.size(); a++)
					parentMesh.renderObjectIndices.push_back(siblingMesh.renderObjectIndices[a]);
				siblingMesh.renderObjectIndices.clear();
			}
			else
			{
				i = j - 1;  // Speed up parent mesh seeker to where new mesh is (minus 1 so that the iterator can increment for it!).
				break;  // Bc of sorting, there shouldn't be any other sibling meshes to find.
			}
		}
	}
	std::erase_if(
		_metaMeshes,
		[](MetaMesh mm) {
			return mm.renderObjectIndices.empty();
		}
	);

	// Smoosh skinned meshes together.
	_skinnedMeshEntries.clear();
	size_t smeInstanceIDOffset = 0;
	for (size_t i = 0; i < _metaMeshes.size(); i++)
	{
		auto& parentMesh = _metaMeshes[i];
		if (!parentMesh.isSkinned)
			continue;
		if (parentMesh.renderObjectIndices.empty())
			continue;  // Already consumed.

		// Insert mesh entries.
		for (size_t a = 0; a < parentMesh.renderObjectIndices.size(); a++)
			_skinnedMeshEntries.push_back({
				.model = parentMesh.model,
				.meshIdx = parentMesh.meshIdx,
				.uniqueMaterialBaseID = _renderObjectPool[parentMesh.renderObjectIndices[a]].perPrimitiveUniqueMaterialBaseIndices[parentMesh.meshIdx],
				.animatorNodeID = _renderObjectPool[parentMesh.renderObjectIndices[a]].calculatedModelInstances[parentMesh.meshIdx].animatorNodeID,
				.baseInstanceID = smeInstanceIDOffset++,
			});

		for (size_t j = i + 1; j < _metaMeshes.size(); j++)
		{
			auto& siblingMesh = _metaMeshes[j];
			if (siblingMesh.renderObjectIndices.empty())
				continue;  // Already consumed.

			bool sameAsSkinnedParent =
				(parentMesh.isSkinned == siblingMesh.isSkinned &&
				parentMesh.uniqueMaterialBaseId == siblingMesh.uniqueMaterialBaseId);
			if (sameAsSkinnedParent)
			{
				// Insert mesh entries.
				for (size_t a = 0; a < siblingMesh.renderObjectIndices.size(); a++)
				{
					_skinnedMeshEntries.push_back({
						.model = siblingMesh.model,
						.meshIdx = siblingMesh.meshIdx,
						.uniqueMaterialBaseID = _renderObjectPool[siblingMesh.renderObjectIndices[a]].perPrimitiveUniqueMaterialBaseIndices[siblingMesh.meshIdx],
						.animatorNodeID = _renderObjectPool[siblingMesh.renderObjectIndices[a]].calculatedModelInstances[siblingMesh.meshIdx].animatorNodeID,
						.baseInstanceID = smeInstanceIDOffset++,
					});
					parentMesh.renderObjectIndices.push_back(siblingMesh.renderObjectIndices[a]);
				}
				siblingMesh.renderObjectIndices.clear();
			}
			else
			{
				smeInstanceIDOffset = 0;  // Offset should be relative to the metamesh group, hence resetting it here.
				i = j - 1;  // Speed up parent mesh seeker to where new mesh is (minus 1 so that the iterator can increment for it!).
				break;  // Bc of sorting, there shouldn't be any other sibling meshes to find.
			}
		}
	}
	std::erase_if(
		_metaMeshes,
		[](MetaMesh mm) {
			return mm.renderObjectIndices.empty();
		}
	);

	// Mark all skinned meshes.
	for (auto& mm : _metaMeshes)
	{
		if (!mm.isSkinned)
			continue;
		
		mm.model = (vkglTF::Model*)&_skinnedMeshModelMemAddr;  // @HACK: @NOTE: marks metamesh as part of the intermediate skinned mesh buffer.
		mm.meshIdx = 0;
	}

	// // Sort by material for skinned metameshes. @NOTE: all the indices get combined together into a huge mesh so sorting on other props isn't needed.
	// std::sort(
	// 	skinnedMetaMeshes.begin(),
	// 	skinnedMetaMeshes.end(),
	// 	[&](MetaMesh& a, MetaMesh& b) {
	// 		if (a.uniqueMaterialBaseId != b.uniqueMaterialBaseId)
	// 			return a.uniqueMaterialBaseId < b.uniqueMaterialBaseId;
	// 		return false;
	// 	}
	// );

	// // Smoosh skinned meshes together.
	// _skinnedMetaMeshCalculatedModelInstances.clear();
	// for (auto& smm : skinnedMetaMeshes)
	// {
	// 	smm.model = (vkglTF::Model*)&_skinnedMeshModelMemAddr;  // @HACK
	// 	smm.renderObjectIndices
	// }

	// // Smoosh skinned meshes together.

	// Capture mesh info.
	_cookedMeshDraws.clear();
	for (vkglTF::Model* um : uniqueModels)
	{
		size_t baseMeshIndex = _cookedMeshDraws.size();
		uint32_t numMeshes = 0;
		um->appendPrimitiveDraws(_cookedMeshDraws, numMeshes);
		for (auto& metaMesh : _metaMeshes)
			if (metaMesh.model == um)
			{
				metaMesh.cookedMeshDrawIdx = baseMeshIndex + metaMesh.meshIdx;
			}
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
	_renderObjectModels[name] = model;
	return _renderObjectModels[name];  // Ehhh, we could've just sent back the original model pointer
}
