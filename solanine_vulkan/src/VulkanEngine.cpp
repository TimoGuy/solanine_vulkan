#include "pch.h"

#include "VulkanEngine.h"

#include "VkBootstrap.h"
#include "VkInitializers.h"
#include "VkDescriptorBuilderUtil.h"
#include "VkPipelineBuilderUtil.h"
#include "VkTextures.h"
#include "MaterialOrganizer.h"
#include "VkglTFModel.h"
#include "TextMesh.h"
#include "AudioEngine.h"
#include "PhysicsEngine.h"
#include "InputManager.h"
#include "Textbox.h"
#include "RenderObject.h"
#include "Entity.h"
#include "EntityManager.h"
#include "Camera.h"
#include "SceneManagement.h"
#include "DataSerialization.h"
#include "Debug.h"
#include "HotswapResources.h"
#include "GlobalState.h"
#include "GondolaSystem.h"

#ifdef _DEVELOP
#include "EDITORTextureViewer.h"
#endif

#include "SimulationCharacter.h"  // @NOCHECKIN


constexpr uint64_t TIMEOUT_1_SEC = 1000000000;

#ifdef _DEVELOP
std::mutex* hotswapMutex = nullptr;
#endif

void VulkanEngine::init()
{
	//
	// Read build number for window title
	//
	std::ifstream buildNumberFile;
	buildNumberFile.open("build_number.txt", std::ios::in);
	std::string buildNumber;
	if (buildNumberFile.is_open())
	{
		getline(buildNumberFile, buildNumber);
		buildNumberFile.close();
	}
	if (!buildNumber.empty())
		buildNumber = " - Build " + buildNumber;		// Prepend a good looking tag to the build number

	//
	// Initialization routine
	//
	SDL_Init(SDL_INIT_VIDEO);
	SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
	if (_windowFullscreen)
		window_flags = (SDL_WindowFlags)(window_flags | SDL_WINDOW_FULLSCREEN_DESKTOP);

	_window = SDL_CreateWindow(
		("Solanine Prealpha - Vulkan" + buildNumber).c_str(),
		SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED,
		_windowExtent.width,
		_windowExtent.height,
		window_flags
	);

	_roManager = new RenderObjectManager(_allocator);
	_entityManager = new EntityManager();
	_camera = new Camera(this);

#ifdef _DEVELOP
	hotswapMutex = hotswapres::startResourceChecker(this, _roManager, &_recreateSwapchain);
#endif

	initVulkan();
	initSwapchain();
	initCommands();
	initShadowRenderpass();
	initShadowImages();  // @NOTE: this isn't screen space, so no need to recreate images on swapchain recreation.
	initMainRenderpass();
	initUIRenderpass();
	initPostprocessRenderpass();
	initPostprocessImages();
	initPickingRenderpass();
	initFramebuffers();
	initSyncStructures();

	initImgui();
	loadImages();
	loadMeshes();
	generatePBRCubemaps();
	generateBRDFLUT();
	initDescriptors();
	initPipelines();
	loadMaterials();

	AudioEngine::getInstance().initialize();
	physengine::start(_entityManager);
	globalState::initGlobalState(this, _camera->sceneCamera);
	scene::init(this);
	GondolaSystem::_engine = this;

	while (!physengine::isInitialized);  // Spin lock so that new scene doesn't get loaded before physics are finished initializing.

	SDL_ShowWindow(_window);

	_isInitialized = true;

	changeEditorMode(_currentEditorMode);
}

constexpr size_t numPerfs = 15;
uint64_t perfs[numPerfs];

vec4 lightDir = { 0.144958f, 0.849756f, 0.506855f, 0.0f };

void VulkanEngine::run()
{
	//
	// Initialize Scene Camera
	//
	_camera->sceneCamera.aspect = (float_t)_windowExtent.width / (float_t)_windowExtent.height;
	_camera->sceneCamera.boxCastExtents[0] = _camera->sceneCamera.zNear * std::tan(_camera->sceneCamera.fov * 0.5f) * _camera->sceneCamera.aspect;
	_camera->sceneCamera.boxCastExtents[1] = _camera->sceneCamera.zNear* std::tan(_camera->sceneCamera.fov * 0.5f);
	_camera->sceneCamera.boxCastExtents[2] = _camera->sceneCamera.zNear * 0.5f;
	
	// @HARDCODED: Set the initial light direction
	glm_vec4_normalize(lightDir);
	glm_vec4_copy(lightDir, _pbrRendering.gpuSceneShadingProps.lightDir);

	_camera->sceneCamera.recalculateSceneCamera(_pbrRendering.gpuSceneShadingProps);

	//
	// Main Loop
	//
	bool isRunning = true;
	float_t ticksFrequency = 1.0f / (float_t)SDL_GetPerformanceFrequency();
	uint64_t lastFrame = SDL_GetPerformanceCounter();

	float_t saveGlobalStateTime        = 45.0f;  // 45 seconds
	float_t saveGlobalStateTimeElapsed = 0.0f;

#ifdef _DEVELOP
	input::registerEditorInputSetOnThisThread();
#endif

	while (isRunning)
	{
		perfs[2] = SDL_GetPerformanceCounter();
		// Update DeltaTime
		uint64_t currentFrame = SDL_GetPerformanceCounter();
		const float_t deltaTime = (float_t)(currentFrame - lastFrame) * ticksFrequency;
		const float_t scaledDeltaTime = deltaTime * globalState::timescale;
		lastFrame = currentFrame;
		perfs[2] = SDL_GetPerformanceCounter() - perfs[2];


		perfs[0] = SDL_GetPerformanceCounter();
		// Poll events from the window
		input::processInput(&isRunning, &_isWindowMinimized);
		input::editorInputSet().update();
		input::renderInputSet().update(deltaTime);
		perfs[0] = SDL_GetPerformanceCounter() - perfs[0];


		perfs[1] = SDL_GetPerformanceCounter();
		// Toggle fullscreen.
		if (input::renderInputSet().toggleFullscreen.onAction)
			setWindowFullscreen(!_windowFullscreen);

#ifdef _DEVELOP
		// Update time multiplier
		{
			bool changedTimescale = false;
			if (input::editorInputSet().halveTimescale.onAction)
			{
				globalState::timescale *= 0.5f;
				changedTimescale = true;
			}
			if (input::editorInputSet().doubleTimescale.onAction)
			{
				globalState::timescale *= 2.0f;
				changedTimescale = true;
			}
			if (changedTimescale)
			{
				debug::pushDebugMessage({
					.message = "Set timescale to " + std::to_string(globalState::timescale),
				});
			}
		}
#endif
		perfs[1] = SDL_GetPerformanceCounter() - perfs[1];


		perfs[3] = SDL_GetPerformanceCounter();
		// Stop anything from updating when window is minimized
		// @NOTE: this prevents the VK_ERROR_DEVICE_LOST(-4) error
		//        once the rendering code gets run while the window
		//        is minimized.  -Timo 2022/10/23
		// @NOTE: well, if you wanted the game to keep running in the
		//        background, you could just have this `continue;` block
		//        right after all of the simulations...
		if (_isWindowMinimized)
			continue;

		// Collect debug stats
		updateDebugStats(deltaTime);
		perfs[3] = SDL_GetPerformanceCounter() - perfs[3];


		perfs[4] = SDL_GetPerformanceCounter();
		// Update textbox
		textbox::update(deltaTime);
		perfs[4] = SDL_GetPerformanceCounter() - perfs[4];


		perfs[5] = SDL_GetPerformanceCounter();
		// Update render objects.
		physengine::recalcInterpolatedTransformsSet();
		_roManager->updateSimTransforms();
		_roManager->updateAnimators(scaledDeltaTime);
		perfs[5] = SDL_GetPerformanceCounter() - perfs[5];


		perfs[6] = SDL_GetPerformanceCounter();
		perfs[6] = SDL_GetPerformanceCounter() - perfs[6];


		perfs[7] = SDL_GetPerformanceCounter();
		// Update camera
		_camera->update(deltaTime);
		perfs[7] = SDL_GetPerformanceCounter() - perfs[7];


		perfs[8] = SDL_GetPerformanceCounter();
		// Allow scene management to tear down or load scenes.
		scene::tick();

		// Add/Remove requested entities
		_entityManager->INTERNALaddRemoveRequestedEntities();

		// Add/Change/Remove text meshes
		// textmesh::INTERNALprocessChangeQueue();
		perfs[8] = SDL_GetPerformanceCounter() - perfs[8];


		perfs[9] = SDL_GetPerformanceCounter();
		// Update global state
		saveGlobalStateTimeElapsed += deltaTime;
		if (saveGlobalStateTimeElapsed > saveGlobalStateTime)
		{
			saveGlobalStateTimeElapsed = 0.0f;
			globalState::launchAsyncWriteTask();
		}
		perfs[9] = SDL_GetPerformanceCounter() - perfs[9];


		perfs[10] = SDL_GetPerformanceCounter();
		// Update Audio Engine
		AudioEngine::getInstance().update();
		perfs[10] = SDL_GetPerformanceCounter() - perfs[10];


		perfs[11] = SDL_GetPerformanceCounter();
		//
		// Render
		//
#ifdef _DEVELOP
		{
			std::lock_guard<std::mutex> lg(*hotswapMutex);
#endif

			if (_recreateSwapchain)
				recreateSwapchain();
			perfs[11] = SDL_GetPerformanceCounter() - perfs[11];


			perfs[12] = SDL_GetPerformanceCounter();
			renderImGui(deltaTime);
			perfs[12] = SDL_GetPerformanceCounter() - perfs[12];


			perfs[13] = SDL_GetPerformanceCounter();
			render();
			perfs[13] = SDL_GetPerformanceCounter() - perfs[13];

#ifdef _DEVELOP
		}
#endif

		//
		// Calculate performance
		//
		if (input::editorInputSet().snapModifier.holding)  // @DEBUG: @INCOMPLETE: Change to tracy profiler instead of this hodge podge.
		{
			uint64_t totalPerf = 0;
			for (size_t i = 0; i < numPerfs; i++)
				totalPerf += perfs[i];

			std::cout << "Performance:";
			for (size_t i = 0; i < numPerfs; i++)
				std::cout << "\t" << (perfs[i] * 100 / totalPerf) << "% (" << perfs[i] << ")";
			std::cout << "\tCPS: " << SDL_GetPerformanceFrequency();
			std::cout << std::endl;
		}
	}
}

void VulkanEngine::cleanup()
{
	std::cout << std::endl
		<< "[CLEANUP PROCEDURE BEGIN]" << std::endl
		<< "===================================================================================================" << std::endl << std::endl;

	if (_isInitialized)
	{
		vkDeviceWaitIdle(_device);  // @NOTE: in subsequent cleanup procedures, Vulkan objects will get deleted, so to prevent anything being used from getting deleted, this barrier is here.
		SDL_DestroyWindow(_window);

#ifdef _DEVELOP
		hotswapres::flagStopRunning();
#endif

		globalState::cleanupGlobalState();
		AudioEngine::getInstance().cleanup();

		// @NOTE: halting the async runner allows for an immediate flush of entities since it's guaranteed to not be read anymore
		//        once the async runner is halted. While entities are being flushed, their physics bodies are getting destroyed.
		//        Then, the physics world gets destroyed in `::cleanup()` after all the bodies in the world have been destroyed.  -Timo 2023/09/28
		physengine::haltAsyncRunner();
		delete _entityManager;
		physengine::cleanup();

		delete _roManager;
		vkglTF::Animator::destroyEmpty(this);
		for (size_t i = 0; i < FRAME_OVERLAP; i++)
			destroySkinningBuffersIfCreated(_frames[i]);

		_mainDeletionQueue.flush();
		_swapchainDependentDeletionQueue.flush();

		textbox::cleanup();
		textmesh::cleanup();
		vkutil::pipelinelayoutcache::cleanup();
		vkutil::descriptorlayoutcache::cleanup();
		vkutil::descriptorallocator::cleanup();

		vmaDestroyAllocator(_allocator);
		vkDestroySurfaceKHR(_instance, _surface, nullptr);
		vkb::destroy_debug_utils_messenger(_instance, _debugMessenger);
		vkDestroyDevice(_device, nullptr);
		vkDestroyInstance(_instance, nullptr);

#ifdef _DEVELOP
		hotswapres::waitForShutdownAndTeardownResourceList();
#endif
	}

	std::cout << "Cleanup procedure finished." << std::endl;
}

void VulkanEngine::setWindowFullscreen(bool isFullscreen)
{
	_windowFullscreen = isFullscreen;
	SDL_SetWindowFullscreen(_window, _windowFullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

void normalizePlane(vec4 a, vec4 w, vec4& outNormalizedPlane)
{
	vec4 aw;
	glm_vec4_add(a, w, aw);
	vec3 aw3;
	glm_vec3(aw, aw3);
	glm_vec4_scale(aw, 1.0f / glm_vec3_norm(aw3), outNormalizedPlane);
}

static bool doCullingStuff = true;

void VulkanEngine::computeShadowCulling(const FrameData& currentFrame, VkCommandBuffer cmd)
{
	// Set up frustum culling params.
	mat4 reverseOrtho;
	glm_ortho(
		_camera->sceneCamera.wholeShadowMinExtents[0],
		_camera->sceneCamera.wholeShadowMaxExtents[0],
		_camera->sceneCamera.wholeShadowMinExtents[1],
		_camera->sceneCamera.wholeShadowMaxExtents[1],
		_camera->sceneCamera.wholeShadowMaxExtents[2],  // @NOTE: Znear and zfar are switched... though it doesn't really matter for anything with ortho projection.
		_camera->sceneCamera.wholeShadowMinExtents[2],
		reverseOrtho
	);
	mat4 reverseOrthoTransposed;
	glm_mat4_transpose_to(_camera->sceneCamera.gpuCameraData.projection, reverseOrthoTransposed);
	vec4 frustumX;
	vec4 frustumY;
	normalizePlane(reverseOrthoTransposed[0], reverseOrthoTransposed[3], frustumX);
	normalizePlane(reverseOrthoTransposed[1], reverseOrthoTransposed[3], frustumY);

	GPUCullingParams pc = {
		.zNear = std::numeric_limits<float_t>::min(),  // @TODO: add switch to turn off near/far plane comparison in frustum culling.
		.zFar = std::numeric_limits<float_t>::max(),
		.frustumX_x = frustumX[0],
		.frustumX_z = frustumX[2],
		.frustumY_y = frustumY[1],
		.frustumY_z = frustumY[2],
		.cullingEnabled = (uint32_t)true,
		.numInstances = currentFrame.numInstances,
	};
	glm_mat4_copy(_camera->sceneCamera.wholeShadowLightViewMatrix, pc.view);

	// Dispatch compute.
	Material& computeCulling = *getMaterial("computeCulling");
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computeCulling.pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computeCulling.pipelineLayout, 0, 1, &currentFrame.indirectShadowPass.indirectDrawCommandDescriptor, 0, nullptr);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computeCulling.pipelineLayout, 1, 1, &currentFrame.objectDescriptor, 0, nullptr);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computeCulling.pipelineLayout, 2, 1, &currentFrame.instancePtrDescriptor, 0, nullptr);
	vkCmdPushConstants(cmd, computeCulling.pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(GPUCullingParams), &pc);
	vkCmdDispatch(cmd, std::ceil(currentFrame.numInstances / 128.0f), 1, 1);

	// Block vertex shaders from running until the dispatched job is finished.
	VkBufferMemoryBarrier barriers[] = {
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
			.srcQueueFamilyIndex = _graphicsQueueFamily,
			.dstQueueFamilyIndex = _graphicsQueueFamily,
			.buffer = currentFrame.indirectShadowPass.indirectDrawCommandsBuffer._buffer,
			.offset = 0,
			.size = sizeof(VkDrawIndexedIndirectCommand) * INSTANCE_PTR_MAX_CAPACITY,
		},
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
			.srcQueueFamilyIndex = _graphicsQueueFamily,
			.dstQueueFamilyIndex = _graphicsQueueFamily,
			.buffer = currentFrame.indirectShadowPass.indirectDrawCommandCountsBuffer._buffer,
			.offset = 0,
			.size = sizeof(uint32_t) * INSTANCE_PTR_MAX_CAPACITY,
		}
	};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 0, nullptr, 2, barriers, 0, nullptr);
}

void VulkanEngine::computeMainCulling(const FrameData& currentFrame, VkCommandBuffer cmd)
{
	// Set up frustum culling params.
	// Ref: https://github.com/vblanco20-1/vulkan-guide/blob/164c144c4819840a9e59cc955a91b74abea4bd6f/extra-engine/vk_engine_scenerender.cpp#L68
	mat4 reverseProjection;  // @NOTE: reverse projection reverses the far and near values.
	glm_perspective(
		_camera->sceneCamera.fov,
		_camera->sceneCamera.aspect,
		_camera->sceneCamera.zFar,
		_camera->sceneCamera.zNear,
		reverseProjection
	);
	reverseProjection[1][1] *= -1.0f;
	mat4 reverseProjectionTransposed;
	glm_mat4_transpose_to(_camera->sceneCamera.gpuCameraData.projection, reverseProjectionTransposed);
	vec4 frustumX;
	vec4 frustumY;
	normalizePlane(reverseProjectionTransposed[0], reverseProjectionTransposed[3], frustumX);
	normalizePlane(reverseProjectionTransposed[1], reverseProjectionTransposed[3], frustumY);

	GPUCullingParams pc = {
		.zNear = _camera->sceneCamera.zNear,
		.zFar = _camera->sceneCamera.zFar,
		.frustumX_x = frustumX[0],
		.frustumX_z = frustumX[2],
		.frustumY_y = frustumY[1],
		.frustumY_z = frustumY[2],
		.cullingEnabled = (uint32_t)true,
		.numInstances = currentFrame.numInstances,
	};
	glm_mat4_copy(_camera->sceneCamera.gpuCameraData.view, pc.view);

	// Dispatch compute.
	Material& computeCulling = *getMaterial("computeCulling");
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computeCulling.pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computeCulling.pipelineLayout, 0, 1, &currentFrame.indirectMainPass.indirectDrawCommandDescriptor, 0, nullptr);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computeCulling.pipelineLayout, 1, 1, &currentFrame.objectDescriptor, 0, nullptr);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computeCulling.pipelineLayout, 2, 1, &currentFrame.instancePtrDescriptor, 0, nullptr);
	vkCmdPushConstants(cmd, computeCulling.pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(GPUCullingParams), &pc);
	vkCmdDispatch(cmd, std::ceil(currentFrame.numInstances / 128.0f), 1, 1);

	// Block vertex shaders from running until the dispatched job is finished.
	VkBufferMemoryBarrier barriers[] = {
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
			.srcQueueFamilyIndex = _graphicsQueueFamily,
			.dstQueueFamilyIndex = _graphicsQueueFamily,
			.buffer = currentFrame.indirectMainPass.indirectDrawCommandsBuffer._buffer,
			.offset = 0,
			.size = sizeof(VkDrawIndexedIndirectCommand) * INSTANCE_PTR_MAX_CAPACITY,
		},
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
			.srcQueueFamilyIndex = _graphicsQueueFamily,
			.dstQueueFamilyIndex = _graphicsQueueFamily,
			.buffer = currentFrame.indirectMainPass.indirectDrawCommandCountsBuffer._buffer,
			.offset = 0,
			.size = sizeof(uint32_t) * INSTANCE_PTR_MAX_CAPACITY,
		}
	};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 0, nullptr, 2, barriers, 0, nullptr);
}

void VulkanEngine::computeSkinnedMeshes(const FrameData& currentFrame, VkCommandBuffer cmd)
{
	if (_roManager->_renderObjectsWithAnimatorIndices.empty())
		return;  // Omit skinning meshes if no meshes to skin.

	Material& computeSkinning = *getMaterial("computeSkinning");
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computeSkinning.pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computeSkinning.pipelineLayout, 0, 1, &currentFrame.skinning.inoutVerticesDescriptor, 0, nullptr);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computeSkinning.pipelineLayout, 1, 1, vkglTF::Animator::getGlobalAnimatorNodeCollectionDescriptorSet(this), 0, nullptr);
	vkCmdDispatch(cmd, std::ceil(currentFrame.skinning.numVertices / 256.0f), 1, 1);

	// Block vertex shaders from running until the dispatched job is finished.
	VkBufferMemoryBarrier barrier = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		.srcQueueFamilyIndex = _graphicsQueueFamily,
		.dstQueueFamilyIndex = _graphicsQueueFamily,
		.buffer = currentFrame.skinning.outputVerticesBuffer._buffer,
		.offset = 0,
		.size = currentFrame.skinning.outputBufferSize,
	};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 0, nullptr, 1, &barrier, 0, nullptr);
}

void VulkanEngine::renderPickingRenderpass(const FrameData& currentFrame)
{
	VK_CHECK(vkResetFences(_device, 1, &currentFrame.pickingRenderFence));

	// Reset the command buffer and start the render pass
	VK_CHECK(vkResetCommandBuffer(currentFrame.pickingCommandBuffer, 0));
	VkCommandBuffer cmd = currentFrame.pickingCommandBuffer;

	VkCommandBufferBeginInfo cmdBeginInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.pNext = nullptr,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		.pInheritanceInfo = nullptr,
	};

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	VkClearValue clearValue;
	clearValue.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

	VkClearValue depthClear;
	depthClear.depthStencil.depth = 1.0f;

	VkClearValue clearValues[] = { clearValue, depthClear };

	VkRenderPassBeginInfo renderpassInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.pNext = nullptr,

		.renderPass = _pickingRenderPass,
		.framebuffer = _pickingFramebuffer,
		.renderArea = {
			.offset = VkOffset2D{ 0, 0 },
			.extent = _windowExtent,
		},

		.clearValueCount = 2,
		.pClearValues = &clearValues[0],
	};

	// Begin renderpass
	vkCmdBeginRenderPass(cmd, &renderpassInfo, VK_SUBPASS_CONTENTS_INLINE);

	// Bind picking material
	Material& pickingMaterial = *getMaterial("pickingMaterial");
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pickingMaterial.pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pickingMaterial.pipelineLayout, 0, 1, &currentFrame.globalDescriptor, 0, nullptr);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pickingMaterial.pipelineLayout, 1, 1, &currentFrame.objectDescriptor, 0, nullptr);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pickingMaterial.pipelineLayout, 2, 1, &currentFrame.instancePtrDescriptor, 0, nullptr);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pickingMaterial.pipelineLayout, 3, 1, &currentFrame.pickingReturnValueDescriptor, 0, nullptr);

	// Set dynamic scissor
	VkRect2D scissor = {};
	scissor.offset.x = (int32_t)ImGui::GetIO().MousePos.x;
	scissor.offset.y = (int32_t)ImGui::GetIO().MousePos.y;
	scissor.extent = { 1, 1 };
	vkCmdSetScissor(cmd, 0, 1, &scissor);    // @NOTE: the scissor is set to be dynamic state for this pipeline

	std::cout << "[PICKING]" << std::endl
		<< "set picking scissor to: x=" << scissor.offset.x << "  y=" << scissor.offset.y << "  w=" << scissor.extent.width << "  h=" << scissor.extent.height << std::endl;

	renderRenderObjects(cmd, currentFrame, true, false);

	// End renderpass
	vkCmdEndRenderPass(cmd);
	VK_CHECK(vkEndCommandBuffer(cmd));

	//
	// Submit picking command buffer to gpu for execution
	//
	VkSubmitInfo submit = {};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.pNext = nullptr;

	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &cmd;

	// Submit work to gpu
	VkResult result = vkQueueSubmit(_graphicsQueue, 1, &submit, currentFrame.pickingRenderFence);
	if (result == VK_ERROR_DEVICE_LOST)
	{
		std::cerr << "ERROR: VULKAN DEVICE LOST." << std::endl;
		return;
	}

	//
	// Read from GPU to the CPU (the actual picking part eh!)
	//
	// Wait until GPU finishes rendering the previous picking
	VK_CHECK(vkWaitForFences(_device, 1, &currentFrame.pickingRenderFence, true, TIMEOUT_1_SEC));
	VK_CHECK(vkResetFences(_device, 1, &currentFrame.pickingRenderFence));

	// Read from the gpu
	GPUPickingSelectedIdData resetData = {};
	GPUPickingSelectedIdData p;

	void* data;
	vmaMapMemory(_allocator, currentFrame.pickingSelectedIdBuffer._allocation, &data);
	memcpy(&p, data, sizeof(GPUPickingSelectedIdData));            // It's not Dmitri, it's Irtimd this time
	memcpy(data, &resetData, sizeof(GPUPickingSelectedIdData));    // @NOTE: if you don't reset the buffer, then you won't get 0 if you click on an empty spot next time bc you end up just getting garbage data.  -Dmitri
	vmaUnmapMemory(_allocator, currentFrame.pickingSelectedIdBuffer._allocation);

	// @TODO: Make a pre-check to see if these combination of objects were selected in the previous click, and then choose the 2nd nearest object, 3rd nearest, etc. depending on how many clicks.

	uint32_t nearestSelectedId = 0;
	float_t nearestDepth = std::numeric_limits<float_t>::max();
	for (size_t poolIndex : _roManager->_renderObjectsIndices)
	{
		if (p.selectedId[poolIndex] == 0)
			continue;  // Means that the data never got filled

		if (p.selectedDepth[poolIndex] > nearestDepth)
			continue;

		nearestSelectedId = p.selectedId[poolIndex];
		nearestDepth = p.selectedDepth[poolIndex];
	}

	submitSelectedRenderObjectId(static_cast<int32_t>(nearestSelectedId) - 1);
}

void VulkanEngine::renderShadowRenderpass(const FrameData& currentFrame, VkCommandBuffer cmd)
{
	VkClearValue depthClear;
	depthClear.depthStencil = { 1.0f, 0 };

	VkRenderPassBeginInfo renderpassInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.pNext = nullptr,

		.renderPass = _shadowRenderPass,
		.renderArea = {
			.offset = VkOffset2D{ 0, 0 },
			.extent = VkExtent2D{ SHADOWMAP_DIMENSION, SHADOWMAP_DIMENSION },
		},

		.clearValueCount = 1,
		.pClearValues = &depthClear,
	};

	// Upload shadow cascades to GPU
	void* data;
	vmaMapMemory(_allocator, currentFrame.cascadeViewProjsBuffer._allocation, &data);
	memcpy(data, &_camera->sceneCamera.gpuCascadeViewProjsData, sizeof(GPUCascadeViewProjsData));
	vmaUnmapMemory(_allocator, currentFrame.cascadeViewProjsBuffer._allocation);

	Material& shadowDepthPassMaterial = *getMaterial("shadowdepthpass.special.humba");
	for (uint32_t i = 0; i < SHADOWMAP_CASCADES; i++)
	{
		renderpassInfo.framebuffer = _shadowCascades[i].framebuffer;
		vkCmdBeginRenderPass(cmd, &renderpassInfo, VK_SUBPASS_CONTENTS_INLINE);

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowDepthPassMaterial.pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowDepthPassMaterial.pipelineLayout, 0, 1, &currentFrame.cascadeViewProjsDescriptor, 0, nullptr);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowDepthPassMaterial.pipelineLayout, 1, 1, &currentFrame.objectDescriptor, 0, nullptr);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowDepthPassMaterial.pipelineLayout, 2, 1, &currentFrame.instancePtrDescriptor, 0, nullptr);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowDepthPassMaterial.pipelineLayout, 3, 1, &shadowDepthPassMaterial.textureSet, 0, nullptr);

		CascadeIndexPushConstBlock pc = { i };
		vkCmdPushConstants(cmd, shadowDepthPassMaterial.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(CascadeIndexPushConstBlock), &pc);

		renderRenderObjects(cmd, currentFrame, true, true);
		
		vkCmdEndRenderPass(cmd);
	}
}

void VulkanEngine::renderMainRenderpass(const FrameData& currentFrame, VkCommandBuffer cmd, const std::vector<ModelWithIndirectDrawId>& pickingIndirectDrawCommandIds)
{
	VkClearValue clearValue;
	clearValue.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

	VkClearValue depthClear;
	depthClear.depthStencil.depth = 1.0f;

	VkClearValue clearValues[] = { clearValue, depthClear };

	VkRenderPassBeginInfo renderpassInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.pNext = nullptr,

		.renderPass = _mainRenderPass,
		.framebuffer = _mainFramebuffer,
		.renderArea = {
			.offset = VkOffset2D{ 0, 0 },
			.extent = _windowExtent,
		},

		.clearValueCount = 2,
		.pClearValues = &clearValues[0],
	};

	// Begin renderpass
	vkCmdBeginRenderPass(cmd, &renderpassInfo, VK_SUBPASS_CONTENTS_INLINE);


	// Render z prepass //
	Material& defaultZPrepassMaterial = *getMaterial("zprepass.special.humba");
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, defaultZPrepassMaterial.pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, defaultZPrepassMaterial.pipelineLayout, 0, 1, &currentFrame.globalDescriptor, 0, nullptr);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, defaultZPrepassMaterial.pipelineLayout, 1, 1, &currentFrame.objectDescriptor, 0, nullptr);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, defaultZPrepassMaterial.pipelineLayout, 2, 1, &currentFrame.instancePtrDescriptor, 0, nullptr);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, defaultZPrepassMaterial.pipelineLayout, 3, 1, &defaultZPrepassMaterial.textureSet, 0, nullptr);
	renderRenderObjects(cmd, currentFrame, true, false);
	//////////////////////

	// Switch from zprepass subpass to main subpass
	vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_INLINE);

	// Render skybox //
	if (_currentEditorMode == EditorModes::LEVEL_EDITOR ||
		_currentEditorMode == EditorModes::MATERIAL_EDITOR)
	{
		// @TODO: put this into its own function!
		Material& skyboxMaterial = *getMaterial("skyboxMaterial");
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxMaterial.pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxMaterial.pipelineLayout, 0, 1, &currentFrame.globalDescriptor, 0, nullptr);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxMaterial.pipelineLayout, 1, 1, &skyboxMaterial.textureSet, 0, nullptr);

		auto skybox = _roManager->getModel("Box", nullptr, [](){});
		skybox->bind(cmd);
		skybox->draw(cmd);
	}
	///////////////////

	renderRenderObjects(cmd, currentFrame, false, false);
	if (!pickingIndirectDrawCommandIds.empty())
		renderPickedObject(cmd, currentFrame, pickingIndirectDrawCommandIds);
	physengine::renderDebugVisualization(cmd);

	// End renderpass
	vkCmdEndRenderPass(cmd);
}

void VulkanEngine::renderUIRenderpass(VkCommandBuffer cmd)
{
	VkClearValue clearValue;
	clearValue.color = { { 0.0f, 0.0f, 0.0f, 0.0f } };

	VkClearValue clearValues[] = { clearValue };

	VkRenderPassBeginInfo renderpassInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.pNext = nullptr,

		.renderPass = _uiRenderPass,
		.framebuffer = _uiFramebuffer,
		.renderArea = {
			.offset = VkOffset2D{ 0, 0 },
			.extent = _windowExtent,
		},

		.clearValueCount = 1,
		.pClearValues = &clearValues[0],
	};

	// Renderpass
	vkCmdBeginRenderPass(cmd, &renderpassInfo, VK_SUBPASS_CONTENTS_INLINE);

	textmesh::renderTextMeshesBulk(cmd);
	textbox::renderTextbox(cmd);

	vkCmdEndRenderPass(cmd);
}

void ppBlitBloom(VkCommandBuffer cmd, Texture& mainImage, VkExtent2D& windowExtent, Texture& bloomImage, VkExtent2D& bloomImageExtent)
{
	//
	// Blit bloom
	// @NOTE: @IMPROVE: the bloom render pass has obvious artifacts when a camera pans slowly.
	//                  Might wanna implement this in the future: (https://learnopengl.com/Guest-Articles/2022/Phys.-Based-Bloom)
	//                    -Timo 2022/11/09
	//

	// Change bloom image all mips to dst transfer layout
	{
		VkImageMemoryBarrier imageBarrier = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.image = bloomImage.image._image,
			.subresourceRange = {
				.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel   = 0,
				.levelCount     = static_cast<uint32_t>(bloomImage.image._mipLevels),
				.baseArrayLayer = 0,
				.layerCount     = 1,
			},
		};
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
			0, nullptr,
			0, nullptr,
			1, &imageBarrier
		);
	}

	// Copy mainRenderPass image to bloom buffer
	VkImageBlit blitRegion = {
		.srcSubresource = {
			.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
			.mipLevel       = 0,
			.baseArrayLayer = 0,
			.layerCount     = 1,
		},
		.srcOffsets = {
			{ 0, 0, 0 },
			{ (int32_t)windowExtent.width, (int32_t)windowExtent.height, 1 },
		},
		.dstSubresource = {
			.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
			.mipLevel       = 0,
			.baseArrayLayer = 0,
			.layerCount     = 1,
		},
		.dstOffsets = {
			{ 0, 0, 0 },
			{
				(int32_t)bloomImageExtent.width,
				(int32_t)bloomImageExtent.height,
				1
			},
		},
	};
	vkCmdBlitImage(cmd,
		mainImage.image._image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,  // @NOTE: the renderpass for the mainImage makes it turn into VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL at the end, so a manual change to SHADER_READ_ONLY_OPTIMAL is necessary
		bloomImage.image._image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &blitRegion,
		VK_FILTER_LINEAR
	);

	// Blit out all remaining mip levels of the chain
	// BUT FIRST: Change mip0 of bloom image to src transfer layout
	VkImageMemoryBarrier imageBarrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.image = bloomImage.image._image,
		.subresourceRange = {
			.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel   = 0,
			.levelCount     = 1,
			.baseArrayLayer = 0,
			.layerCount     = 1,
		},
	};

	int32_t mipWidth = bloomImageExtent.width;
	int32_t mipHeight = bloomImageExtent.height;

	for (uint32_t mipLevel = 1; mipLevel < bloomImage.image._mipLevels; mipLevel++)
	{
		// Change previous mip to src optimal image
		imageBarrier.subresourceRange.baseMipLevel = mipLevel - 1;
		imageBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		imageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		imageBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		imageBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
			0, nullptr,
			0, nullptr,
			1, &imageBarrier
		);

		// Blit image to next mip
		VkImageBlit blitRegion = {
			.srcSubresource = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = mipLevel - 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
			.srcOffsets = {
				{ 0, 0, 0 },
				{ mipWidth, mipHeight, 1 },
			},
			.dstSubresource = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = mipLevel,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
			.dstOffsets = {
				{ 0, 0, 0 },
				{ mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 },
			},
		};
		vkCmdBlitImage(cmd,
			bloomImage.image._image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			bloomImage.image._image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &blitRegion,
			VK_FILTER_LINEAR
		);

		// Change previous (src) mip to shader readonly layout
		imageBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		imageBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
			0, nullptr,
			0, nullptr,
			1, &imageBarrier
		);

		// Update mip sizes
		if (mipWidth > 1) mipWidth /= 2;
		if (mipHeight > 1) mipHeight /= 2;
	}

	// Change final mip to shader readonly layout
	imageBarrier.subresourceRange.baseMipLevel = bloomImage.image._mipLevels - 1;
	imageBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	imageBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	vkCmdPipelineBarrier(cmd,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
		0, nullptr,
		0, nullptr,
		1, &imageBarrier
	);

	// Change mainRenderPass image to shader optimal image layout
	{
		VkImageMemoryBarrier imageBarrier = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.image = mainImage.image._image,
			.subresourceRange = {
				.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel   = 0,
				.levelCount     = 1,
				.baseArrayLayer = 0,
				.layerCount     = 1,
			},
		};
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
			0, nullptr,
			0, nullptr,
			1, &imageBarrier
		);
	}
}

void ppDepthOfField_GenerateCircleOfConfusion(VkCommandBuffer cmd, VkRenderPass CoCRenderPass, VkFramebuffer CoCFramebuffer, Material& CoCMaterial, GPUCoCParams& CoCParams, VkExtent2D& windowExtent)
{
	VkClearValue clearValues[1];
	clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

	VkRenderPassBeginInfo renderpassInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.pNext = nullptr,

		.renderPass = CoCRenderPass,
		.framebuffer = CoCFramebuffer,
		.renderArea = {
			.offset = VkOffset2D{ 0, 0 },
			.extent = windowExtent,
		},

		.clearValueCount = 1,
		.pClearValues = clearValues,
	};

	// Execute renderpass.
	vkCmdBeginRenderPass(cmd, &renderpassInfo, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, CoCMaterial.pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, CoCMaterial.pipelineLayout, 0, 1, &CoCMaterial.textureSet, 0, nullptr);
	vkCmdPushConstants(cmd, CoCMaterial.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(GPUCoCParams), &CoCParams);
	vkCmdDraw(cmd, 3, 1, 0, 0);

	vkCmdEndRenderPass(cmd);
}

void ppDepthOfField_HalveCircleOfConfusionWhileGeneratingNearFar(VkCommandBuffer cmd, VkRenderPass halveCoCRenderPass, VkFramebuffer halveCoCFramebuffer, Material& halveCoCMaterial, VkExtent2D& halfResImageExtent)
{
	VkClearValue clearValues[2];
	clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
	clearValues[1].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

	VkRenderPassBeginInfo renderpassInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.pNext = nullptr,

		.renderPass = halveCoCRenderPass,
		.framebuffer = halveCoCFramebuffer,
		.renderArea = {
			.offset = VkOffset2D{ 0, 0 },
			.extent = halfResImageExtent,
		},

		.clearValueCount = 2,
		.pClearValues = clearValues,
	};

	// Execute renderpass.
	vkCmdBeginRenderPass(cmd, &renderpassInfo, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, halveCoCMaterial.pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, halveCoCMaterial.pipelineLayout, 0, 1, &halveCoCMaterial.textureSet, 0, nullptr);
	vkCmdDraw(cmd, 3, 1, 0, 0);

	vkCmdEndRenderPass(cmd);
}

struct IncrementalHalveCoCParams
{
	VkFramebuffer framebuffer;
	Material* material;
	VkExtent2D imageExtent;
};

void ppDepthOfField_IncrementalReductionHalveCircleOfConfusion(VkCommandBuffer cmd, VkRenderPass incrementalReductionHalveCoCRenderPass, std::vector<IncrementalHalveCoCParams>& incrementalReductions)
{
	for (IncrementalHalveCoCParams& ihcp : incrementalReductions)
	{
		VkFramebuffer incrementalReductionHalveCoCFramebuffer = ihcp.framebuffer;
		Material& incrementalReductionHalveCoCMaterial = *ihcp.material;
		VkExtent2D& incrementalReductionHalveResImageExtent = ihcp.imageExtent;

		VkClearValue clearValues[1];
		clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

		VkRenderPassBeginInfo renderpassInfo = {
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.pNext = nullptr,

			.renderPass = incrementalReductionHalveCoCRenderPass,
			.framebuffer = incrementalReductionHalveCoCFramebuffer,
			.renderArea = {
				.offset = VkOffset2D{ 0, 0 },
				.extent = incrementalReductionHalveResImageExtent,
			},

			.clearValueCount = 1,
			.pClearValues = clearValues,
		};

		// Execute renderpass.
		vkCmdBeginRenderPass(cmd, &renderpassInfo, VK_SUBPASS_CONTENTS_INLINE);

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, incrementalReductionHalveCoCMaterial.pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, incrementalReductionHalveCoCMaterial.pipelineLayout, 0, 1, &incrementalReductionHalveCoCMaterial.textureSet, 0, nullptr);
		vkCmdDraw(cmd, 3, 1, 0, 0);

		vkCmdEndRenderPass(cmd);
	}
}

void ppDepthOfField_BlurNearsideCoC(VkCommandBuffer cmd, VkRenderPass blurXNearsideCoCRenderPass, VkFramebuffer blurXNearsideCoCFramebuffer, Material& blurXMaterial, VkRenderPass blurYNearsideCoCRenderPass, VkFramebuffer blurYNearsideCoCFramebuffer, Material& blurYMaterial, GPUBlurParams& blurParams, VkExtent2D& incrementalReductionHalveResImageExtent)
{
	// Blur the downsized nearside CoC using ping-pong technique.
	VkRenderPass blurPasses[] = { blurXNearsideCoCRenderPass, blurYNearsideCoCRenderPass };
	VkFramebuffer blurFramebuffers[] = { blurXNearsideCoCFramebuffer, blurYNearsideCoCFramebuffer };
	Material* blurMaterials[] = { &blurXMaterial, &blurYMaterial };

	VkClearValue clearValue;
	clearValue.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

	VkRenderPassBeginInfo renderpassInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.pNext = nullptr,

		.renderArea = {
			.offset = VkOffset2D{ 0, 0 },
			.extent = incrementalReductionHalveResImageExtent,
		},

		.clearValueCount = 1,
		.pClearValues = &clearValue,
	};

	for (size_t i = 0; i < 2; i++)
	{
		// Blur one axis of new nearside CoC.
		renderpassInfo.renderPass = blurPasses[i];
		renderpassInfo.framebuffer = blurFramebuffers[i];

		vkCmdBeginRenderPass(cmd, &renderpassInfo, VK_SUBPASS_CONTENTS_INLINE);

		Material& blurMaterial = *blurMaterials[i];
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blurMaterial.pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blurMaterial.pipelineLayout, 0, 1, &blurMaterial.textureSet, 0, nullptr);
		vkCmdPushConstants(cmd, blurMaterial.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(GPUBlurParams), &blurParams);
		vkCmdDraw(cmd, 3, 1, 0, 0);

		vkCmdEndRenderPass(cmd);
	}
}

void ppDepthOfField_GatherDepthOfField(VkCommandBuffer cmd, VkRenderPass gatherDOFRenderPass, VkFramebuffer gatherDOFFramebuffer, Material& gatherDOFMaterial, GPUGatherDOFParams& dofParams, VkExtent2D& halfResImageExtent)
{
	// Downsize nearside CoC.
	VkClearValue clearValues[2];
	clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
	clearValues[1].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

	VkRenderPassBeginInfo renderpassInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.pNext = nullptr,

		.renderPass = gatherDOFRenderPass,
		.framebuffer = gatherDOFFramebuffer,
		.renderArea = {
			.offset = VkOffset2D{ 0, 0 },
			.extent = halfResImageExtent,
		},

		.clearValueCount = 2,
		.pClearValues = clearValues,
	};

	vkCmdBeginRenderPass(cmd, &renderpassInfo, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gatherDOFMaterial.pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gatherDOFMaterial.pipelineLayout, 0, 1, &gatherDOFMaterial.textureSet, 0, nullptr);
	vkCmdPushConstants(cmd, gatherDOFMaterial.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(GPUGatherDOFParams), &dofParams);
	vkCmdDraw(cmd, 3, 1, 0, 0);

	vkCmdEndRenderPass(cmd);
}

void ppDepthOfField_DepthOfFieldFloodFill(VkCommandBuffer cmd, VkRenderPass dofFloodFillRenderPass, VkFramebuffer dofFloodFillFramebuffer, Material& dofFloodFillMaterial, GPUBlurParams& floodfillParams, VkExtent2D& halfResImageExtent)
{
	// Downsize nearside CoC.
	VkClearValue clearValues[2];
	clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
	clearValues[1].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

	VkRenderPassBeginInfo renderpassInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.pNext = nullptr,

		.renderPass = dofFloodFillRenderPass,
		.framebuffer = dofFloodFillFramebuffer,
		.renderArea = {
			.offset = VkOffset2D{ 0, 0 },
			.extent = halfResImageExtent,
		},

		.clearValueCount = 2,
		.pClearValues = clearValues,
	};

	vkCmdBeginRenderPass(cmd, &renderpassInfo, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, dofFloodFillMaterial.pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, dofFloodFillMaterial.pipelineLayout, 0, 1, &dofFloodFillMaterial.textureSet, 0, nullptr);
	vkCmdPushConstants(cmd, dofFloodFillMaterial.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(GPUBlurParams), &floodfillParams);
	vkCmdDraw(cmd, 3, 1, 0, 0);

	vkCmdEndRenderPass(cmd);
}

void ppDepthOfField(
	VkCommandBuffer cmd,
	VkRenderPass CoCRenderPass, VkFramebuffer CoCFramebuffer, Material& CoCMaterial, GPUCoCParams& CoCParams, VkExtent2D& windowExtent,
	VkRenderPass halveCoCRenderPass, VkFramebuffer halveCoCFramebuffer, Material& halveCoCMaterial, VkExtent2D& halfResImageExtent,
	VkRenderPass incrementalReductionHalveCoCRenderPass, std::vector<IncrementalHalveCoCParams>& incrementalReductions,
	VkRenderPass blurXNearsideCoCRenderPass, VkFramebuffer blurXNearsideCoCFramebuffer, Material& blurXMaterial,
	VkRenderPass blurYNearsideCoCRenderPass, VkFramebuffer blurYNearsideCoCFramebuffer, Material& blurYMaterial, GPUBlurParams& blurParams,
	VkRenderPass gatherDOFRenderPass, VkFramebuffer gatherDOFFramebuffer, Material& gatherDOFMaterial, GPUGatherDOFParams& dofParams,
	VkRenderPass dofFloodFillRenderPass, VkFramebuffer dofFloodFillFramebuffer, Material& dofFloodFillMaterial, GPUBlurParams& floodfillParams)
{
	ppDepthOfField_GenerateCircleOfConfusion(
		cmd,
		CoCRenderPass,
		CoCFramebuffer,
		CoCMaterial,
		CoCParams,
		windowExtent
	);

	ppDepthOfField_HalveCircleOfConfusionWhileGeneratingNearFar(
		cmd,
		halveCoCRenderPass,
		halveCoCFramebuffer,
		halveCoCMaterial,
		halfResImageExtent
	);

	ppDepthOfField_IncrementalReductionHalveCircleOfConfusion(
		cmd,
		incrementalReductionHalveCoCRenderPass,
		incrementalReductions
	);

	ppDepthOfField_BlurNearsideCoC(
		cmd,
		blurXNearsideCoCRenderPass,
		blurXNearsideCoCFramebuffer,
		blurXMaterial,
		blurYNearsideCoCRenderPass,
		blurYNearsideCoCFramebuffer,
		blurYMaterial,
		blurParams,
		incrementalReductions.back().imageExtent
	);

	ppDepthOfField_GatherDepthOfField(
		cmd,
		gatherDOFRenderPass,
		gatherDOFFramebuffer,
		gatherDOFMaterial,
		dofParams,
		halfResImageExtent
	);

	ppDepthOfField_DepthOfFieldFloodFill(
		cmd,
		dofFloodFillRenderPass,
		dofFloodFillFramebuffer,
		dofFloodFillMaterial,
		floodfillParams,
		halfResImageExtent
	);
}

void ppCombinePostprocesses(
	VkCommandBuffer cmd,
	VkRenderPass postprocessRenderPass, VkFramebuffer postprocessFramebuffer, Material& postprocessMaterial, VkExtent2D& windowExtent, VkDescriptorSet currentFrameGlobalDescriptor, bool applyTonemap, bool applyImGui)
{
	// Combine all postprocessing
	GPUPostProcessParams CoCParams = {
		.applyTonemap = applyTonemap,
	};

	VkClearValue clearValue;
	clearValue.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

	VkRenderPassBeginInfo renderpassInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.pNext = nullptr,

		.renderPass = postprocessRenderPass,
		.framebuffer = postprocessFramebuffer,
		.renderArea = {
			.offset = VkOffset2D{ 0, 0 },
			.extent = windowExtent,
		},

		.clearValueCount = 1,
		.pClearValues = &clearValue,
	};

	// Begin renderpass
	vkCmdBeginRenderPass(cmd, &renderpassInfo, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, postprocessMaterial.pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, postprocessMaterial.pipelineLayout, 0, 1, &currentFrameGlobalDescriptor, 0, nullptr);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, postprocessMaterial.pipelineLayout, 1, 1, &postprocessMaterial.textureSet, 0, nullptr);
	vkCmdPushConstants(cmd, postprocessMaterial.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(GPUPostProcessParams), &CoCParams);
	vkCmdDraw(cmd, 3, 1, 0, 0);

	if (applyImGui)
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

	// End renderpass
	vkCmdEndRenderPass(cmd);
}

void VulkanEngine::renderPostprocessRenderpass(const FrameData& currentFrame, VkCommandBuffer cmd, uint32_t swapchainImageIndex)
{
	// Generate postprocessing.
	ppBlitBloom(
		cmd,
		_mainImage,
		_windowExtent,
		_bloomPostprocessImage,
		_bloomPostprocessImageExtent
	);

	GPUCoCParams CoCParams = {
		.cameraZNear = _camera->sceneCamera.zNear,
		.cameraZFar = _camera->sceneCamera.zFar,
		.focusDepth = globalState::DOFFocusDepth,
		.focusExtent = globalState::DOFFocusExtent,
		.blurExtent = globalState::DOFBlurExtent,
	};

	std::vector<IncrementalHalveCoCParams> incrementalReductions;
	incrementalReductions.reserve(NUM_INCREMENTAL_COC_REDUCTIONS);
	for (size_t i = 0; i < NUM_INCREMENTAL_COC_REDUCTIONS; i++)
	{
		std::string materialName = "incrementalReductionHalveCoCMaterial_" + std::to_string(i);
		incrementalReductions.push_back({
			.framebuffer = _incrementalReductionHalveCoCFramebuffers[i],
			.material = getMaterial(materialName),
			.imageExtent = _incrementalReductionHalveResImageExtents[i],
		});
	}

	GPUBlurParams blurParams = {};
	blurParams.oneOverImageExtent[0] = 1.0f / _incrementalReductionHalveResImageExtents[NUM_INCREMENTAL_COC_REDUCTIONS - 1].width;
	blurParams.oneOverImageExtent[1] = 1.0f / _incrementalReductionHalveResImageExtents[NUM_INCREMENTAL_COC_REDUCTIONS - 1].height;

	GPUGatherDOFParams dofParams = {
		.sampleRadiusMultiplier = _DOFSampleRadiusMultiplier,
	};
	constexpr float_t arbitraryHeight = 100.0f;
	dofParams.oneOverArbitraryResExtentX = 1.0f / (arbitraryHeight * _camera->sceneCamera.aspect);
	dofParams.oneOverArbitraryResExtentY = 1.0f / arbitraryHeight;

	GPUBlurParams floodfillParams = {};
	floodfillParams.oneOverImageExtent[0] = 1.0f / _halfResImageExtent.width;
	floodfillParams.oneOverImageExtent[1] = 1.0f / _halfResImageExtent.height;


	ppDepthOfField(
		cmd,
		_CoCRenderPass,
		_CoCFramebuffer,
		*getMaterial("CoCMaterial"),
		CoCParams,
		_windowExtent,
		_halveCoCRenderPass,
		_halveCoCFramebuffer,
		*getMaterial("halveCoCMaterial"),
		_halfResImageExtent,
		_incrementalReductionHalveCoCRenderPass,
		incrementalReductions,
		_blurXNearsideCoCRenderPass,
		_blurXNearsideCoCFramebuffer,
		*getMaterial("blurXSingleChannelMaterial"),
		_blurYNearsideCoCRenderPass,
		_blurYNearsideCoCFramebuffer,
		*getMaterial("blurYSingleChannelMaterial"),
		blurParams,
		_gatherDOFRenderPass,
		_gatherDOFFramebuffer,
		*getMaterial("gatherDOFMaterial"),
		dofParams,
		_dofFloodFillRenderPass,
		_dofFloodFillFramebuffer,
		*getMaterial("DOFFloodFillMaterial"),
		floodfillParams
	);

	// Blit result to snapshot image.
	if (_blitToSnapshotImageFlag)
	{
		// Combine postprocesses without tonemapping.
		ppCombinePostprocesses(
			cmd,
			_postprocessRenderPass,
			_swapchainFramebuffers[swapchainImageIndex],
			*getMaterial("postprocessMaterial"),
			_windowExtent,
			currentFrame.globalDescriptor,
			false,
			false
		);

		// Do blitting process.
		VkImageMemoryBarrier imageBarrier = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.subresourceRange = {
				.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel   = 0,
				.levelCount     = 1,
				.baseArrayLayer = 0,
				.layerCount     = 1,
			},
		};

		// Convert KHR image to transfer src.
		imageBarrier.image = _swapchainImages[swapchainImageIndex];
		imageBarrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		imageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		imageBarrier.srcAccessMask = VK_ACCESS_NONE;
		imageBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
			0, nullptr,
			0, nullptr,
			1, &imageBarrier
		);

		// Convert snapshot image to transfer dst.
		imageBarrier.image = _snapshotImage.image._image;
		imageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		imageBarrier.srcAccessMask = VK_ACCESS_NONE;
		imageBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
			0, nullptr,
			0, nullptr,
			1, &imageBarrier
		);

		// Blit.
		VkImageBlit blitRegion = {
			.srcSubresource = {
				.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel       = 0,
				.baseArrayLayer = 0,
				.layerCount     = 1,
			},
			.srcOffsets = {
				{ 0, 0, 0 },
				{ (int32_t)_windowExtent.width, (int32_t)_windowExtent.height, 1 },
			},
			.dstSubresource = {
				.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel       = 0,
				.baseArrayLayer = 0,
				.layerCount     = 1,
			},
			.dstOffsets = {
				{ 0, 0, 0 },
				{
					(int32_t)_windowExtent.width,
					(int32_t)_windowExtent.height,
					1
				},
			},
		};
		vkCmdBlitImage(cmd,
			_swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			_snapshotImage.image._image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &blitRegion,
			VK_FILTER_NEAREST
		);

		// Convert KHR image back to KHR present src.
		imageBarrier.image = _swapchainImages[swapchainImageIndex];
		imageBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		imageBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		imageBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		imageBarrier.dstAccessMask = VK_ACCESS_NONE;
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
			0, nullptr,
			0, nullptr,
			1, &imageBarrier
		);

		// Convert snapshot image to shader read only.
		imageBarrier.image = _snapshotImage.image._image;
		imageBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		imageBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		imageBarrier.dstAccessMask = VK_ACCESS_NONE;
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
			0, nullptr,
			0, nullptr,
			1, &imageBarrier
		);

		// Finish.
		_blitToSnapshotImageFlag = false;
	}

	// Finish postprocess stack.
	ppCombinePostprocesses(
		cmd,
		_postprocessRenderPass,
		_swapchainFramebuffers[swapchainImageIndex],
		*getMaterial("postprocessMaterial"),
		_windowExtent,
		currentFrame.globalDescriptor,
		true,
		true
	);
}

void VulkanEngine::render()
{
	const auto& currentFrame = getCurrentFrame();
	VkResult result;

	// Wait until GPU finishes rendering the previous frame
	result = vkWaitForFences(_device, 1, &currentFrame.renderFence, true, TIMEOUT_1_SEC);
	if (result == VK_ERROR_DEVICE_LOST)
	{
		std::cerr << "ERROR: VULKAN DEVICE LOST." << std::endl;
		return;
	}

	VK_CHECK(vkResetFences(_device, 1, &currentFrame.renderFence));

	//
	// Request image from swapchain
	//
	uint32_t swapchainImageIndex;
	result = vkAcquireNextImageKHR(_device, _swapchain, TIMEOUT_1_SEC, currentFrame.presentSemaphore, nullptr, &swapchainImageIndex);
	if (result == VK_ERROR_OUT_OF_DATE_KHR)
	{
		_recreateSwapchain = true;
		return;
	}
	else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
	{
		throw std::runtime_error("ERROR: failed to acquire swap chain image!");
	}

	//
	// Reset command buffer to start recording commands again
	//
	VK_CHECK(vkResetCommandBuffer(currentFrame.mainCommandBuffer, 0));
	VkCommandBuffer cmd = currentFrame.mainCommandBuffer;
	VkCommandBufferBeginInfo cmdBeginInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.pNext = nullptr,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		.pInheritanceInfo = nullptr,
	};
	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	//
	// Upload current frame to GPU and compact into draw calls
	//
	recreateVoxelLightingDescriptor();
	uploadCurrentFrameToGPU(currentFrame);
	textmesh::uploadUICameraDataToGPU();

#ifdef _DEVELOP
	std::vector<size_t> pickedPoolIndices = { 0 };
	if (!searchForPickedObjectPoolIndex(pickedPoolIndices[0]))
		pickedPoolIndices.clear();
	std::vector<ModelWithIndirectDrawId> pickingIndirectDrawCommandIds;
#endif

	perfs[14] = SDL_GetPerformanceCounter();
	if (_roManager->checkIsMetaMeshListUnoptimized())
	{
		_roManager->optimizeMetaMeshList();
		for (size_t i = 0; i < FRAME_OVERLAP; i++)
			_frames[i].skinning.recalculateSkinningBuffers = true;
	}
	if (currentFrame.skinning.recalculateSkinningBuffers)
		createSkinningBuffers(getCurrentFrame());

	if (doCullingStuff)
		compactRenderObjectsIntoDraws(getCurrentFrame(), pickedPoolIndices, pickingIndirectDrawCommandIds);
	perfs[14] = SDL_GetPerformanceCounter() - perfs[14];

	// Render render passes.
	if (doCullingStuff)
	{
		computeShadowCulling(currentFrame, cmd);
		computeMainCulling(currentFrame, cmd);
	}
	computeSkinnedMeshes(currentFrame, cmd);
	renderShadowRenderpass(currentFrame, cmd);
	renderMainRenderpass(currentFrame, cmd, pickingIndirectDrawCommandIds);
	renderUIRenderpass(cmd);
	renderPostprocessRenderpass(currentFrame, cmd, swapchainImageIndex);

	//
	// Submit command buffer to gpu for execution
	//
	VK_CHECK(vkEndCommandBuffer(cmd));

	VkSubmitInfo submit = {};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.pNext = nullptr;

	VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	submit.pWaitDstStageMask = &waitStage;

	submit.waitSemaphoreCount = 1;
	submit.pWaitSemaphores = &currentFrame.presentSemaphore;

	submit.signalSemaphoreCount = 1;
	submit.pSignalSemaphores = &currentFrame.renderSemaphore;

	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &cmd;

	// Submit work to gpu
	result = vkQueueSubmit(_graphicsQueue, 1, &submit, currentFrame.renderFence);
	if (result == VK_ERROR_DEVICE_LOST)
	{
		std::cerr << "ERROR: VULKAN DEVICE LOST." << std::endl;
		return;
	}

	//
	// Picking Render Pass (OPTIONAL AND SEPARATE)
	//
	if (_currentEditorMode == EditorModes::LEVEL_EDITOR &&
		input::editorInputSet().pickObject.onAction &&
		_camera->getCameraMode() == Camera::_cameraMode_freeCamMode &&
		!_camera->freeCamMode.enabled &&
		!ImGui::GetIO().WantCaptureMouse &&
		(_movingMatrix.matrixToMove != nullptr ?
			!ImGuizmo::IsUsing() && !ImGuizmo::IsOver() :
			true) &&
		ImGui::IsMousePosValid())
	{
		renderPickingRenderpass(currentFrame);
	}

	//
	// Present the rendered frame to the screen
	//
	VkPresentInfoKHR presentInfo = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.pNext = nullptr,

		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &currentFrame.renderSemaphore,

		.swapchainCount = 1,
		.pSwapchains = &_swapchain,

		.pImageIndices = &swapchainImageIndex,
	};

	result = vkQueuePresentKHR(_graphicsQueue, &presentInfo);
	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
	{
		_recreateSwapchain = true;
	}
	else if (result != VK_SUCCESS)
	{
		throw std::runtime_error("ERROR: failed to present swap chain image!");
	}

	//
	// End of frame!
	//
	_frameNumber++;
}

void VulkanEngine::loadImages()
{
	// @NOTE: @NOCHECKIN: This needs to be resolved. Do we keep this or discard this? Images should be loaded in with ktx loaders now.
	// // Load empty
	// {
	// 	Texture empty;
	// 	vkutil::loadImageFromFile(*this, "res/texture_pool/empty.png", VK_FORMAT_R8G8B8A8_UNORM, 1, empty.image);

	// 	VkImageViewCreateInfo imageInfo = vkinit::imageviewCreateInfo(VK_FORMAT_R8G8B8A8_UNORM, empty.image._image, VK_IMAGE_ASPECT_COLOR_BIT, empty.image._mipLevels);
	// 	vkCreateImageView(_device, &imageInfo, nullptr, &empty.imageView);

	// 	VkSamplerCreateInfo samplerInfo = vkinit::samplerCreateInfo(static_cast<float_t>(empty.image._mipLevels), VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);
	// 	vkCreateSampler(_device, &samplerInfo, nullptr, &empty.sampler);

	// 	_mainDeletionQueue.pushFunction([=]() {
	// 		vkDestroySampler(_device, empty.sampler, nullptr);
	// 		vkDestroyImageView(_device, empty.imageView, nullptr);
	// 	});

	// 	_loadedTextures["empty"] = empty;
	// }
	struct ImageFnameName
	{
		std::string fname;
		std::string textureName;
	};
	std::vector<ImageFnameName> fnameNames = {
		{ "empty.hdelicious", "empty" },
		{ "empty3d.hdelicious", "empty3d" },
		{ "_develop_icon_layer_visible.hdelicious", "imguiTextureLayerVisible" },
		{ "_develop_icon_layer_invisible.hdelicious", "imguiTextureLayerInvisible" },
		{ "_develop_icon_layer_builder.hdelicious", "imguiTextureLayerBuilder" },
		{ "_develop_icon_layer_collision.hdelicious", "imguiTextureLayerCollision" },
	};
	for (auto& fn : fnameNames)
	{
		uint32_t dimensions;
		Texture tex;
		VkFormat format;
		vkutil::loadKTXImageFromFile(*this, ("res/texture_cooked/" + fn.fname).c_str(), dimensions, /*VK_FORMAT_R8G8B8A8_UNORM*/format, tex.image);

		VkImageViewCreateInfo imageInfo =
			(dimensions == 3 ?
			vkinit::imageview3DCreateInfo(format, tex.image._image, VK_IMAGE_ASPECT_COLOR_BIT, tex.image._mipLevels) :
			vkinit::imageviewCreateInfo(format, tex.image._image, VK_IMAGE_ASPECT_COLOR_BIT, tex.image._mipLevels));
		vkCreateImageView(_device, &imageInfo, nullptr, &tex.imageView);

		VkSamplerCreateInfo samplerInfo = vkinit::samplerCreateInfo(static_cast<float_t>(tex.image._mipLevels), VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);
		vkCreateSampler(_device, &samplerInfo, nullptr, &tex.sampler);

		_mainDeletionQueue.pushFunction([=]() {
			vkDestroySampler(_device, tex.sampler, nullptr);
			vkDestroyImageView(_device, tex.imageView, nullptr);
		});

		_loadedTextures[fn.textureName] = tex;
	}


	// // Load imguiTextureLayerVisible
	// {
	// 	Texture textureLayerVisible;
	// 	vkutil::loadImageFromFile(*this, "res/_develop/icon_layer_visible.png", VK_FORMAT_R8G8B8A8_SRGB, 0, textureLayerVisible.image);

	// 	VkImageViewCreateInfo imageInfo = vkinit::imageviewCreateInfo(VK_FORMAT_R8G8B8A8_SRGB, textureLayerVisible.image._image, VK_IMAGE_ASPECT_COLOR_BIT, textureLayerVisible.image._mipLevels);
	// 	vkCreateImageView(_device, &imageInfo, nullptr, &textureLayerVisible.imageView);

	// 	VkSamplerCreateInfo samplerInfo = vkinit::samplerCreateInfo(static_cast<float_t>(textureLayerVisible.image._mipLevels), VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);
	// 	vkCreateSampler(_device, &samplerInfo, nullptr, &textureLayerVisible.sampler);

	// 	_mainDeletionQueue.pushFunction([=]() {
	// 		vkDestroySampler(_device, textureLayerVisible.sampler, nullptr);
	// 		vkDestroyImageView(_device, textureLayerVisible.imageView, nullptr);
	// 		});

	// 	_loadedTextures["imguiTextureLayerVisible"] = textureLayerVisible;
	// }

	// // Load imguiTextureLayerInvisible
	// {
	// 	Texture textureLayerInvisible;
	// 	vkutil::loadImageFromFile(*this, "res/_develop/icon_layer_invisible.png", VK_FORMAT_R8G8B8A8_SRGB, 0, textureLayerInvisible.image);

	// 	VkImageViewCreateInfo imageInfo = vkinit::imageviewCreateInfo(VK_FORMAT_R8G8B8A8_SRGB, textureLayerInvisible.image._image, VK_IMAGE_ASPECT_COLOR_BIT, textureLayerInvisible.image._mipLevels);
	// 	vkCreateImageView(_device, &imageInfo, nullptr, &textureLayerInvisible.imageView);

	// 	VkSamplerCreateInfo samplerInfo = vkinit::samplerCreateInfo(static_cast<float_t>(textureLayerInvisible.image._mipLevels), VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);
	// 	vkCreateSampler(_device, &samplerInfo, nullptr, &textureLayerInvisible.sampler);

	// 	_mainDeletionQueue.pushFunction([=]() {
	// 		vkDestroySampler(_device, textureLayerInvisible.sampler, nullptr);
	// 		vkDestroyImageView(_device, textureLayerInvisible.imageView, nullptr);
	// 		});

	// 	_loadedTextures["imguiTextureLayerInvisible"] = textureLayerInvisible;
	// }

	// // Load imguiTextureLayerBuilder
	// {
	// 	Texture textureLayerBuilder;
	// 	vkutil::loadImageFromFile(*this, "res/_develop/icon_layer_builder.png", VK_FORMAT_R8G8B8A8_SRGB, 0, textureLayerBuilder.image);

	// 	VkImageViewCreateInfo imageInfo = vkinit::imageviewCreateInfo(VK_FORMAT_R8G8B8A8_SRGB, textureLayerBuilder.image._image, VK_IMAGE_ASPECT_COLOR_BIT, textureLayerBuilder.image._mipLevels);
	// 	vkCreateImageView(_device, &imageInfo, nullptr, &textureLayerBuilder.imageView);

	// 	VkSamplerCreateInfo samplerInfo = vkinit::samplerCreateInfo(static_cast<float_t>(textureLayerBuilder.image._mipLevels), VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);
	// 	vkCreateSampler(_device, &samplerInfo, nullptr, &textureLayerBuilder.sampler);

	// 	_mainDeletionQueue.pushFunction([=]() {
	// 		vkDestroySampler(_device, textureLayerBuilder.sampler, nullptr);
	// 		vkDestroyImageView(_device, textureLayerBuilder.imageView, nullptr);
	// 		});

	// 	_loadedTextures["imguiTextureLayerBuilder"] = textureLayerBuilder;
	// }

	// // Load imguiTextureLayerCollision  @NOTE: this is a special case. It's not a render layer but rather a toggle to see the debug shapes rendered
	// {
	// 	Texture textureLayerCollision;
	// 	vkutil::loadImageFromFile(*this, "res/_develop/icon_layer_collision.png", VK_FORMAT_R8G8B8A8_SRGB, 0, textureLayerCollision.image);

	// 	VkImageViewCreateInfo imageInfo = vkinit::imageviewCreateInfo(VK_FORMAT_R8G8B8A8_SRGB, textureLayerCollision.image._image, VK_IMAGE_ASPECT_COLOR_BIT, textureLayerCollision.image._mipLevels);
	// 	vkCreateImageView(_device, &imageInfo, nullptr, &textureLayerCollision.imageView);

	// 	VkSamplerCreateInfo samplerInfo = vkinit::samplerCreateInfo(static_cast<float_t>(textureLayerCollision.image._mipLevels), VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);
	// 	vkCreateSampler(_device, &samplerInfo, nullptr, &textureLayerCollision.sampler);

	// 	_mainDeletionQueue.pushFunction([=]() {
	// 		vkDestroySampler(_device, textureLayerCollision.sampler, nullptr);
	// 		vkDestroyImageView(_device, textureLayerCollision.imageView, nullptr);
	// 		});

	// 	_loadedTextures["imguiTextureLayerCollision"] = textureLayerCollision;
	// }

	// Initialize the shadow jitter image
	{
		size_t pixelSize = 4 * SHADOWMAP_JITTERMAP_DIMENSION_X * SHADOWMAP_JITTERMAP_DIMENSION_Y * SHADOWMAP_JITTERMAP_DIMENSION_Z;
		float_t* pixels = new float_t[pixelSize];

		std::default_random_engine generator;
		std::uniform_real_distribution<float_t> distribution(0.0f, 1.0f);

		constexpr uint32_t SHADOWMAP_JITTERMAP_DIMENSION_Z_2 = SHADOWMAP_JITTERMAP_DIMENSION_Z * 2;
		uint32_t offsetDimension = (uint32_t)std::sqrt(SHADOWMAP_JITTERMAP_DIMENSION_Z_2);

		for (uint32_t i = 0; i < SHADOWMAP_JITTERMAP_DIMENSION_X; i++)
		for (uint32_t j = 0; j < SHADOWMAP_JITTERMAP_DIMENSION_Y; j++)
		{
			size_t cursor = 4 * (j * SHADOWMAP_JITTERMAP_DIMENSION_X + i);
			for (uint32_t z = 0; z < SHADOWMAP_JITTERMAP_DIMENSION_Z_2; z++)
			{
				uint32_t reversedZ = SHADOWMAP_JITTERMAP_DIMENSION_Z_2 - z - 1;  // Reverse so that first samples are the outermost ring.
				vec2 uv = {
					reversedZ % offsetDimension + distribution(generator),
					reversedZ / offsetDimension + distribution(generator),
				};
				vec2 uvWarped = {
					std::sqrt(uv[1]) * std::cos(2.0f * M_PI * uv[0]),
					std::sqrt(uv[1]) * std::sin(2.0f * M_PI * uv[0]),
				};

				if (z % 2 == 0)
				{
					pixels[cursor + 0] = uvWarped[0];
					pixels[cursor + 1] = uvWarped[1];
				}
				else
				{
					pixels[cursor + 2] = uvWarped[0];
					pixels[cursor + 3] = uvWarped[1];
					cursor += 4 * SHADOWMAP_JITTERMAP_DIMENSION_X * SHADOWMAP_JITTERMAP_DIMENSION_Y;
				}
			}
		}

		// @NOTE: in the future, you could blit this into an RGBA16 float (there isn't a native cpp half float (which is why the buffer is uploaded as RGBA32) so it'd have to be a blit), or an RGBA8 SNORM if they allow it, just to save on vram.
		vkutil::loadImage3DFromBuffer(*this, SHADOWMAP_JITTERMAP_DIMENSION_X, SHADOWMAP_JITTERMAP_DIMENSION_Y, SHADOWMAP_JITTERMAP_DIMENSION_Z, pixelSize * sizeof(float_t), VK_FORMAT_R32G32B32A32_SFLOAT, (void*)pixels, _pbrSceneTextureSet.shadowJitterMap.image);
		delete[] pixels;

		VkImageViewCreateInfo shadowJitterImageViewInfo = vkinit::imageview3DCreateInfo(VK_FORMAT_R32G32B32A32_SFLOAT, _pbrSceneTextureSet.shadowJitterMap.image._image, VK_IMAGE_ASPECT_COLOR_BIT, 1);
		VK_CHECK(vkCreateImageView(_device, &shadowJitterImageViewInfo, nullptr, &_pbrSceneTextureSet.shadowJitterMap.imageView));

		VkSamplerCreateInfo jitterSamplerInfo = vkinit::samplerCreateInfo(1.0f, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT, false);
		VK_CHECK(vkCreateSampler(_device, &jitterSamplerInfo, nullptr, &_pbrSceneTextureSet.shadowJitterMap.sampler));

		_mainDeletionQueue.pushFunction([=]() {
			vkDestroySampler(_device, _pbrSceneTextureSet.shadowJitterMap.sampler, nullptr);
			vkDestroyImageView(_device, _pbrSceneTextureSet.shadowJitterMap.imageView, nullptr);
			});
	}

	//
	// @TODO: add a thing to destroy all the loaded images from _loadedTextures hashmap
	//
}

void VulkanEngine::initVoxelLightingDescriptor()
{
	_voxelFieldLightingGridTextureSet.textures.resize(1, _loadedTextures["empty3d"]);
	_voxelFieldLightingGridTextureSet.transforms.resize(1);
	glm_mat4_identity(_voxelFieldLightingGridTextureSet.transforms[0].transform);

	// Prop up the transforms buffer
	_voxelFieldLightingGridTextureSet.transformsBuffer = createBuffer(sizeof(VoxelFieldLightingGridTextureSet::GPUTransform) * MAX_NUM_VOXEL_FIELD_LIGHTMAPS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

	_mainDeletionQueue.pushFunction([=]() {
		vmaDestroyBuffer(_allocator, _voxelFieldLightingGridTextureSet.transformsBuffer._buffer, _voxelFieldLightingGridTextureSet.transformsBuffer._allocation);
	});

	// Setup initial descriptor set and layout.
	_voxelFieldLightingGridTextureSet.flagRecreateTextureSet = true;
	recreateVoxelLightingDescriptor();
}

void VulkanEngine::recreateVoxelLightingDescriptor()
{
	// Upload transforms.
	void* data;
	vmaMapMemory(_allocator, _voxelFieldLightingGridTextureSet.transformsBuffer._allocation, &data);
	memcpy(
		data,
		_voxelFieldLightingGridTextureSet.transforms.data(),
		sizeof(VoxelFieldLightingGridTextureSet::GPUTransform) * std::min(MAX_NUM_VOXEL_FIELD_LIGHTMAPS, _voxelFieldLightingGridTextureSet.transforms.size())
	);
	vmaUnmapMemory(_allocator, _voxelFieldLightingGridTextureSet.transformsBuffer._allocation);

	// Check if need to reupload images.
	if (!_voxelFieldLightingGridTextureSet.flagRecreateTextureSet)
		return;  // No changes detected. Abort.

	// Reupload images.
	VkDescriptorImageInfo* lightgridImageInfos = new VkDescriptorImageInfo[MAX_NUM_VOXEL_FIELD_LIGHTMAPS];  // @COPYPASTA
	for (size_t i = 0; i < MAX_NUM_VOXEL_FIELD_LIGHTMAPS; i++)  // @TODO: make this an expandable array.
		lightgridImageInfos[i] =
			(i < _voxelFieldLightingGridTextureSet.textures.size()) ?
			vkinit::textureToDescriptorImageInfo(&_voxelFieldLightingGridTextureSet.textures[i]) :
			vkinit::textureToDescriptorImageInfo(&_voxelFieldLightingGridTextureSet.textures[0]);

	VkDescriptorBufferInfo transformsBufferInfo = {
		.buffer = _voxelFieldLightingGridTextureSet.transformsBuffer._buffer,
		.offset = 0,
		.range = sizeof(VoxelFieldLightingGridTextureSet::GPUTransform) * MAX_NUM_VOXEL_FIELD_LIGHTMAPS,
	};

	vkutil::DescriptorBuilder::begin()
		.bindBuffer(0, &transformsBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
		.bindImageArray(1, MAX_NUM_VOXEL_FIELD_LIGHTMAPS, lightgridImageInfos, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
		.build(_voxelFieldLightingGridTextureSet.descriptor, _voxelFieldLightingGridTextureSet.layout);

	_voxelFieldLightingGridTextureSet.flagRecreateTextureSet = false;
}

Material* VulkanEngine::attachPipelineToMaterial(VkPipeline pipeline, VkPipelineLayout layout, const std::string& name)
{
	Material material = {};
	Material* alreadyExistsMaterial = getMaterial(name);
	if (alreadyExistsMaterial != nullptr)
	{
		// Copy over the texture descriptorset
		material.textureSet = alreadyExistsMaterial->textureSet;
	}
	material.pipeline = pipeline;
	material.pipelineLayout = layout;

	_materials[name] = material;
	return &_materials[name];
}

Material* VulkanEngine::attachTextureSetToMaterial(VkDescriptorSet textureSet, const std::string& name)
{
	Material material = {};
	Material* alreadyExistsMaterial = getMaterial(name);
	if (alreadyExistsMaterial != nullptr)
	{
		// Copy over the pipeline and layout
		material.pipeline = alreadyExistsMaterial->pipeline;
		material.pipelineLayout = alreadyExistsMaterial->pipelineLayout;
	}
	material.textureSet = textureSet;

	_materials[name] = material;
	return &_materials[name];
}

Material* VulkanEngine::getMaterial(const std::string& name)
{
	auto it = _materials.find(name);
	if (it == _materials.end())
		return nullptr;
	return &it->second;
}

AllocatedBuffer VulkanEngine::createBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
{
	VkBufferCreateInfo bufferInfo = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.pNext = nullptr,
		.size = allocSize,
		.usage = usage,
	};
	VmaAllocationCreateInfo vmaAllocInfo = {
		.usage = memoryUsage,
	};

	AllocatedBuffer newBuffer;
	VK_CHECK(vmaCreateBuffer(_allocator, &bufferInfo, &vmaAllocInfo, &newBuffer._buffer, &newBuffer._allocation, nullptr));
	return newBuffer;
}

size_t VulkanEngine::padUniformBufferSize(size_t originalSize)
{
	// https://github.com/SaschaWillems/Vulkan/tree/master/examples/dynamicuniformbuffer
	size_t minUboAlignment = _gpuProperties.limits.minUniformBufferOffsetAlignment;
	size_t alignedSize = originalSize;
	if (minUboAlignment > 0)
		alignedSize = (alignedSize + minUboAlignment - 1) & ~(minUboAlignment - 1);
	return alignedSize;
}

void VulkanEngine::immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function)
{
	static std::mutex mutex;
	std::lock_guard<std::mutex> cmdBufLockGuard(mutex);

	VkCommandBuffer cmd = _uploadContext.commandBuffer;
	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::commandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	function(cmd);

	VK_CHECK(vkEndCommandBuffer(cmd));
	VkSubmitInfo submit = vkinit::submitInfo(&cmd);

	VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submit, _uploadContext.uploadFence));
	vkWaitForFences(_device, 1, &_uploadContext.uploadFence, true, 9999999999);
	vkResetFences(_device, 1, &_uploadContext.uploadFence);

	vkResetCommandPool(_device, _uploadContext.commandPool, 0);
}

void VulkanEngine::initVulkan()
{
	//
	// Setup vulkan instance and debug messenger
	//
	vkb::InstanceBuilder builder;

	auto instance = builder.set_app_name("Hawsoo_Solanine_x64")
		.request_validation_layers(true)
		.require_api_version(1, 3, 0)
		.use_default_debug_messenger()
		.build();

	vkb::Instance vkbInstance = instance.value();
	_instance = vkbInstance.instance;
	_debugMessenger = vkbInstance.debug_messenger;

	//
	// Select physical device
	//
	SDL_Vulkan_CreateSurface(_window, _instance, &_surface);

	vkb::PhysicalDeviceSelector selector{ vkbInstance };
	vkb::PhysicalDevice physicalDevice = selector
		.set_minimum_version(1, 3)  // I thought draw indirect count was in 1.3, but it's in 1.2. Idk any other reason to have 1.3 be a requirement.
		.set_surface(_surface)
		.set_required_features({
			// @NOTE: @FEATURES: Enable required features right here
			.multiDrawIndirect = VK_TRUE,           // @NOTE: this is so that vkCmdDrawIndexedIndirect() can be called with a >1 drawCount
			.depthClamp = VK_TRUE,				    // @NOTE: for shadow maps, this is really nice
			.fillModeNonSolid = VK_TRUE,            // @NOTE: well, I guess this is necessary to render wireframes
			.samplerAnisotropy = VK_TRUE,
			.fragmentStoresAndAtomics = VK_TRUE,    // @NOTE: this is only necessary for the picking buffer! If a release build then you can just disable this feature (@NOTE: it allows for me to write into an ssbo in the fragment shader. The picking buffer shader would have to be readonly if this were disabled)  -Timo 2022/10/21
			})
		.select()
		.value();

	//
	// Create vulkan device
	// @NOTE: @FEATURES: Enable device features right here.
	//
	vkb::DeviceBuilder deviceBuilder{ physicalDevice };
	VkPhysicalDeviceShaderDrawParametersFeatures shaderDrawParametersFeatures = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES,
		.pNext = nullptr,
		.shaderDrawParameters = VK_TRUE,
	};
	VkPhysicalDeviceVulkan12Features vulkan12Features = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
		.pNext = nullptr,
		// For `vkCmdDrawIndexedIndirectCount`
		.drawIndirectCount = VK_TRUE,
		// For non-uniform, dynamic arrays of textures in shaders.
		.descriptorIndexing = VK_TRUE,
		.shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
		.descriptorBindingVariableDescriptorCount = VK_TRUE,
		.runtimeDescriptorArray = VK_TRUE,
		// For MIN/MAX sampler when creating mip chains.
		.samplerFilterMinmax = VK_TRUE,
	};
	vkb::Device vkbDevice =
		deviceBuilder
			.add_pNext(&shaderDrawParametersFeatures)
			.add_pNext(&vulkan12Features)
			.build()
			.value();

	_device = vkbDevice.device;
	_chosenGPU = physicalDevice.physical_device;
	_gpuProperties = physicalDevice.properties;

	_graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	_graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

	//
	// Initialize memory allocator
	//
	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.physicalDevice = _chosenGPU;
	allocatorInfo.device = _device;
	allocatorInfo.instance = _instance;
	vmaCreateAllocator(&allocatorInfo, &_allocator);

	//
	// Setup misc
	//
	vkutil::descriptorallocator::init(_device);
	vkutil::descriptorlayoutcache::init(_device);
	vkutil::pipelinelayoutcache::init(_device);
	textmesh::init(this);
	textbox::init(this);
	materialorganizer::init(this);
	vkinit::_maxSamplerAnisotropy = _gpuProperties.limits.maxSamplerAnisotropy;

	//
	// Spit out phsyical device properties
	//
	std::cout << "[Chosen Physical Device Properties]" << std::endl;
	std::cout << "API_VERSION                          " << VK_API_VERSION_MAJOR(_gpuProperties.apiVersion) << "." << VK_API_VERSION_MINOR(_gpuProperties.apiVersion) << "." << VK_API_VERSION_PATCH(_gpuProperties.apiVersion) << "." << VK_API_VERSION_VARIANT(_gpuProperties.apiVersion) << std::endl;
	std::cout << "DRIVER_VERSION                       " << _gpuProperties.driverVersion << std::endl;
	std::cout << "VENDOR_ID                            " << _gpuProperties.vendorID << std::endl;
	std::cout << "DEVICE_ID                            " << _gpuProperties.deviceID << std::endl;
	std::cout << "DEVICE_TYPE                          " << _gpuProperties.deviceType << std::endl;
	std::cout << "DEVICE_NAME                          " << _gpuProperties.deviceName << std::endl;
	std::cout << "MAX_IMAGE_DIMENSION_1D               " << _gpuProperties.limits.maxImageDimension1D << std::endl;
	std::cout << "MAX_IMAGE_DIMENSION_2D               " << _gpuProperties.limits.maxImageDimension2D << std::endl;
	std::cout << "MAX_IMAGE_DIMENSION_3D               " << _gpuProperties.limits.maxImageDimension3D << std::endl;
	std::cout << "MAX_IMAGE_DIMENSION_CUBE             " << _gpuProperties.limits.maxImageDimensionCube << std::endl;
	std::cout << "MAX_IMAGE_ARRAY_LAYERS               " << _gpuProperties.limits.maxImageArrayLayers << std::endl;
	std::cout << "MAX_SAMPLER_ANISOTROPY               " << _gpuProperties.limits.maxSamplerAnisotropy << std::endl;
	std::cout << "MAX_BOUND_DESCRIPTOR_SETS            " << _gpuProperties.limits.maxBoundDescriptorSets << std::endl;
	std::cout << "MINIMUM_BUFFER_ALIGNMENT             " << _gpuProperties.limits.minUniformBufferOffsetAlignment << std::endl;
	std::cout << "MAX_COLOR_ATTACHMENTS                " << _gpuProperties.limits.maxColorAttachments << std::endl;
	std::cout << "MAX_DRAW_INDIRECT_COUNT              " << _gpuProperties.limits.maxDrawIndirectCount << std::endl;
	std::cout << "MAX_DESCRIPTOR_SET_SAMPLED_IMAGES    " << _gpuProperties.limits.maxDescriptorSetSampledImages << std::endl;
	std::cout << "MAX_DESCRIPTOR_SET_SAMPLERS          " << _gpuProperties.limits.maxDescriptorSetSamplers << std::endl;
	std::cout << "MAX_SAMPLER_ALLOCATION_COUNT         " << _gpuProperties.limits.maxSamplerAllocationCount << std::endl;
	std::cout << std::endl;
}

void VulkanEngine::initSwapchain()
{
	vkb::SwapchainBuilder swapchainBuilder{ _chosenGPU, _device, _surface };
	vkb::Swapchain vkbSwapchain = swapchainBuilder
		.use_default_format_selection()
		.set_desired_present_mode(VK_PRESENT_MODE_MAILBOX_KHR)		// @NOTE: this is "soft" v-sync, where it won't go above the monitor hertz, but it won't immediately go down to 1/2 the framerate if dips below.
		.set_desired_extent(_windowExtent.width, _windowExtent.height)
		.build()
		.value();

	_swapchain = vkbSwapchain.swapchain;
	_swapchainImages = vkbSwapchain.get_images().value();
	_swapchainImageViews = vkbSwapchain.get_image_views().value();
	_swapchainImageFormat = vkbSwapchain.image_format;

	// Add destroy command for cleanup
	_swapchainDependentDeletionQueue.pushFunction([=]() {
		vkDestroySwapchainKHR(_device, _swapchain, nullptr);
		});

	//
	// Create depth buffer
	//
	VkExtent3D depthImgExtent = {
		.width = _windowExtent.width,
		.height = _windowExtent.height,
		.depth = 1
	};

	_depthFormat = VK_FORMAT_D32_SFLOAT;
	VkImageCreateInfo depthImgInfo = vkinit::imageCreateInfo(_depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, depthImgExtent, 1);
	VmaAllocationCreateInfo depthImgAllocInfo = {
		.usage = VMA_MEMORY_USAGE_GPU_ONLY,
		.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
	};
	vmaCreateImage(_allocator, &depthImgInfo, &depthImgAllocInfo, &_depthImage.image._image, &_depthImage.image._allocation, nullptr);

	VkImageViewCreateInfo depthViewInfo = vkinit::imageviewCreateInfo(_depthFormat, _depthImage.image._image, VK_IMAGE_ASPECT_DEPTH_BIT, 1);
	VK_CHECK(vkCreateImageView(_device, &depthViewInfo, nullptr, &_depthImage.imageView));

	VkSamplerCreateInfo depthSamplerInfo = vkinit::samplerCreateInfo(static_cast<float_t>(_depthImage.image._mipLevels), VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);
	vkCreateSampler(_device, &depthSamplerInfo, nullptr, &_depthImage.sampler);

	// Add destroy command
	_swapchainDependentDeletionQueue.pushFunction([=]() {
		vkDestroySampler(_device, _depthImage.sampler, nullptr);
		vkDestroyImageView(_device, _depthImage.imageView, nullptr);
		vmaDestroyImage(_allocator, _depthImage.image._image, _depthImage.image._allocation);
		});
}

void VulkanEngine::initCommands()
{
	VkCommandPoolCreateInfo commandPoolInfo = vkinit::commandPoolCreateInfo(_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	for (size_t i = 0; i < FRAME_OVERLAP; i++)
	{
		// @NOTE: we are creating FRAME_OVERLAP number of command pools (for Doublebuffering etc.)
		VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_frames[i].commandPool));

		// Create main command buffer
		VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::commandBufferAllocateInfo(_frames[i].commandPool, 1);
		VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_frames[i].mainCommandBuffer));

		// Create picking command buffer  @NOTE: commandbufferallocateinfo just says we're gonna allocate 1 commandbuffer from the pool
		VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_frames[i].pickingCommandBuffer));

		// Create indirect draw command buffer
		_frames[i].indirectDrawCommandRawBuffer = createBuffer(sizeof(VkDrawIndexedIndirectCommand) * INSTANCE_PTR_MAX_CAPACITY, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
		_frames[i].indirectShadowPass.indirectDrawCommandsBuffer = createBuffer(sizeof(VkDrawIndexedIndirectCommand) * INSTANCE_PTR_MAX_CAPACITY, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
		_frames[i].indirectMainPass.indirectDrawCommandsBuffer = createBuffer(sizeof(VkDrawIndexedIndirectCommand) * INSTANCE_PTR_MAX_CAPACITY, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
		_frames[i].indirectDrawCommandOffsetsBuffer = createBuffer(sizeof(GPUIndirectDrawCommandOffsetsData) * INSTANCE_PTR_MAX_CAPACITY, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
		_frames[i].indirectShadowPass.indirectDrawCommandCountsBuffer = createBuffer(sizeof(uint32_t) * INSTANCE_PTR_MAX_CAPACITY, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
		_frames[i].indirectMainPass.indirectDrawCommandCountsBuffer = createBuffer(sizeof(uint32_t) * INSTANCE_PTR_MAX_CAPACITY, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

		// Add destroy command for cleanup
		_mainDeletionQueue.pushFunction([=]() {
			vkDestroyCommandPool(_device, _frames[i].commandPool, nullptr);
			vmaDestroyBuffer(_allocator, _frames[i].indirectDrawCommandRawBuffer._buffer, _frames[i].indirectDrawCommandRawBuffer._allocation);
			vmaDestroyBuffer(_allocator, _frames[i].indirectShadowPass.indirectDrawCommandsBuffer._buffer, _frames[i].indirectShadowPass.indirectDrawCommandsBuffer._allocation);
			vmaDestroyBuffer(_allocator, _frames[i].indirectMainPass.indirectDrawCommandsBuffer._buffer, _frames[i].indirectMainPass.indirectDrawCommandsBuffer._allocation);
			vmaDestroyBuffer(_allocator, _frames[i].indirectDrawCommandOffsetsBuffer._buffer, _frames[i].indirectDrawCommandOffsetsBuffer._allocation);
			vmaDestroyBuffer(_allocator, _frames[i].indirectShadowPass.indirectDrawCommandCountsBuffer._buffer, _frames[i].indirectShadowPass.indirectDrawCommandCountsBuffer._allocation);
			vmaDestroyBuffer(_allocator, _frames[i].indirectMainPass.indirectDrawCommandCountsBuffer._buffer, _frames[i].indirectMainPass.indirectDrawCommandCountsBuffer._allocation);
			});
	}

	//
	// Create Command Pool
	//
	VkCommandPoolCreateInfo uploadCommandPoolInfo = vkinit::commandPoolCreateInfo(_graphicsQueueFamily);
	VK_CHECK(vkCreateCommandPool(_device, &uploadCommandPoolInfo, nullptr, &_uploadContext.commandPool));

	// Add destroy command for cleanup
	_mainDeletionQueue.pushFunction([=]() {
		vkDestroyCommandPool(_device, _uploadContext.commandPool, nullptr);
		});

	VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::commandBufferAllocateInfo(_uploadContext.commandPool, 1);
	VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_uploadContext.commandBuffer));
}

void createImageSampler(VkDevice device, uint32_t numMips, VkFilter samplerFilter, VkSamplerAddressMode samplerAddressMode, VkSampler& sampler, DeletionQueue& deletionQueue)
{
	VkSamplerCreateInfo samplerInfo = vkinit::samplerCreateInfo((float_t)numMips, samplerFilter, samplerAddressMode, false);
	VK_CHECK(vkCreateSampler(device, &samplerInfo, nullptr, &sampler));
	
	deletionQueue.pushFunction([=]() {
		vkDestroySampler(device, sampler, nullptr);
		});
}

void createRenderTexture(VmaAllocator allocator, VkDevice device, Texture& texture, VkFormat imageFormat, VkImageUsageFlags usageFlags, VkExtent3D imageExtent, uint32_t numMips, VkImageAspectFlags aspectFlags, VkFilter samplerFilter, VkSamplerAddressMode samplerAddressMode, DeletionQueue& deletionQueue, bool createSampler = true)
{
	VkImageCreateInfo imgInfo = vkinit::imageCreateInfo(imageFormat, usageFlags, imageExtent, numMips);
	VmaAllocationCreateInfo imgAllocInfo = {
		.usage = VMA_MEMORY_USAGE_GPU_ONLY,
	};
	vmaCreateImage(allocator, &imgInfo, &imgAllocInfo, &texture.image._image, &texture.image._allocation, nullptr);
	texture.image._mipLevels = numMips;

	VkImageViewCreateInfo imgViewInfo = vkinit::imageviewCreateInfo(imageFormat, texture.image._image, aspectFlags, numMips);
	VK_CHECK(vkCreateImageView(device, &imgViewInfo, nullptr, &texture.imageView));

	if (createSampler)
		createImageSampler(device, numMips, samplerFilter, samplerAddressMode, texture.sampler, deletionQueue);

	deletionQueue.pushFunction([=]() {
		vkDestroyImageView(device, texture.imageView, nullptr);
		vmaDestroyImage(allocator, texture.image._image, texture.image._allocation);
		});
}

void createFramebuffer(VkDevice device, VkFramebuffer& framebuffer, VkRenderPass renderPass, const std::vector<VkImageView>& attachments, VkExtent2D extent, uint32_t layers, DeletionQueue& deletionQueue)
{
	VkFramebufferCreateInfo fbInfo = {
		.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
		.pNext = nullptr,

		.renderPass = renderPass,
		.attachmentCount = (uint32_t)attachments.size(),
		.pAttachments = attachments.data(),
		.width = extent.width,
		.height = extent.height,
		.layers = layers,
	};

	VK_CHECK(vkCreateFramebuffer(device, &fbInfo, nullptr, &framebuffer));

	deletionQueue.pushFunction([=]() {
		vkDestroyFramebuffer(device, framebuffer, nullptr);
		});
}

void VulkanEngine::initShadowRenderpass()  // @COPYPASTA
{
	//
	// Initialize the renderpass object
	//
	// Depth attachment
	//
	VkAttachmentDescription depthAttachment = {
		.flags = 0,
		.format = _depthFormat,  // @MAYBE... check sampling ability for this? (See Sascha Willem's `getSupportedDepthFormat(true)`
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
	};
	VkAttachmentReference depthAttachmentRef = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
	};

	//
	// Define the subpass to render to the shadow renderpass
	//
	VkSubpassDescription subpass = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = 0,
		.pDepthStencilAttachment = &depthAttachmentRef
	};

	//
	// GPU work ordering dependencies
	//
	VkSubpassDependency dependency0 = {  // I think... this transforms the data to shader depth writing friendly
		.srcSubpass = VK_SUBPASS_EXTERNAL,
		.dstSubpass = 0,
		.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
		.srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
		.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
	};
	VkSubpassDependency dependency1 = {  // This one seems obvious to me that it converts the depth information into a texture to be read by a shader
		.srcSubpass = 0,
		.dstSubpass = VK_SUBPASS_EXTERNAL,
		.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
	};

	//
	// Create the renderpass for the subpass
	//
	VkSubpassDependency dependencies[] = { dependency0, dependency1 };
	VkAttachmentDescription attachments[] = { depthAttachment };
	VkRenderPassCreateInfo renderPassInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &attachments[0],
		.subpassCount = 1,
		.pSubpasses = &subpass,
		.dependencyCount = 2,
		.pDependencies = &dependencies[0]
	};

	VK_CHECK(vkCreateRenderPass(_device, &renderPassInfo, nullptr, &_shadowRenderPass));

	// Add destroy command for cleanup
	_swapchainDependentDeletionQueue.pushFunction([=]() {
		vkDestroyRenderPass(_device, _shadowRenderPass, nullptr);
		});
}

void VulkanEngine::initShadowImages()
{
	//
	// Initialize the shadow images
	// @NOTE: I know that this is kind of a waste, and that
	//        the images should get generated once since recreating the swapchain
	//        doesn't affect the static image size, however, for readability this
	//        is right here... I think that a reorganization should warrant @IMPROVEMENT
	//        in this area.  -Timo 2022/11/1
	//
	VkExtent3D shadowImgExtent = {
		.width = SHADOWMAP_DIMENSION,
		.height = SHADOWMAP_DIMENSION,
		.depth = 1,
	};
	VkImageCreateInfo shadowImgInfo = vkinit::imageCreateInfo(_depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, shadowImgExtent, 1);
	shadowImgInfo.arrayLayers = SHADOWMAP_CASCADES;
	VmaAllocationCreateInfo shadowImgAllocInfo = {
		.usage = VMA_MEMORY_USAGE_GPU_ONLY,
		.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
	};
	vmaCreateImage(_allocator, &shadowImgInfo, &shadowImgAllocInfo, &_pbrSceneTextureSet.shadowMap.image._image, &_pbrSceneTextureSet.shadowMap.image._allocation, nullptr);

	VkImageViewCreateInfo shadowDepthViewInfo = vkinit::imageviewCreateInfo(_depthFormat, _pbrSceneTextureSet.shadowMap.image._image, VK_IMAGE_ASPECT_DEPTH_BIT, 1);
	shadowDepthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
	shadowDepthViewInfo.subresourceRange.layerCount = SHADOWMAP_CASCADES;
	VK_CHECK(vkCreateImageView(_device, &shadowDepthViewInfo, nullptr, &_pbrSceneTextureSet.shadowMap.imageView));

	// Shared sampler for combined shadow map
	VkSamplerCreateInfo samplerInfo = vkinit::samplerCreateInfo(1.0f, /*VK_FILTER_LINEAR*/VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);  // Why linear? it should be nearest if I say so myself.
	VK_CHECK(vkCreateSampler(_device, &samplerInfo, nullptr, &_pbrSceneTextureSet.shadowMap.sampler));

	_mainDeletionQueue.pushFunction([=]() {
		vkDestroySampler(_device, _pbrSceneTextureSet.shadowMap.sampler, nullptr);
		vkDestroyImageView(_device, _pbrSceneTextureSet.shadowMap.imageView, nullptr);
		vmaDestroyImage(_allocator, _pbrSceneTextureSet.shadowMap.image._image, _pbrSceneTextureSet.shadowMap.image._allocation);
		});

	// One framebuffer and imageview per layer of shadow image
	for (uint32_t i = 0; i < SHADOWMAP_CASCADES; i++)
	{
		VkImageViewCreateInfo individualViewInfo = vkinit::imageviewCreateInfo(_depthFormat, _pbrSceneTextureSet.shadowMap.image._image, VK_IMAGE_ASPECT_DEPTH_BIT, 1);
		individualViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
		individualViewInfo.subresourceRange.baseArrayLayer = i;
		individualViewInfo.subresourceRange.layerCount = 1;
		VK_CHECK(vkCreateImageView(_device, &individualViewInfo, nullptr, &_shadowCascades[i].imageView));

		_mainDeletionQueue.pushFunction([=]() {
			vkDestroyImageView(_device, _shadowCascades[i].imageView, nullptr);
			});

		createFramebuffer(
			_device,
			_shadowCascades[i].framebuffer,
			_shadowRenderPass,
			{ _shadowCascades[i].imageView, },
			{ SHADOWMAP_DIMENSION, SHADOWMAP_DIMENSION },
			1,
			_mainDeletionQueue
		);
	}
}

void VulkanEngine::initMainRenderpass()
{
	//
	// Color Attachment
	//
	VkAttachmentDescription colorAttachment = {
		.format = VK_FORMAT_R16G16B16A16_SFLOAT,  // Do we really need the alpha channel?????
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL  // @NOTE: this is for bloom, use VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL if not using bloom
	};
	VkAttachmentReference colorAttachmentRef = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	};

	//
	// Depth attachment
	//
	VkAttachmentDescription depthAttachment = {
		.flags = 0,
		.format = _depthFormat,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL  // For bokeh depth of field.
	};
	VkAttachmentReference depthAttachmentRef = {
		.attachment = 1,
		.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
	};

	//
	// Define the subpass to render to the default renderpass
	//
	VkSubpassDescription zPrepassSubpass = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = 0,
		.pColorAttachments = nullptr,
		.pDepthStencilAttachment = &depthAttachmentRef
	};
	VkSubpassDescription mainSubpass = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = 1,
		.pColorAttachments = &colorAttachmentRef,
		.pDepthStencilAttachment = &depthAttachmentRef
	};

	//
	// GPU work ordering dependencies
	//
	VkSubpassDependency colorDependency = {
		.srcSubpass = VK_SUBPASS_EXTERNAL,
		.dstSubpass = 1,
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
	};
	VkSubpassDependency zPrepassDepthDependency = {
		.srcSubpass = VK_SUBPASS_EXTERNAL,
		.dstSubpass = 0,
		.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
	};
	VkSubpassDependency mainDepthDependency = {
		.srcSubpass = 0,
		.dstSubpass = 1,
		.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
	};

	//
	// Create the renderpass for the subpass
	//
	VkSubpassDependency dependencies[] = { colorDependency, zPrepassDepthDependency, mainDepthDependency };
	VkAttachmentDescription attachments[] = { colorAttachment, depthAttachment };
	VkSubpassDescription subpasses[] = { zPrepassSubpass, mainSubpass };
	VkRenderPassCreateInfo renderPassInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = 2,
		.pAttachments = &attachments[0],
		.subpassCount = 2,
		.pSubpasses = &subpasses[0],
		.dependencyCount = 3,
		.pDependencies = &dependencies[0]
	};

	VK_CHECK(vkCreateRenderPass(_device, &renderPassInfo, nullptr, &_mainRenderPass));

	// Add destroy command for cleanup
	_swapchainDependentDeletionQueue.pushFunction([=]() {
		vkDestroyRenderPass(_device, _mainRenderPass, nullptr);
		});

	//
	// Create image for renderpass  (@NOTE: Depthmap is already created at this point... idk where it's at though.)
	//
	// Color image
	VkExtent3D mainImgExtent = {
		.width = _windowExtent.width,
		.height = _windowExtent.height,
		.depth = 1,
	};
	VkImageCreateInfo mainColorImgInfo = vkinit::imageCreateInfo(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, mainImgExtent, 1);
	VmaAllocationCreateInfo mainImgAllocInfo = {
		.usage = VMA_MEMORY_USAGE_GPU_ONLY,
	};
	vmaCreateImage(_allocator, &mainColorImgInfo, &mainImgAllocInfo, &_mainImage.image._image, &_mainImage.image._allocation, nullptr);

	VkImageViewCreateInfo mainColorViewInfo = vkinit::imageviewCreateInfo(VK_FORMAT_R16G16B16A16_SFLOAT, _mainImage.image._image, VK_IMAGE_ASPECT_COLOR_BIT, 1);
	VK_CHECK(vkCreateImageView(_device, &mainColorViewInfo, nullptr, &_mainImage.imageView));

	VkSamplerCreateInfo samplerInfo = vkinit::samplerCreateInfo(1.0f, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);
	VK_CHECK(vkCreateSampler(_device, &samplerInfo, nullptr, &_mainImage.sampler));

	_swapchainDependentDeletionQueue.pushFunction([=]() {
		vkDestroySampler(_device, _mainImage.sampler, nullptr);
		vkDestroyImageView(_device, _mainImage.imageView, nullptr);
		vmaDestroyImage(_allocator, _mainImage.image._image, _mainImage.image._allocation);
		});
}

void VulkanEngine::initUIRenderpass()    // @NOTE: @COPYPASTA: This is really copypasta of the above function (initMainRenderpass)
{
	//
	// Color Attachment
	//
	VkAttachmentDescription colorAttachment = {
		.format = VK_FORMAT_R8G8B8A8_UNORM,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	};
	VkAttachmentReference colorAttachmentRef = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	};

	//
	// Define the subpass to render to the default renderpass
	//
	VkSubpassDescription subpass = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = 1,
		.pColorAttachments = &colorAttachmentRef,
	};

	//
	// GPU work ordering dependencies
	//
	VkSubpassDependency colorDependency = {
		.srcSubpass = VK_SUBPASS_EXTERNAL,
		.dstSubpass = 0,
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
	};

	//
	// Create the renderpass for the subpass
	//
	VkRenderPassCreateInfo renderPassInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &colorAttachment,
		.subpassCount = 1,
		.pSubpasses = &subpass,
		.dependencyCount = 1,
		.pDependencies = &colorDependency
	};

	VK_CHECK(vkCreateRenderPass(_device, &renderPassInfo, nullptr, &_uiRenderPass));

	// Add destroy command for cleanup
	_swapchainDependentDeletionQueue.pushFunction([=]() {
		vkDestroyRenderPass(_device, _uiRenderPass, nullptr);
		});

	//
	// Create image for renderpass
	//
	// Color image
	VkExtent3D mainImgExtent = {
		.width = _windowExtent.width,
		.height = _windowExtent.height,
		.depth = 1,
	};
	VkImageCreateInfo mainColorImgInfo = vkinit::imageCreateInfo(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, mainImgExtent, 1);
	VmaAllocationCreateInfo mainImgAllocInfo = {
		.usage = VMA_MEMORY_USAGE_GPU_ONLY,
	};
	vmaCreateImage(_allocator, &mainColorImgInfo, &mainImgAllocInfo, &_uiImage.image._image, &_uiImage.image._allocation, nullptr);

	VkImageViewCreateInfo mainColorViewInfo = vkinit::imageviewCreateInfo(VK_FORMAT_R8G8B8A8_UNORM, _uiImage.image._image, VK_IMAGE_ASPECT_COLOR_BIT, 1);
	VK_CHECK(vkCreateImageView(_device, &mainColorViewInfo, nullptr, &_uiImage.imageView));

	VkSamplerCreateInfo samplerInfo = vkinit::samplerCreateInfo(1.0f, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);
	VK_CHECK(vkCreateSampler(_device, &samplerInfo, nullptr, &_uiImage.sampler));

	_swapchainDependentDeletionQueue.pushFunction([=]() {
		vkDestroySampler(_device, _uiImage.sampler, nullptr);
		vkDestroyImageView(_device, _uiImage.imageView, nullptr);
		vmaDestroyImage(_allocator, _uiImage.image._image, _uiImage.image._allocation);
		});
}

void initPostprocessCombineRenderPass(VkDevice device, VkFormat swapchainImageFormat, VkRenderPass& renderPass)
{
	//
	// Color Attachment  (@NOTE: based off the swapchain images)
	//
	VkAttachmentDescription colorAttachment = {
		.format = swapchainImageFormat,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR		// @NOTE: After this renderpass, the image needs to be ready for the display
	};
	VkAttachmentReference colorAttachmentRef = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	};

	//
	// Define the subpasses.
	//
	VkSubpassDescription combineSubpass = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = 1,
		.pColorAttachments = &colorAttachmentRef
	};

	//
	// GPU work ordering dependencies
	//
	VkSubpassDependency colorDependency = {
		.srcSubpass = VK_SUBPASS_EXTERNAL,
		.dstSubpass = 0,
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
	};

	//
	// Create the renderpass for the subpass
	//
	VkSubpassDescription subpasses[] = { combineSubpass };
	VkRenderPassCreateInfo renderPassInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &colorAttachment,
		.subpassCount = 1,
		.pSubpasses = subpasses,
		.dependencyCount = 1,
		.pDependencies = &colorDependency
	};

	VK_CHECK(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass));
}

void initDOF_CoCRenderPass(VkDevice device, VkRenderPass& renderPass)
{
	// Define the attachments and refs.
	VkAttachmentDescription CoC = {
		.format = VK_FORMAT_R16G16_SFLOAT,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
	VkAttachmentReference CoCRef = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	};

	std::vector<VkAttachmentDescription> colorAttachments = { CoC, };
	std::vector<VkAttachmentReference> colorAttachmentRefs = { CoCRef, };

	// Define the subpasses.
	VkSubpassDescription subpass = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = (uint32_t)colorAttachmentRefs.size(),
		.pColorAttachments = colorAttachmentRefs.data(),
	};

	// GPU work ordering dependencies.
	VkSubpassDependency colorDependency = {
		.srcSubpass = VK_SUBPASS_EXTERNAL,
		.dstSubpass = 0,
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
	};

	// Create the renderpass for the subpass.
	VkRenderPassCreateInfo renderPassInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = (uint32_t)colorAttachments.size(),
		.pAttachments = colorAttachments.data(),
		.subpassCount = 1,
		.pSubpasses = &subpass,
		.dependencyCount = 1,
		.pDependencies = &colorDependency
	};

	VK_CHECK(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass));
}

void initDOF_HalveCoCRenderPass(VkDevice device, VkRenderPass& renderPass)
{
	// Define the attachments and refs.
	VkAttachmentDescription nearField = {
		.format = VK_FORMAT_R16G16B16A16_SFLOAT,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
	VkAttachmentReference nearFieldRef = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	};

	VkAttachmentDescription farField = {
		.format = VK_FORMAT_R16G16B16A16_SFLOAT,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
	VkAttachmentReference farFieldRef = {
		.attachment = 1,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	};

	std::vector<VkAttachmentDescription> colorAttachments = { nearField, farField, };
	std::vector<VkAttachmentReference> colorAttachmentRefs = { nearFieldRef, farFieldRef, };

	// Define the subpasses.
	VkSubpassDescription subpass = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = (uint32_t)colorAttachmentRefs.size(),
		.pColorAttachments = colorAttachmentRefs.data(),
	};

	// GPU work ordering dependencies.
	VkSubpassDependency colorDependency = {
		.srcSubpass = VK_SUBPASS_EXTERNAL,
		.dstSubpass = 0,
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
	};

	// Create the renderpass for the subpass.
	VkRenderPassCreateInfo renderPassInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = (uint32_t)colorAttachments.size(),
		.pAttachments = colorAttachments.data(),
		.subpassCount = 1,
		.pSubpasses = &subpass,
		.dependencyCount = 1,
		.pDependencies = &colorDependency
	};

	VK_CHECK(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass));
}

void initDOF_IncrementalReductionHalveCoCRenderPass(VkDevice device, VkRenderPass& renderPass)
{
	// Define the attachments and refs.
	VkAttachmentDescription nearFieldIncrementalReductionHalveCoC = {
		.format = VK_FORMAT_R16_SFLOAT,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
	VkAttachmentReference nearFieldIncrementalReductionHalveCoCRef = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	};

	std::vector<VkAttachmentDescription> colorAttachments = { nearFieldIncrementalReductionHalveCoC, };
	std::vector<VkAttachmentReference> colorAttachmentRefs = { nearFieldIncrementalReductionHalveCoCRef, };

	// Define the subpasses.
	VkSubpassDescription subpass = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = (uint32_t)colorAttachmentRefs.size(),
		.pColorAttachments = colorAttachmentRefs.data(),
	};

	// GPU work ordering dependencies.
	VkSubpassDependency colorDependency = {
		.srcSubpass = VK_SUBPASS_EXTERNAL,
		.dstSubpass = 0,
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
	};

	// Create the renderpass for the subpass.
	VkRenderPassCreateInfo renderPassInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = (uint32_t)colorAttachments.size(),
		.pAttachments = colorAttachments.data(),
		.subpassCount = 1,
		.pSubpasses = &subpass,
		.dependencyCount = 1,
		.pDependencies = &colorDependency
	};

	VK_CHECK(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass));
}

void initDOF_BlurXNearsideCoCRenderPass(VkDevice device, VkRenderPass& renderPass)
{
	// Define the attachments and refs.
	VkAttachmentDescription nearFieldIncrementalReductionHalveResCoCPong = {
		.format = VK_FORMAT_R16_SFLOAT,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
	VkAttachmentReference nearFieldIncrementalReductionHalveResCoCPongRef = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	};

	std::vector<VkAttachmentDescription> colorAttachments = { nearFieldIncrementalReductionHalveResCoCPong, };
	std::vector<VkAttachmentReference> colorAttachmentRefs = { nearFieldIncrementalReductionHalveResCoCPongRef, };

	// Define the subpasses.
	VkSubpassDescription subpass = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = (uint32_t)colorAttachmentRefs.size(),
		.pColorAttachments = colorAttachmentRefs.data(),
	};

	// GPU work ordering dependencies.
	VkSubpassDependency colorDependency = {
		.srcSubpass = VK_SUBPASS_EXTERNAL,
		.dstSubpass = 0,
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
	};

	// Create the renderpass for the subpass.
	VkRenderPassCreateInfo renderPassInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = (uint32_t)colorAttachments.size(),
		.pAttachments = colorAttachments.data(),
		.subpassCount = 1,
		.pSubpasses = &subpass,
		.dependencyCount = 1,
		.pDependencies = &colorDependency
	};

	VK_CHECK(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass));
}

void initDOF_BlurYNearsideCoCRenderPass(VkDevice device, VkRenderPass& renderPass)
{
	// Define the attachments and refs.
	VkAttachmentDescription nearFieldIncrementalReductionHalveResCoCPing = {
		.format = VK_FORMAT_R16_SFLOAT,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
	VkAttachmentReference nearFieldIncrementalReductionHalveResCoCPingRef = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	};

	std::vector<VkAttachmentDescription> colorAttachments = { nearFieldIncrementalReductionHalveResCoCPing, };
	std::vector<VkAttachmentReference> colorAttachmentRefs = { nearFieldIncrementalReductionHalveResCoCPingRef, };

	// Define the subpasses.
	VkSubpassDescription subpass = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = (uint32_t)colorAttachmentRefs.size(),
		.pColorAttachments = colorAttachmentRefs.data(),
	};

	// GPU work ordering dependencies.
	VkSubpassDependency colorDependency = {
		.srcSubpass = VK_SUBPASS_EXTERNAL,
		.dstSubpass = 0,
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
	};

	// Create the renderpass for the subpass.
	VkRenderPassCreateInfo renderPassInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = (uint32_t)colorAttachments.size(),
		.pAttachments = colorAttachments.data(),
		.subpassCount = 1,
		.pSubpasses = &subpass,
		.dependencyCount = 1,
		.pDependencies = &colorDependency
	};

	VK_CHECK(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass));
}

void initDOF_GatherDOFRenderPass(VkDevice device, VkRenderPass& renderPass)
{
	// Define the attachments and refs.
	VkAttachmentDescription nearFieldPong = {
		.format = VK_FORMAT_R16G16B16A16_SFLOAT,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
	VkAttachmentReference nearFieldPongRef = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	};

	VkAttachmentDescription farFieldPong = {
		.format = VK_FORMAT_R16G16B16A16_SFLOAT,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
	VkAttachmentReference farFieldPongRef = {
		.attachment = 1,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	};

	std::vector<VkAttachmentDescription> colorAttachments = { nearFieldPong, farFieldPong, };
	std::vector<VkAttachmentReference> colorAttachmentRefs = { nearFieldPongRef, farFieldPongRef, };

	// Define the subpasses.
	VkSubpassDescription subpass = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = (uint32_t)colorAttachmentRefs.size(),
		.pColorAttachments = colorAttachmentRefs.data(),
	};

	// GPU work ordering dependencies.
	VkSubpassDependency colorDependency = {
		.srcSubpass = VK_SUBPASS_EXTERNAL,
		.dstSubpass = 0,
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
	};

	// Create the renderpass for the subpass.
	VkRenderPassCreateInfo renderPassInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = (uint32_t)colorAttachments.size(),
		.pAttachments = colorAttachments.data(),
		.subpassCount = 1,
		.pSubpasses = &subpass,
		.dependencyCount = 1,
		.pDependencies = &colorDependency
	};

	VK_CHECK(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass));
}

void initDOF_DOFFloodFillRenderPass(VkDevice device, VkRenderPass& renderPass)
{
	// Define the attachments and refs.
	VkAttachmentDescription nearField = {
		.format = VK_FORMAT_R16G16B16A16_SFLOAT,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
	VkAttachmentReference nearFieldRef = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	};

	VkAttachmentDescription farField = {
		.format = VK_FORMAT_R16G16B16A16_SFLOAT,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
	VkAttachmentReference farFieldRef = {
		.attachment = 1,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	};

	std::vector<VkAttachmentDescription> colorAttachments = { nearField, farField, };
	std::vector<VkAttachmentReference> colorAttachmentRefs = { nearFieldRef, farFieldRef, };

	// Define the subpasses.
	VkSubpassDescription subpass = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = (uint32_t)colorAttachmentRefs.size(),
		.pColorAttachments = colorAttachmentRefs.data(),
	};

	// GPU work ordering dependencies.
	VkSubpassDependency colorDependency = {
		.srcSubpass = VK_SUBPASS_EXTERNAL,
		.dstSubpass = 0,
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
	};

	// Create the renderpass for the subpass.
	VkRenderPassCreateInfo renderPassInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = (uint32_t)colorAttachments.size(),
		.pAttachments = colorAttachments.data(),
		.subpassCount = 1,
		.pSubpasses = &subpass,
		.dependencyCount = 1,
		.pDependencies = &colorDependency
	};

	VK_CHECK(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass));
}

void VulkanEngine::initPostprocessRenderpass()    // @NOTE: @COPYPASTA: This is really copypasta of the above function (initMainRenderpass)
{
	initPostprocessCombineRenderPass(_device, _swapchainImageFormat, _postprocessRenderPass);
	initDOF_CoCRenderPass(_device, _CoCRenderPass);
	initDOF_HalveCoCRenderPass(_device, _halveCoCRenderPass);
	initDOF_IncrementalReductionHalveCoCRenderPass(_device, _incrementalReductionHalveCoCRenderPass);
	initDOF_BlurXNearsideCoCRenderPass(_device, _blurXNearsideCoCRenderPass);
	initDOF_BlurYNearsideCoCRenderPass(_device, _blurYNearsideCoCRenderPass);
	initDOF_GatherDOFRenderPass(_device, _gatherDOFRenderPass);
	initDOF_DOFFloodFillRenderPass(_device, _dofFloodFillRenderPass);

	// Add destroy command for cleanup
	_swapchainDependentDeletionQueue.pushFunction([=]() {
		vkDestroyRenderPass(_device, _postprocessRenderPass, nullptr);
		vkDestroyRenderPass(_device, _CoCRenderPass, nullptr);
		vkDestroyRenderPass(_device, _halveCoCRenderPass, nullptr);
		vkDestroyRenderPass(_device, _incrementalReductionHalveCoCRenderPass, nullptr);
		vkDestroyRenderPass(_device, _blurXNearsideCoCRenderPass, nullptr);
		vkDestroyRenderPass(_device, _blurYNearsideCoCRenderPass, nullptr);
		vkDestroyRenderPass(_device, _gatherDOFRenderPass, nullptr);
		vkDestroyRenderPass(_device, _dofFloodFillRenderPass, nullptr);
		});
}

void VulkanEngine::initPostprocessImages()
{
	//
	// Create bloom image
	//
	{
		uint32_t numBloomMips = 5;
		uint32_t startingBloomBufferHeight = _windowExtent.height / 2;
		_bloomPostprocessImageExtent = {
			.width = (uint32_t)((float_t)startingBloomBufferHeight * (float_t)_windowExtent.width / (float_t)_windowExtent.height),
			.height = startingBloomBufferHeight,
		};
		VkExtent3D bloomImgExtent = { _bloomPostprocessImageExtent.width, _bloomPostprocessImageExtent.height, 1 };

		createRenderTexture(
			_allocator,
			_device,
			_bloomPostprocessImage,
			VK_FORMAT_R16G16B16A16_SFLOAT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			bloomImgExtent,
			numBloomMips,
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_FILTER_LINEAR,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			_swapchainDependentDeletionQueue
		);
	}

	//
	// Depth of Field
	//
	{
		// Create Render Textures.
		constexpr uint32_t numMips = 1;

		_halfResImageExtent = {
			.width = (uint32_t)(_windowExtent.width / 2),
			.height = (uint32_t)(_windowExtent.height / 2),
		};
		for (size_t i = 0; i < NUM_INCREMENTAL_COC_REDUCTIONS; i++)
		{
			float_t divisor = std::powf(2.0f, i + 1.0f);
			_incrementalReductionHalveResImageExtents[i] = {
				.width = (uint32_t)(_windowExtent.width / divisor),
				.height = (uint32_t)(_windowExtent.height / divisor),
			};
		}

		VkExtent3D fullImgExtent = { _windowExtent.width, _windowExtent.height, 1 };
		VkExtent3D halfImgExtent = { _halfResImageExtent.width, _halfResImageExtent.height, 1 };
		VkExtent3D incrementalReductionHalveImgExtents[NUM_INCREMENTAL_COC_REDUCTIONS];
		for (size_t i = 0; i < NUM_INCREMENTAL_COC_REDUCTIONS; i++)
			incrementalReductionHalveImgExtents[i] = {
				_incrementalReductionHalveResImageExtents[i].width,
				_incrementalReductionHalveResImageExtents[i].height,
				1
			};

		createRenderTexture(
			_allocator,
			_device,
			_CoCImage,
			VK_FORMAT_R16G16_SFLOAT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			fullImgExtent,
			numMips,
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_FILTER_NEAREST,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			_swapchainDependentDeletionQueue
		);
		{
			// Create special MAX sampler for this texture.
			VkSamplerCreateInfo samplerInfo = vkinit::samplerCreateInfo((float_t)numMips, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);
			samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			samplerInfo.maxLod = 4.0f;  // Should be enough to make an 1/16th of the CoC size.

			VkSamplerReductionModeCreateInfo reductionSamplerInfo = {
				.sType = VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO,
				.pNext = nullptr,
				.reductionMode = VK_SAMPLER_REDUCTION_MODE_MAX,
			};

			samplerInfo.pNext = &reductionSamplerInfo;

			VK_CHECK(vkCreateSampler(_device, &samplerInfo, nullptr, &_CoCImageMaxSampler));

			_swapchainDependentDeletionQueue.pushFunction([=]() {
				vkDestroySampler(_device, _CoCImageMaxSampler, nullptr);
				});
		}

		createRenderTexture(
			_allocator,
			_device,
			_nearFieldImage,
			VK_FORMAT_R16G16B16A16_SFLOAT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			halfImgExtent,
			numMips,
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_FILTER_NEAREST,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			_swapchainDependentDeletionQueue
		);
		createImageSampler(
			_device,
			numMips,
			VK_FILTER_LINEAR,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			_nearFieldImageLinearSampler,
			_swapchainDependentDeletionQueue
		);

		createRenderTexture(
			_allocator,
			_device,
			_farFieldImage,
			VK_FORMAT_R16G16B16A16_SFLOAT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			halfImgExtent,
			numMips,
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_FILTER_NEAREST,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			_swapchainDependentDeletionQueue
		);
		createImageSampler(
			_device,
			numMips,
			VK_FILTER_LINEAR,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			_farFieldImageLinearSampler,
			_swapchainDependentDeletionQueue
		);

		createRenderTexture(
			_allocator,
			_device,
			_nearFieldImagePongImage,
			VK_FORMAT_R16G16B16A16_SFLOAT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			halfImgExtent,
			numMips,
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_FILTER_NEAREST,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			_swapchainDependentDeletionQueue
		);

		createRenderTexture(
			_allocator,
			_device,
			_farFieldImagePongImage,
			VK_FORMAT_R16G16B16A16_SFLOAT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			halfImgExtent,
			numMips,
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_FILTER_NEAREST,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			_swapchainDependentDeletionQueue
		);		

		for (size_t i = 0; i < NUM_INCREMENTAL_COC_REDUCTIONS; i++)
			createRenderTexture(
				_allocator,
				_device,
				_nearFieldIncrementalReductionHalveResCoCImages[i],
				VK_FORMAT_R16_SFLOAT,
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
				incrementalReductionHalveImgExtents[i],
				numMips,
				VK_IMAGE_ASPECT_COLOR_BIT,
				VK_FILTER_NEAREST,
				VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
				_swapchainDependentDeletionQueue
			);

		createRenderTexture(
			_allocator,
			_device,
			_nearFieldIncrementalReductionHalveResCoCImagePongImage,
			VK_FORMAT_R16_SFLOAT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			incrementalReductionHalveImgExtents[NUM_INCREMENTAL_COC_REDUCTIONS - 1],
			numMips,
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_FILTER_NEAREST,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			_swapchainDependentDeletionQueue
		);

		// Create Descriptor Sets.
		{
			VkDescriptorImageInfo imageInfo = {
				.sampler = _depthImage.sampler,
				.imageView = _depthImage.imageView,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};
			VkDescriptorSet textureSet;
			vkutil::DescriptorBuilder::begin()
				.bindImage(0, &imageInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
				.build(textureSet, _dofSingleTextureLayout);
			attachTextureSetToMaterial(textureSet, "CoCMaterial");
		}
		{
			VkDescriptorImageInfo imageInfo = {
				.sampler = _mainImage.sampler,
				.imageView = _mainImage.imageView,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};
			VkDescriptorImageInfo imageInfo1 = {
				.sampler = _CoCImage.sampler,
				.imageView = _CoCImage.imageView,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};
			VkDescriptorSet textureSet;
			vkutil::DescriptorBuilder::begin()
				.bindImage(0, &imageInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
				.bindImage(1, &imageInfo1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
				.build(textureSet, _dofDoubleTextureLayout);
			attachTextureSetToMaterial(textureSet, "halveCoCMaterial");
		}
		for (size_t i = 0; i < NUM_INCREMENTAL_COC_REDUCTIONS; i++)
		{
			VkDescriptorImageInfo imageInfo = {
				.sampler = _CoCImageMaxSampler,
				.imageView = (i == 0) ? _CoCImage.imageView : _nearFieldIncrementalReductionHalveResCoCImages[i - 1].imageView,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};
			VkDescriptorSet textureSet;
			vkutil::DescriptorBuilder::begin()
				.bindImage(0, &imageInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
				.build(textureSet, _dofSingleTextureLayout);
			std::string materialName = "incrementalReductionHalveCoCMaterial_" + std::to_string(i);
			attachTextureSetToMaterial(textureSet, materialName);
		}
		{
			VkDescriptorImageInfo imageInfo = {
				.sampler = _nearFieldIncrementalReductionHalveResCoCImages[NUM_INCREMENTAL_COC_REDUCTIONS - 1].sampler,
				.imageView = _nearFieldIncrementalReductionHalveResCoCImages[NUM_INCREMENTAL_COC_REDUCTIONS - 1].imageView,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};
			VkDescriptorSet textureSet;
			vkutil::DescriptorBuilder::begin()
				.bindImage(0, &imageInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
				.build(textureSet, _dofSingleTextureLayout);
			attachTextureSetToMaterial(textureSet, "blurXSingleChannelMaterial");
		}
		{
			VkDescriptorImageInfo imageInfo = {
				.sampler = _nearFieldIncrementalReductionHalveResCoCImagePongImage.sampler,
				.imageView = _nearFieldIncrementalReductionHalveResCoCImagePongImage.imageView,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};
			VkDescriptorSet textureSet;
			vkutil::DescriptorBuilder::begin()
				.bindImage(0, &imageInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
				.build(textureSet, _dofSingleTextureLayout);
			attachTextureSetToMaterial(textureSet, "blurYSingleChannelMaterial");
		}
		{
			VkDescriptorImageInfo imageInfo = {
				.sampler = _nearFieldImage.sampler,
				.imageView = _nearFieldImage.imageView,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};
			VkDescriptorImageInfo imageInfo1 = {
				.sampler = _farFieldImage.sampler,
				.imageView = _farFieldImage.imageView,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};
			VkDescriptorImageInfo imageInfo2 = {
				.sampler = _nearFieldIncrementalReductionHalveResCoCImages[NUM_INCREMENTAL_COC_REDUCTIONS - 1].sampler,
				.imageView = _nearFieldIncrementalReductionHalveResCoCImages[NUM_INCREMENTAL_COC_REDUCTIONS - 1].imageView,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};
			VkDescriptorSet textureSet;
			vkutil::DescriptorBuilder::begin()
				.bindImage(0, &imageInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
				.bindImage(1, &imageInfo1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
				.bindImage(2, &imageInfo2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
				.build(textureSet, _dofTripleTextureLayout);
			attachTextureSetToMaterial(textureSet, "gatherDOFMaterial");
		}
		{
			VkDescriptorImageInfo imageInfo = {
				.sampler = _nearFieldImagePongImage.sampler,
				.imageView = _nearFieldImagePongImage.imageView,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};
			VkDescriptorImageInfo imageInfo1 = {
				.sampler = _farFieldImagePongImage.sampler,
				.imageView = _farFieldImagePongImage.imageView,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};
			VkDescriptorSet textureSet;
			vkutil::DescriptorBuilder::begin()
				.bindImage(0, &imageInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
				.bindImage(1, &imageInfo1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
				.build(textureSet, _dofDoubleTextureLayout);
			attachTextureSetToMaterial(textureSet, "DOFFloodFillMaterial");
		}
	}

	//
	// Postprocessing combine descriptor set.
	// @TODO: @RESEARCH: doing this, where the descriptors are getting initialized every time
	//                   the renderpass is getting created (which is necessary), does it cause
	//                   a memory leak?  -Timo 2023/05/29
	//
	VkDescriptorImageInfo mainHDRImageInfo = {
		.sampler = _mainImage.sampler,
		.imageView = _mainImage.imageView,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
	VkDescriptorImageInfo uiImageInfo = {
		.sampler = _uiImage.sampler,
		.imageView = _uiImage.imageView,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
	VkDescriptorImageInfo bloomImageInfo = {
		.sampler = _bloomPostprocessImage.sampler,
		.imageView = _bloomPostprocessImage.imageView,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
	VkDescriptorImageInfo depthBufferImageInfo = {
		.sampler = _depthImage.sampler,
		.imageView = _depthImage.imageView,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
	VkDescriptorImageInfo dofCoCImageInfo = {
		.sampler = _CoCImage.sampler,
		.imageView = _CoCImage.imageView,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
	VkDescriptorImageInfo dofNearImageInfo = {
		.sampler = _nearFieldImageLinearSampler,
		.imageView = _nearFieldImage.imageView,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
	VkDescriptorImageInfo dofFarImageInfo = {
		.sampler = _farFieldImageLinearSampler,
		.imageView = _farFieldImage.imageView,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
	VkDescriptorSet postprocessingTextureSet;
	vkutil::DescriptorBuilder::begin()
		.bindImage(0, &mainHDRImageInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
		.bindImage(1, &uiImageInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
		.bindImage(2, &bloomImageInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
		.bindImage(3, &depthBufferImageInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
		.bindImage(4, &dofCoCImageInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
		.bindImage(5, &dofNearImageInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
		.bindImage(6, &dofFarImageInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
		.build(postprocessingTextureSet, _postprocessSetLayout);
	attachTextureSetToMaterial(postprocessingTextureSet, "postprocessMaterial");
}

void VulkanEngine::initPickingRenderpass()    // @NOTE: @COPYPASTA: This is really copypasta of the above function (initMainRenderpass)
{
	//
	// Initialize the picking images
	//
	// Color image
	VkExtent3D pickingImgExtent = {
		.width = _windowExtent.width,
		.height = _windowExtent.height,
		.depth = 1,
	};
	VkImageCreateInfo pickingColorImgInfo = vkinit::imageCreateInfo(VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, pickingImgExtent, 1);
	VmaAllocationCreateInfo pickingImgAllocInfo = {
		.usage = VMA_MEMORY_USAGE_GPU_ONLY,
	};
	vmaCreateImage(_allocator, &pickingColorImgInfo, &pickingImgAllocInfo, &_pickingImage._image, &_pickingImage._allocation, nullptr);

	VkImageViewCreateInfo pickingColorViewInfo = vkinit::imageviewCreateInfo(VK_FORMAT_R32_SFLOAT, _pickingImage._image, VK_IMAGE_ASPECT_COLOR_BIT, 1);
	VK_CHECK(vkCreateImageView(_device, &pickingColorViewInfo, nullptr, &_pickingImageView));

	// Depth image
	VkImageCreateInfo pickingDepthImgInfo = vkinit::imageCreateInfo(_depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, pickingImgExtent, 1);
	VmaAllocationCreateInfo pickingDepthImgAllocInfo = {
		.usage = VMA_MEMORY_USAGE_GPU_ONLY,
		.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
	};
	vmaCreateImage(_allocator, &pickingDepthImgInfo, &pickingDepthImgAllocInfo, &_pickingDepthImage._image, &_pickingDepthImage._allocation, nullptr);

	VkImageViewCreateInfo pickingDepthViewInfo = vkinit::imageviewCreateInfo(_depthFormat, _pickingDepthImage._image, VK_IMAGE_ASPECT_DEPTH_BIT, 1);
	VK_CHECK(vkCreateImageView(_device, &pickingDepthViewInfo, nullptr, &_pickingDepthImageView));

	_swapchainDependentDeletionQueue.pushFunction([=]() {
		vkDestroyImageView(_device, _pickingImageView, nullptr);
		vkDestroyImageView(_device, _pickingDepthImageView, nullptr);
		vmaDestroyImage(_allocator, _pickingImage._image, _pickingImage._allocation);
		vmaDestroyImage(_allocator, _pickingDepthImage._image, _pickingDepthImage._allocation);
		});

	//
	// Color Attachment
	//
	VkAttachmentDescription colorAttachment = {
		.format = VK_FORMAT_R32_SFLOAT,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL    // @NOTE: After this renderpass, the image will be used as a texture to read from
	};
	VkAttachmentReference colorAttachmentRef = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	};

	//
	// Depth attachment
	//
	VkAttachmentDescription depthAttachment = {
		.flags = 0,
		.format = _depthFormat,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
	};
	VkAttachmentReference depthAttachmentRef = {
		.attachment = 1,
		.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
	};

	//
	// Define the subpass to render to the picking renderpass
	//
	VkSubpassDescription subpass = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = 1,
		.pColorAttachments = &colorAttachmentRef,
		.pDepthStencilAttachment = &depthAttachmentRef
	};

	//
	// GPU work ordering dependencies
	//
	VkSubpassDependency colorDependency = {
		.srcSubpass = VK_SUBPASS_EXTERNAL,
		.dstSubpass = 0,
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
	};
	VkSubpassDependency depthDependency = {
		.srcSubpass = VK_SUBPASS_EXTERNAL,
		.dstSubpass = 0,
		.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
	};

	//
	// Create the renderpass for the subpass
	//
	VkSubpassDependency dependencies[] = { colorDependency, depthDependency };
	VkAttachmentDescription attachments[] = { colorAttachment, depthAttachment };
	VkRenderPassCreateInfo renderPassInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = 2,
		.pAttachments = &attachments[0],
		.subpassCount = 1,
		.pSubpasses = &subpass,
		.dependencyCount = 2,
		.pDependencies = &dependencies[0]
	};

	VK_CHECK(vkCreateRenderPass(_device, &renderPassInfo, nullptr, &_pickingRenderPass));

	// Add destroy command for cleanup
	_swapchainDependentDeletionQueue.pushFunction([=]() {
		vkDestroyRenderPass(_device, _pickingRenderPass, nullptr);
		});
}

void VulkanEngine::initFramebuffers()
{
	_swapchainFramebuffers = std::vector<VkFramebuffer>(_swapchainImages.size());
	for (size_t i = 0; i < _swapchainImages.size(); i++)
	{
		createFramebuffer(
			_device,
			_swapchainFramebuffers[i],
			_postprocessRenderPass,
			{ _swapchainImageViews[i], },
			_windowExtent,
			1,
			_swapchainDependentDeletionQueue
		);
		_swapchainDependentDeletionQueue.pushFunction([=]() {
			vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
			});
	}

	createFramebuffer(
		_device,
		_mainFramebuffer,
		_mainRenderPass,
		{ _mainImage.imageView, _depthImage.imageView, },
		_windowExtent,
		1,
		_swapchainDependentDeletionQueue
	);

	createFramebuffer(
		_device,
		_uiFramebuffer,
		_uiRenderPass,
		{ _uiImage.imageView, },
		_windowExtent,
		1,
		_swapchainDependentDeletionQueue
	);

	createFramebuffer(
		_device,
		_pickingFramebuffer,
		_pickingRenderPass,
		{ _pickingImageView, _pickingDepthImageView, },
		_windowExtent,
		1,
		_swapchainDependentDeletionQueue
	);

	createFramebuffer(
		_device,
		_CoCFramebuffer,
		_CoCRenderPass,
		{ _CoCImage.imageView, },
		_windowExtent,
		1,
		_swapchainDependentDeletionQueue
	);

	createFramebuffer(
		_device,
		_halveCoCFramebuffer,
		_halveCoCRenderPass,
		{ _nearFieldImage.imageView, _farFieldImage.imageView, },
		_halfResImageExtent,
		1,
		_swapchainDependentDeletionQueue
	);

	for (size_t i = 0; i < NUM_INCREMENTAL_COC_REDUCTIONS; i++)
		createFramebuffer(
			_device,
			_incrementalReductionHalveCoCFramebuffers[i],
			_incrementalReductionHalveCoCRenderPass,
			{ _nearFieldIncrementalReductionHalveResCoCImages[i].imageView, },
			_incrementalReductionHalveResImageExtents[i],
			1,
			_swapchainDependentDeletionQueue
		);

	createFramebuffer(
		_device,
		_blurXNearsideCoCFramebuffer,
		_blurXNearsideCoCRenderPass,
		{ _nearFieldIncrementalReductionHalveResCoCImagePongImage.imageView, },
		_incrementalReductionHalveResImageExtents[NUM_INCREMENTAL_COC_REDUCTIONS - 1],
		1,
		_swapchainDependentDeletionQueue
	);

	createFramebuffer(
		_device,
		_blurYNearsideCoCFramebuffer,
		_blurYNearsideCoCRenderPass,
		{ _nearFieldIncrementalReductionHalveResCoCImages[NUM_INCREMENTAL_COC_REDUCTIONS - 1].imageView, },
		_incrementalReductionHalveResImageExtents[NUM_INCREMENTAL_COC_REDUCTIONS - 1],
		1,
		_swapchainDependentDeletionQueue
	);

	createFramebuffer(
		_device,
		_gatherDOFFramebuffer,
		_gatherDOFRenderPass,
		{ _nearFieldImagePongImage.imageView, _farFieldImagePongImage.imageView, },
		_halfResImageExtent,
		1,
		_swapchainDependentDeletionQueue
	);

	createFramebuffer(
		_device,
		_dofFloodFillFramebuffer,
		_dofFloodFillRenderPass,
		{ _nearFieldImage.imageView, _farFieldImage.imageView, },
		_halfResImageExtent,
		1,
		_swapchainDependentDeletionQueue
	);
}

void VulkanEngine::initSyncStructures()
{
	VkFenceCreateInfo fenceCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.pNext = nullptr,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT,
	};
	VkSemaphoreCreateInfo semaphoreCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
	};

	for (size_t i = 0; i < FRAME_OVERLAP; i++)
	{
		VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_frames[i].renderFence));
		VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_frames[i].pickingRenderFence));

		// Add destroy command for cleanup
		_mainDeletionQueue.pushFunction([=]() {
			vkDestroyFence(_device, _frames[i].renderFence, nullptr);
			vkDestroyFence(_device, _frames[i].pickingRenderFence, nullptr);
			});

		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i].presentSemaphore));
		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i].renderSemaphore));

		// Add destroy command for cleanup
		_mainDeletionQueue.pushFunction([=]() {
			vkDestroySemaphore(_device, _frames[i].presentSemaphore, nullptr);
			vkDestroySemaphore(_device, _frames[i].renderSemaphore, nullptr);
			});
	}

	//
	// Upload context fence
	//
	VkFenceCreateInfo uploadFenceCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,		// @NOTE: we will not try to wait on this fence
	};
	VK_CHECK(vkCreateFence(_device, &uploadFenceCreateInfo, nullptr, &_uploadContext.uploadFence));

	// Add destroy command for cleanup
	_mainDeletionQueue.pushFunction([=]() {
		vkDestroyFence(_device, _uploadContext.uploadFence, nullptr);
		});
}

void VulkanEngine::initDescriptors()    // @NOTE: don't destroy and then recreate descriptors when recreating the swapchain. Only pipelines (not even pipelinelayouts), framebuffers, and the corresponding image/imageviews/samplers need to get recreated.  -Timo
{
	//
	// Materials for ImGui
	//
	_imguiData.textureLayerVisible   = ImGui_ImplVulkan_AddTexture(_loadedTextures["imguiTextureLayerVisible"].sampler, _loadedTextures["imguiTextureLayerVisible"].imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	_imguiData.textureLayerInvisible = ImGui_ImplVulkan_AddTexture(_loadedTextures["imguiTextureLayerInvisible"].sampler, _loadedTextures["imguiTextureLayerInvisible"].imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	_imguiData.textureLayerBuilder   = ImGui_ImplVulkan_AddTexture(_loadedTextures["imguiTextureLayerBuilder"].sampler, _loadedTextures["imguiTextureLayerBuilder"].imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	_imguiData.textureLayerCollision = ImGui_ImplVulkan_AddTexture(_loadedTextures["imguiTextureLayerCollision"].sampler, _loadedTextures["imguiTextureLayerCollision"].imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	//
	// Create Descriptor Sets
	//
	for (size_t i = 0; i < FRAME_OVERLAP; i++)
	{
		//
		// Global
		//
		_frames[i].cameraBuffer = createBuffer(sizeof(GPUCameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
		_frames[i].pbrShadingPropsBuffer = createBuffer(sizeof(GPUPBRShadingProps), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

		VkDescriptorBufferInfo cameraInfo = {
			.buffer = _frames[i].cameraBuffer._buffer,
			.offset = 0,
			.range = sizeof(GPUCameraData),
		};
		VkDescriptorBufferInfo pbrShadingPropsInfo = {
			.buffer = _frames[i].pbrShadingPropsBuffer._buffer,
			.offset = 0,
			.range = sizeof(GPUPBRShadingProps),
		};
		VkDescriptorImageInfo irradianceImageInfo = {
			.sampler = _pbrSceneTextureSet.irradianceCubemap.sampler,
			.imageView = _pbrSceneTextureSet.irradianceCubemap.imageView,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};
		VkDescriptorImageInfo prefilteredImageInfo = {
			.sampler = _pbrSceneTextureSet.prefilteredCubemap.sampler,
			.imageView = _pbrSceneTextureSet.prefilteredCubemap.imageView,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};
		VkDescriptorImageInfo brdfLUTImageInfo = {
			.sampler = _pbrSceneTextureSet.brdfLUTTexture.sampler,
			.imageView = _pbrSceneTextureSet.brdfLUTTexture.imageView,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};
		VkDescriptorImageInfo shadowMapImageInfo = {
			.sampler = _pbrSceneTextureSet.shadowMap.sampler,
			.imageView = _pbrSceneTextureSet.shadowMap.imageView,
			.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
		};
		VkDescriptorImageInfo shadowJitterMapImageInfo = {
			.sampler = _pbrSceneTextureSet.shadowJitterMap.sampler,
			.imageView = _pbrSceneTextureSet.shadowJitterMap.imageView,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};

		vkutil::DescriptorBuilder::begin()
			.bindBuffer(0, &cameraInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
			.bindBuffer(1, &pbrShadingPropsInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
			.bindImage(2, &irradianceImageInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
			.bindImage(3, &prefilteredImageInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
			.bindImage(4, &brdfLUTImageInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
			.bindImage(5, &shadowMapImageInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
			.bindImage(6, &shadowJitterMapImageInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
			.build(_frames[i].globalDescriptor, _globalSetLayout);

		//
		// Cascade Shadow View Projections
		//
		_frames[i].cascadeViewProjsBuffer = createBuffer(sizeof(GPUCascadeViewProjsData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
		VkDescriptorBufferInfo cascadeViewProjsBufferInfo = {
			.buffer = _frames[i].cascadeViewProjsBuffer._buffer,
			.offset = 0,
			.range = sizeof(GPUCascadeViewProjsData),
		};
		vkutil::DescriptorBuilder::begin()
			.bindBuffer(0, &cascadeViewProjsBufferInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
			.build(_frames[i].cascadeViewProjsDescriptor, _cascadeViewProjsSetLayout);

		//
		// Object Information
		//
		_frames[i].objectBuffer = createBuffer(sizeof(GPUObjectData) * RENDER_OBJECTS_MAX_CAPACITY, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
		VkDescriptorBufferInfo objectBufferInfo = {
			.buffer = _frames[i].objectBuffer._buffer,
			.offset = 0,
			.range = sizeof(GPUObjectData) * RENDER_OBJECTS_MAX_CAPACITY,
		};
		vkutil::DescriptorBuilder::begin()
			.bindBuffer(0, &objectBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
			.build(_frames[i].objectDescriptor, _objectSetLayout);

		//
		// Instance Pointers
		//
		_frames[i].instancePtrBuffer = createBuffer(sizeof(GPUInstancePointer) * INSTANCE_PTR_MAX_CAPACITY, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
		VkDescriptorBufferInfo instancePtrBufferInfo = {
			.buffer = _frames[i].instancePtrBuffer._buffer,
			.offset = 0,
			.range = sizeof(GPUInstancePointer) * INSTANCE_PTR_MAX_CAPACITY,
		};
		vkutil::DescriptorBuilder::begin()
			.bindBuffer(0, &instancePtrBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
			.build(_frames[i].instancePtrDescriptor, _instancePtrSetLayout);

		//
		// Picking ID Capture
		//
		_frames[i].pickingSelectedIdBuffer = createBuffer(sizeof(GPUPickingSelectedIdData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU);    // @NOTE: primary focus is to read from gpu, so gpu_to_cpu
		VkDescriptorBufferInfo pickingSelectedIdBufferInfo = {
			.buffer = _frames[i].pickingSelectedIdBuffer._buffer,
			.offset = 0,
			.range = sizeof(GPUPickingSelectedIdData),
		};
		vkutil::DescriptorBuilder::begin()
			.bindBuffer(0, &pickingSelectedIdBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
			.build(_frames[i].pickingReturnValueDescriptor, _pickingReturnValueSetLayout);

		//
		// Add destroy command for cleanup
		//
		_mainDeletionQueue.pushFunction([=]() {
			vmaDestroyBuffer(_allocator, _frames[i].cameraBuffer._buffer, _frames[i].cameraBuffer._allocation);
			vmaDestroyBuffer(_allocator, _frames[i].pbrShadingPropsBuffer._buffer, _frames[i].pbrShadingPropsBuffer._allocation);
			vmaDestroyBuffer(_allocator, _frames[i].cascadeViewProjsBuffer._buffer, _frames[i].cascadeViewProjsBuffer._allocation);
			vmaDestroyBuffer(_allocator, _frames[i].objectBuffer._buffer, _frames[i].objectBuffer._allocation);
			vmaDestroyBuffer(_allocator, _frames[i].instancePtrBuffer._buffer, _frames[i].instancePtrBuffer._allocation);
			vmaDestroyBuffer(_allocator, _frames[i].pickingSelectedIdBuffer._buffer, _frames[i].pickingSelectedIdBuffer._allocation);
		});
	}

	//
	// Single texture (i.e. skybox)
	//
	VkDescriptorImageInfo singleTextureImageInfo = {
		.sampler = _loadedTextures["CubemapSkybox"].sampler,
		.imageView = _loadedTextures["CubemapSkybox"].imageView,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
	VkDescriptorSet singleTextureSet;
	vkutil::DescriptorBuilder::begin()
		.bindImage(0, &singleTextureImageInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
		.build(singleTextureSet, _singleTextureSetLayout);
	attachTextureSetToMaterial(singleTextureSet, "skyboxMaterial");

	//
	// Voxel Field Lightgrids Descriptor Set
	//
	initVoxelLightingDescriptor();

	//
	// Joint Descriptor
	//
	vkglTF::Animator::initializeEmpty(this);

	//
	// Text Mesh Fonts
	//
	textmesh::loadFontSDF("res/texture_pool/font_sdf_rgba.png", "res/font.fnt", "defaultFont");

	physengine::initDebugVisDescriptors(this);

	// Descriptor set for compute culling.
	for (size_t i = 0; i < FRAME_OVERLAP; i++)
	{
		auto& currentFrame = _frames[i];

		VkDescriptorBufferInfo drawCommandsRawBufferInfo = {
			.buffer = currentFrame.indirectDrawCommandRawBuffer._buffer,
			.offset = 0,
			.range = sizeof(VkDrawIndexedIndirectCommand) * INSTANCE_PTR_MAX_CAPACITY,
		};
		VkDescriptorBufferInfo drawCommandOffsetsBufferInfo = {
			.buffer = currentFrame.indirectDrawCommandOffsetsBuffer._buffer,
			.offset = 0,
			.range = sizeof(GPUIndirectDrawCommandOffsetsData) * INSTANCE_PTR_MAX_CAPACITY,
		};
		{
			// Shadow pass.
			VkDescriptorBufferInfo drawCommandsOutputBufferInfo = {
				.buffer = currentFrame.indirectShadowPass.indirectDrawCommandsBuffer._buffer,
				.offset = 0,
				.range = sizeof(VkDrawIndexedIndirectCommand) * INSTANCE_PTR_MAX_CAPACITY,
			};
			VkDescriptorBufferInfo drawCommandCountsBufferInfo = {
				.buffer = currentFrame.indirectShadowPass.indirectDrawCommandCountsBuffer._buffer,
				.offset = 0,
				.range = sizeof(uint32_t) * INSTANCE_PTR_MAX_CAPACITY,
			};

			vkutil::DescriptorBuilder::begin()
				.bindBuffer(0, &drawCommandsRawBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
				.bindBuffer(1, &drawCommandsOutputBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
				.bindBuffer(2, &drawCommandOffsetsBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
				.bindBuffer(3, &drawCommandCountsBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
				.build(currentFrame.indirectShadowPass.indirectDrawCommandDescriptor, _computeCullingIndirectDrawCommandSetLayout);
		}
		{
			// Main pass.
			VkDescriptorBufferInfo drawCommandsOutputBufferInfo = {
				.buffer = currentFrame.indirectMainPass.indirectDrawCommandsBuffer._buffer,
				.offset = 0,
				.range = sizeof(VkDrawIndexedIndirectCommand) * INSTANCE_PTR_MAX_CAPACITY,
			};
			VkDescriptorBufferInfo drawCommandCountsBufferInfo = {
				.buffer = currentFrame.indirectMainPass.indirectDrawCommandCountsBuffer._buffer,
				.offset = 0,
				.range = sizeof(uint32_t) * INSTANCE_PTR_MAX_CAPACITY,
			};

			vkutil::DescriptorBuilder::begin()
				.bindBuffer(0, &drawCommandsRawBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
				.bindBuffer(1, &drawCommandsOutputBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
				.bindBuffer(2, &drawCommandOffsetsBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
				.bindBuffer(3, &drawCommandCountsBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
				.build(currentFrame.indirectMainPass.indirectDrawCommandDescriptor, _computeCullingIndirectDrawCommandSetLayout);
		}
	}

	// Descriptor set layout for compute skinning.
	// (Can't create descriptors bc 0 byte buffers can't get created)
	_computeSkinningInoutVerticesSetLayout =
		vkutil::descriptorlayoutcache::createDescriptorLayout({
			vkutil::descriptorlayoutcache::layoutBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT),
			vkutil::descriptorlayoutcache::layoutBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT),
		});
}

void VulkanEngine::initPipelines()
{
	// Common values
	vkglTF::VertexInputDescription modelVertexDescription = vkglTF::Model::Vertex::getVertexDescription();
	VkViewport screenspaceViewport = {
		0.0f, 0.0f,
		(float_t)_windowExtent.width, (float_t)_windowExtent.height,
		0.0f, 1.0f,
	};
	VkRect2D screenspaceScissor = {
		{ 0, 0 },
		_windowExtent,
	};

	VkViewport halfScreenspaceViewport = {
		0.0f, 0.0f,
		(float_t)_halfResImageExtent.width, (float_t)_halfResImageExtent.height,
		0.0f, 1.0f,
	};
	VkRect2D halfScreenspaceScissor = {
		{ 0, 0 },
		_halfResImageExtent,
	};

	VkViewport incrementalReductionHalveScreenspaceViewports[NUM_INCREMENTAL_COC_REDUCTIONS];
	VkRect2D incrementalReductionHalveScreenspaceScissors[NUM_INCREMENTAL_COC_REDUCTIONS];
	for (size_t i = 0; i < NUM_INCREMENTAL_COC_REDUCTIONS; i++)
	{
		incrementalReductionHalveScreenspaceViewports[i] = {
			0.0f, 0.0f,
			(float_t)_incrementalReductionHalveResImageExtents[i].width, (float_t)_incrementalReductionHalveResImageExtents[i].height,
			0.0f, 1.0f,
		};
		incrementalReductionHalveScreenspaceScissors[i] = {
			{ 0, 0 },
			_incrementalReductionHalveResImageExtents[i],
		};
	}

	// Snapshot image pipeline
	VkPipeline snapshotImagePipeline;
	VkPipelineLayout snapshotImagePipelineLayout;
	vkutil::pipelinebuilder::build(
		{},
		{ _singleTextureSetLayout },
		{
			{ VK_SHADER_STAGE_VERTEX_BIT, "res/shaders/genbrdflut.vert.spv" },
			{ VK_SHADER_STAGE_FRAGMENT_BIT, "res/shaders/snapshotImage.frag.spv" },
		},
		{},  // No triangles are actually streamed in
		{},
		vkinit::inputAssemblyCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
		screenspaceViewport,
		screenspaceScissor,
		vkinit::rasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE),
		{ vkinit::colorBlendAttachmentState() },
		vkinit::multisamplingStateCreateInfo(),
		vkinit::depthStencilCreateInfo(false, false, VK_COMPARE_OP_ALWAYS),
		{},
		_mainRenderPass,
		1,
		snapshotImagePipeline,
		snapshotImagePipelineLayout,
		_swapchainDependentDeletionQueue
	);
	attachPipelineToMaterial(snapshotImagePipeline, snapshotImagePipelineLayout, "snapshotImageMaterial");

	// Skybox pipeline
	VkPipeline skyboxPipeline;
	VkPipelineLayout skyboxPipelineLayout;
	vkutil::pipelinebuilder::build(
		{},
		{ _globalSetLayout, _singleTextureSetLayout },
		{
			{ VK_SHADER_STAGE_VERTEX_BIT, "res/shaders/skybox.vert.spv" },
			{ VK_SHADER_STAGE_FRAGMENT_BIT, "res/shaders/skybox.frag.spv" },
		},
		modelVertexDescription.attributes,
		modelVertexDescription.bindings,
		vkinit::inputAssemblyCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
		screenspaceViewport,
		screenspaceScissor,
		vkinit::rasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_FRONT_BIT),    // Bc we're rendering a box inside-out
		{ vkinit::colorBlendAttachmentState() },
		vkinit::multisamplingStateCreateInfo(),
		vkinit::depthStencilCreateInfo(true, false, VK_COMPARE_OP_LESS_OR_EQUAL),  // @FIX: it's not a perfect depth test bc there is some overdraw with the skybox.
		{},
		_mainRenderPass,
		1,
		skyboxPipeline,
		skyboxPipelineLayout,
		_swapchainDependentDeletionQueue
	);
	attachPipelineToMaterial(skyboxPipeline, skyboxPipelineLayout, "skyboxMaterial");

	// Picking pipeline
	VkPipeline pickingPipeline;
	VkPipelineLayout pickingPipelineLayout;
	vkutil::pipelinebuilder::build(
		{},
		{ _globalSetLayout, _objectSetLayout, _instancePtrSetLayout, _pickingReturnValueSetLayout },
		{
			{ VK_SHADER_STAGE_VERTEX_BIT, "res/shaders/picking.vert.spv" },
			{ VK_SHADER_STAGE_FRAGMENT_BIT, "res/shaders/picking.frag.spv" },
		},
		modelVertexDescription.attributes,
		modelVertexDescription.bindings,
		vkinit::inputAssemblyCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
		screenspaceViewport,
		screenspaceScissor,
		vkinit::rasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT),
		{ vkinit::colorBlendAttachmentState() },
		vkinit::multisamplingStateCreateInfo(),
		vkinit::depthStencilCreateInfo(true, true, VK_COMPARE_OP_LESS_OR_EQUAL),
		{ VK_DYNAMIC_STATE_SCISSOR },
		_pickingRenderPass,
		0,
		pickingPipeline,
		pickingPipelineLayout,
		_swapchainDependentDeletionQueue
	);
	attachPipelineToMaterial(pickingPipeline, pickingPipelineLayout, "pickingMaterial");

	// Wireframe color pipeline
	VkPipeline wireframePipeline;
	VkPipelineLayout wireframePipelineLayout;
	vkutil::pipelinebuilder::build(
		{
			VkPushConstantRange{
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
				.offset = 0,
				.size = sizeof(ColorPushConstBlock)
			}
		},
		{ _globalSetLayout, _objectSetLayout, _instancePtrSetLayout },
		{
			{ VK_SHADER_STAGE_VERTEX_BIT, "res/shaders/wireframe_color.vert.spv" },
			{ VK_SHADER_STAGE_FRAGMENT_BIT, "res/shaders/color.frag.spv" },
		},
		modelVertexDescription.attributes,
		modelVertexDescription.bindings,
		vkinit::inputAssemblyCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
		screenspaceViewport,
		screenspaceScissor,
		vkinit::rasterizationStateCreateInfo(VK_POLYGON_MODE_LINE, VK_CULL_MODE_BACK_BIT),
		{ vkinit::colorBlendAttachmentState() },
		vkinit::multisamplingStateCreateInfo(),
		vkinit::depthStencilCreateInfo(true, true, VK_COMPARE_OP_LESS_OR_EQUAL),
		{},
		_mainRenderPass,
		1,
		wireframePipeline,
		wireframePipelineLayout,
		_swapchainDependentDeletionQueue
	);
	attachPipelineToMaterial(wireframePipeline, wireframePipelineLayout, "wireframeColorMaterial");

	VkPipeline wireframeBehindPipeline;
	vkutil::pipelinebuilder::build(
		{
			VkPushConstantRange{
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
				.offset = 0,
				.size = sizeof(ColorPushConstBlock)
			}
		},
		{ _globalSetLayout, _objectSetLayout, _instancePtrSetLayout },
		{
			{ VK_SHADER_STAGE_VERTEX_BIT, "res/shaders/wireframe_color.vert.spv" },
			{ VK_SHADER_STAGE_FRAGMENT_BIT, "res/shaders/color.frag.spv" },
		},
		modelVertexDescription.attributes,
		modelVertexDescription.bindings,
		vkinit::inputAssemblyCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
		screenspaceViewport,
		screenspaceScissor,
		vkinit::rasterizationStateCreateInfo(VK_POLYGON_MODE_LINE, VK_CULL_MODE_BACK_BIT),
		{ vkinit::colorBlendAttachmentState() },
		vkinit::multisamplingStateCreateInfo(),
		vkinit::depthStencilCreateInfo(true, false, VK_COMPARE_OP_GREATER),
		{},
		_mainRenderPass,
		1,
		wireframeBehindPipeline,
		wireframePipelineLayout,
		_swapchainDependentDeletionQueue
	);
	attachPipelineToMaterial(wireframeBehindPipeline, wireframePipelineLayout, "wireframeColorBehindMaterial");

	// Postprocess pipeline
	VkPipeline postprocessPipeline;
	VkPipelineLayout postprocessPipelineLayout;
	vkutil::pipelinebuilder::build(
		{
			VkPushConstantRange{
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
				.offset = 0,
				.size = sizeof(GPUPostProcessParams)
			}
		},
		{ _globalSetLayout, _postprocessSetLayout },
		{
			{ VK_SHADER_STAGE_VERTEX_BIT, "res/shaders/genbrdflut.vert.spv" },
			{ VK_SHADER_STAGE_FRAGMENT_BIT, "res/shaders/postprocess.frag.spv" },
		},
		{},  // No triangles are actually streamed in
		{},
		vkinit::inputAssemblyCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
		screenspaceViewport,
		screenspaceScissor,
		vkinit::rasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE),
		{ vkinit::colorBlendAttachmentState() },
		vkinit::multisamplingStateCreateInfo(),
		vkinit::depthStencilCreateInfo(false, false, VK_COMPARE_OP_ALWAYS),
		{},
		_postprocessRenderPass,
		0,
		postprocessPipeline,
		postprocessPipelineLayout,
		_swapchainDependentDeletionQueue
	);
	attachPipelineToMaterial(postprocessPipeline, postprocessPipelineLayout, "postprocessMaterial");

	// Generate CoC pipeline
	VkPipelineColorBlendAttachmentState rChannelAttachmentState = vkinit::colorBlendAttachmentState();
	rChannelAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;

	VkPipelineColorBlendAttachmentState rgChannelAttachmentState = vkinit::colorBlendAttachmentState();
	rgChannelAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT;

	VkPipeline CoCPipeline;
	VkPipelineLayout CoCPipelineLayout;
	vkutil::pipelinebuilder::build(
		{
			VkPushConstantRange{
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
				.offset = 0,
				.size = sizeof(GPUCoCParams)
			}
		},
		{ _dofSingleTextureLayout },
		{
			{ VK_SHADER_STAGE_VERTEX_BIT, "res/shaders/genbrdflut.vert.spv" },
			{ VK_SHADER_STAGE_FRAGMENT_BIT, "res/shaders/generate_coc.frag.spv" },
		},
		{},  // No triangles are actually streamed in
		{},
		vkinit::inputAssemblyCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
		screenspaceViewport,
		screenspaceScissor,
		vkinit::rasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE),
		{ rgChannelAttachmentState },
		vkinit::multisamplingStateCreateInfo(),
		vkinit::depthStencilCreateInfo(false, false, VK_COMPARE_OP_ALWAYS),
		{},
		_CoCRenderPass,
		0,
		CoCPipeline,
		CoCPipelineLayout,
		_swapchainDependentDeletionQueue
	);
	attachPipelineToMaterial(CoCPipeline, CoCPipelineLayout, "CoCMaterial");

	// Halve CoC pipeline
	VkPipeline halveCoCPipeline;
	VkPipelineLayout halveCoCPipelineLayout;
	vkutil::pipelinebuilder::build(
		{
			VkPushConstantRange{
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
				.offset = 0,
				.size = sizeof(GPUCoCParams)
			}
		},
		{ _dofDoubleTextureLayout },
		{
			{ VK_SHADER_STAGE_VERTEX_BIT, "res/shaders/genbrdflut.vert.spv" },
			{ VK_SHADER_STAGE_FRAGMENT_BIT, "res/shaders/halve_coc.frag.spv" },
		},
		{},  // No triangles are actually streamed in
		{},
		vkinit::inputAssemblyCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
		halfScreenspaceViewport,
		halfScreenspaceScissor,
		vkinit::rasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE),
		{ vkinit::colorBlendAttachmentState(), vkinit::colorBlendAttachmentState() },
		vkinit::multisamplingStateCreateInfo(),
		vkinit::depthStencilCreateInfo(false, false, VK_COMPARE_OP_ALWAYS),
		{},
		_halveCoCRenderPass,
		0,
		halveCoCPipeline,
		halveCoCPipelineLayout,
		_swapchainDependentDeletionQueue
	);
	attachPipelineToMaterial(halveCoCPipeline, halveCoCPipelineLayout, "halveCoCMaterial");

	// IncrementalReductionHalve CoC pipeline
	for (size_t i = 0; i < NUM_INCREMENTAL_COC_REDUCTIONS; i++)
	{
		VkPipeline incrementalReductionHalveCoCPipeline;
		VkPipelineLayout incrementalReductionHalveCoCPipelineLayout;
		vkutil::pipelinebuilder::build(
			{},
			{ _dofSingleTextureLayout },
			{
				{ VK_SHADER_STAGE_VERTEX_BIT, "res/shaders/genbrdflut.vert.spv" },
				{ VK_SHADER_STAGE_FRAGMENT_BIT, "res/shaders/incrementalReductionHalve_coc.frag.spv" },
			},
			{},  // No triangles are actually streamed in
			{},
			vkinit::inputAssemblyCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
			incrementalReductionHalveScreenspaceViewports[i],
			incrementalReductionHalveScreenspaceScissors[i],
			vkinit::rasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE),
			{ rChannelAttachmentState },
			vkinit::multisamplingStateCreateInfo(),
			vkinit::depthStencilCreateInfo(false, false, VK_COMPARE_OP_ALWAYS),
			{},
			_incrementalReductionHalveCoCRenderPass,
			0,
			incrementalReductionHalveCoCPipeline,
			incrementalReductionHalveCoCPipelineLayout,
			_swapchainDependentDeletionQueue
		);
		std::string materialName = "incrementalReductionHalveCoCMaterial_" + std::to_string(i);
		attachPipelineToMaterial(incrementalReductionHalveCoCPipeline, incrementalReductionHalveCoCPipelineLayout, materialName);
	}

	// Blur X Single Channel pipeline
	VkPipeline blurXSingleChannelPipeline;
	VkPipelineLayout blurXSingleChannelPipelineLayout;
	vkutil::pipelinebuilder::build(
		{
			VkPushConstantRange{
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
				.offset = 0,
				.size = sizeof(GPUBlurParams)
			}
		},
		{ _dofSingleTextureLayout },
		{
			{ VK_SHADER_STAGE_VERTEX_BIT, "res/shaders/genbrdflut.vert.spv" },
			{ VK_SHADER_STAGE_FRAGMENT_BIT, "res/shaders/blur_x_singlechannel.frag.spv" },
		},
		{},  // No triangles are actually streamed in
		{},
		vkinit::inputAssemblyCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
		incrementalReductionHalveScreenspaceViewports[NUM_INCREMENTAL_COC_REDUCTIONS - 1],
		incrementalReductionHalveScreenspaceScissors[NUM_INCREMENTAL_COC_REDUCTIONS - 1],
		vkinit::rasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE),
		{ rChannelAttachmentState },
		vkinit::multisamplingStateCreateInfo(),
		vkinit::depthStencilCreateInfo(false, false, VK_COMPARE_OP_ALWAYS),
		{},
		_blurXNearsideCoCRenderPass,
		0,
		blurXSingleChannelPipeline,
		blurXSingleChannelPipelineLayout,
		_swapchainDependentDeletionQueue
	);
	attachPipelineToMaterial(blurXSingleChannelPipeline, blurXSingleChannelPipelineLayout, "blurXSingleChannelMaterial");

	// Blur Y Single Channel pipeline
	VkPipeline blurYSingleChannelPipeline;
	VkPipelineLayout blurYSingleChannelPipelineLayout;
	vkutil::pipelinebuilder::build(
		{
			VkPushConstantRange{
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
				.offset = 0,
				.size = sizeof(GPUBlurParams)
			}
		},
		{ _dofSingleTextureLayout },
		{
			{ VK_SHADER_STAGE_VERTEX_BIT, "res/shaders/genbrdflut.vert.spv" },
			{ VK_SHADER_STAGE_FRAGMENT_BIT, "res/shaders/blur_y_singlechannel.frag.spv" },
		},
		{},  // No triangles are actually streamed in
		{},
		vkinit::inputAssemblyCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
		incrementalReductionHalveScreenspaceViewports[NUM_INCREMENTAL_COC_REDUCTIONS - 1],
		incrementalReductionHalveScreenspaceScissors[NUM_INCREMENTAL_COC_REDUCTIONS - 1],
		vkinit::rasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE),
		{ rChannelAttachmentState },
		vkinit::multisamplingStateCreateInfo(),
		vkinit::depthStencilCreateInfo(false, false, VK_COMPARE_OP_ALWAYS),
		{},
		_blurYNearsideCoCRenderPass,
		0,
		blurYSingleChannelPipeline,
		blurYSingleChannelPipelineLayout,
		_swapchainDependentDeletionQueue
	);
	attachPipelineToMaterial(blurYSingleChannelPipeline, blurYSingleChannelPipelineLayout, "blurYSingleChannelMaterial");

	// Gather Depth of Field pipeline
	VkPipeline gatherDOFPipeline;
	VkPipelineLayout gatherDOFPipelineLayout;
	vkutil::pipelinebuilder::build(
		{
			VkPushConstantRange{
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
				.offset = 0,
				.size = sizeof(GPUGatherDOFParams)
			}
		},
		{ _dofTripleTextureLayout },
		{
			{ VK_SHADER_STAGE_VERTEX_BIT, "res/shaders/genbrdflut.vert.spv" },
			{ VK_SHADER_STAGE_FRAGMENT_BIT, "res/shaders/gather_dof.frag.spv" },
		},
		{},  // No triangles are actually streamed in
		{},
		vkinit::inputAssemblyCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
		halfScreenspaceViewport,
		halfScreenspaceScissor,
		vkinit::rasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE),
		{ vkinit::colorBlendAttachmentState(), vkinit::colorBlendAttachmentState() },
		vkinit::multisamplingStateCreateInfo(),
		vkinit::depthStencilCreateInfo(false, false, VK_COMPARE_OP_ALWAYS),
		{},
		_gatherDOFRenderPass,
		0,
		gatherDOFPipeline,
		gatherDOFPipelineLayout,
		_swapchainDependentDeletionQueue
	);
	attachPipelineToMaterial(gatherDOFPipeline, gatherDOFPipelineLayout, "gatherDOFMaterial");

	// Depth of Field Flood-fill pipeline
	VkPipeline dofFloodFillPipeline;
	VkPipelineLayout dofFloodFillPipelineLayout;
	vkutil::pipelinebuilder::build(
		{
			VkPushConstantRange{
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
				.offset = 0,
				.size = sizeof(GPUBlurParams)
			}
		},
		{ _dofDoubleTextureLayout },
		{
			{ VK_SHADER_STAGE_VERTEX_BIT, "res/shaders/genbrdflut.vert.spv" },
			{ VK_SHADER_STAGE_FRAGMENT_BIT, "res/shaders/dof_floodfill.frag.spv" },
		},
		{},  // No triangles are actually streamed in
		{},
		vkinit::inputAssemblyCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
		halfScreenspaceViewport,
		halfScreenspaceScissor,
		vkinit::rasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE),
		{ vkinit::colorBlendAttachmentState(), vkinit::colorBlendAttachmentState() },
		vkinit::multisamplingStateCreateInfo(),
		vkinit::depthStencilCreateInfo(false, false, VK_COMPARE_OP_ALWAYS),
		{},
		_dofFloodFillRenderPass,
		0,
		dofFloodFillPipeline,
		dofFloodFillPipelineLayout,
		_swapchainDependentDeletionQueue
	);
	attachPipelineToMaterial(dofFloodFillPipeline, dofFloodFillPipelineLayout, "DOFFloodFillMaterial");

	// Compute culling pipeline.
	VkPipeline computeCullingPipeline;
	VkPipelineLayout computeCullingPipelineLayout;
	vkutil::pipelinebuilder::buildCompute(
		{
			VkPushConstantRange{
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				.offset = 0,
				.size = sizeof(GPUCullingParams)
			}
		},
		{ _computeCullingIndirectDrawCommandSetLayout, _objectSetLayout, _instancePtrSetLayout },
		{ VK_SHADER_STAGE_COMPUTE_BIT, "res/shaders/indirect_culling.comp.spv" },
		computeCullingPipeline,
		computeCullingPipelineLayout,
		_swapchainDependentDeletionQueue  // Ultimately this doesn't need to change when the swapchain changes, but this allows for the shader getting reloaded when a swapchain recreation occurs.
	);
	attachPipelineToMaterial(computeCullingPipeline, computeCullingPipelineLayout, "computeCulling");

	// Compute skinning pipeline.
	VkPipeline computeSkinningPipeline;
	VkPipelineLayout computeSkinningPipelineLayout;
	vkutil::pipelinebuilder::buildCompute(
		{},
		{ _computeSkinningInoutVerticesSetLayout, _skeletalAnimationSetLayout },
		{ VK_SHADER_STAGE_COMPUTE_BIT, "res/shaders/skinned_mesh.comp.spv" },
		computeSkinningPipeline,
		computeSkinningPipelineLayout,
		_swapchainDependentDeletionQueue  // Ultimately this doesn't need to change when the swapchain changes, but this allows for the shader getting reloaded when a swapchain recreation occurs.
	);
	attachPipelineToMaterial(computeSkinningPipeline, computeSkinningPipelineLayout, "computeSkinning");

	//
	// Other pipelines
	//
	textmesh::initPipeline(screenspaceViewport, screenspaceScissor, _swapchainDependentDeletionQueue);
	textbox::initPipeline(screenspaceViewport, screenspaceScissor, _swapchainDependentDeletionQueue);
	physengine::initDebugVisPipelines(_mainRenderPass, screenspaceViewport, screenspaceScissor, _swapchainDependentDeletionQueue);
}

void VulkanEngine::generatePBRCubemaps()
{
	//
	// @NOTE: this function was copied and very slightly modified from Sascha Willem's Vulkan-glTF-PBR example.
	//

	//
	// Offline generation for the cubemaps used for PBR lighting
	// - Environment cubemap for the next two cubemaps
	// - Irradiance cubemap
	// - Pre-filterd environment cubemap
	//
	enum Target
	{
		ENVIRONMENT = 0,
		IRRADIANCE = 1,
		PREFILTEREDENV = 2,
	};

	for (uint32_t target = 0; target <= PREFILTEREDENV; target++)
	{
		//
		// Setup
		//
		Texture cubemapTexture;

		auto tStart = std::chrono::high_resolution_clock::now();

		VkFormat format;
		int32_t dim;

		switch (target)
		{
		case ENVIRONMENT:
			format = VK_FORMAT_R32G32B32A32_SFLOAT;
			dim = 512;
			break;
		case IRRADIANCE:
			format = VK_FORMAT_R32G32B32A32_SFLOAT;
			dim = 64;
			break;
		case PREFILTEREDENV:
			format = VK_FORMAT_R16G16B16A16_SFLOAT;
			dim = 512;
			break;
		};

		const uint32_t numMips = (target == ENVIRONMENT) ? 1 : static_cast<uint32_t>(floor(log2(dim))) + 1;

		// Create target cubemap

		// Image
		VkImageCreateInfo imageCI{};
		imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageCI.imageType = VK_IMAGE_TYPE_2D;
		imageCI.format = format;
		imageCI.extent.width = dim;
		imageCI.extent.height = dim;
		imageCI.extent.depth = 1;
		imageCI.mipLevels = numMips;
		imageCI.arrayLayers = 6;
		imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCI.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		imageCI.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		VmaAllocationCreateInfo imageAllocInfo = {
			.usage = VMA_MEMORY_USAGE_GPU_ONLY,
		};
		vmaCreateImage(_allocator, &imageCI, &imageAllocInfo, &cubemapTexture.image._image, &cubemapTexture.image._allocation, nullptr);

		// View
		VkImageViewCreateInfo viewCI{};
		viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewCI.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
		viewCI.format = format;
		viewCI.subresourceRange = {};
		viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewCI.subresourceRange.levelCount = numMips;
		viewCI.subresourceRange.layerCount = 6;
		viewCI.image = cubemapTexture.image._image;
		VK_CHECK(vkCreateImageView(_device, &viewCI, nullptr, &cubemapTexture.imageView));

		// Sampler
		VkSamplerCreateInfo samplerCI = {
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.magFilter = VK_FILTER_LINEAR,
			.minFilter = VK_FILTER_LINEAR,
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.maxAnisotropy = 1.0f,
			.minLod = 0.0f,
			.maxLod = static_cast<float_t>(numMips),
			.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
		};
		VK_CHECK(vkCreateSampler(_device, &samplerCI, nullptr, &cubemapTexture.sampler));

		// FB, Att, RP, Pipe, etc.
		VkAttachmentDescription attDesc{};
		// Color attachment
		attDesc.format = format;
		attDesc.samples = VK_SAMPLE_COUNT_1_BIT;
		attDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attDesc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

		VkSubpassDescription subpassDescription{};
		subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescription.colorAttachmentCount = 1;
		subpassDescription.pColorAttachments = &colorReference;

		// Use subpass dependencies for layout transitions
		std::array<VkSubpassDependency, 2> dependencies;
		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		// Renderpass
		VkRenderPassCreateInfo renderPassCI{};
		renderPassCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassCI.attachmentCount = 1;
		renderPassCI.pAttachments = &attDesc;
		renderPassCI.subpassCount = 1;
		renderPassCI.pSubpasses = &subpassDescription;
		renderPassCI.dependencyCount = 2;
		renderPassCI.pDependencies = dependencies.data();

		VkRenderPass renderpass;
		VK_CHECK(vkCreateRenderPass(_device, &renderPassCI, nullptr, &renderpass));

		struct Offscreen
		{
			Texture texture;
			VkFramebuffer framebuffer;
		} offscreen;

		// Create offscreen framebuffer
		{
			// Image
			VkImageCreateInfo imageCI{};
			imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageCI.imageType = VK_IMAGE_TYPE_2D;
			imageCI.format = format;
			imageCI.extent.width = dim;
			imageCI.extent.height = dim;
			imageCI.extent.depth = 1;
			imageCI.mipLevels = 1;
			imageCI.arrayLayers = 1;
			imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
			imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageCI.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
			imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			VmaAllocationCreateInfo imageAllocInfo = {
				.usage = VMA_MEMORY_USAGE_GPU_ONLY,
			};
			vmaCreateImage(_allocator, &imageCI, &imageAllocInfo, &offscreen.texture.image._image, &offscreen.texture.image._allocation, nullptr);

			// ImageView
			VkImageViewCreateInfo viewCI{};
			viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewCI.format = format;
			viewCI.flags = 0;
			viewCI.subresourceRange = {};
			viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			viewCI.subresourceRange.baseMipLevel = 0;
			viewCI.subresourceRange.levelCount = 1;
			viewCI.subresourceRange.baseArrayLayer = 0;
			viewCI.subresourceRange.layerCount = 1;
			viewCI.image = offscreen.texture.image._image;
			VK_CHECK(vkCreateImageView(_device, &viewCI, nullptr, &offscreen.texture.imageView));

			// Framebuffer
			VkFramebufferCreateInfo framebufferCI{};
			framebufferCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferCI.renderPass = renderpass;
			framebufferCI.attachmentCount = 1;
			framebufferCI.pAttachments = &offscreen.texture.imageView;
			framebufferCI.width = dim;
			framebufferCI.height = dim;
			framebufferCI.layers = 1;
			VK_CHECK(vkCreateFramebuffer(_device, &framebufferCI, nullptr, &offscreen.framebuffer));

			immediateSubmit([&](VkCommandBuffer cmd) {
				VkImageMemoryBarrier imageMemoryBarrier{};
				imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				imageMemoryBarrier.image = offscreen.texture.image._image;
				imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				imageMemoryBarrier.srcAccessMask = 0;
				imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				imageMemoryBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
				vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
				});
		}

		// Descriptors
		VkDescriptorSetLayout descriptorsetlayout;
		VkDescriptorSetLayoutBinding setLayoutBinding = { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
		descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		descriptorSetLayoutCI.pBindings = &setLayoutBinding;
		descriptorSetLayoutCI.bindingCount = 1;
		VK_CHECK(vkCreateDescriptorSetLayout(_device, &descriptorSetLayoutCI, nullptr, &descriptorsetlayout));

		// Descriptor Pool
		VkDescriptorPoolSize poolSize = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
		VkDescriptorPoolCreateInfo descriptorPoolCI{};
		descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		descriptorPoolCI.poolSizeCount = 1;
		descriptorPoolCI.pPoolSizes = &poolSize;
		descriptorPoolCI.maxSets = 2;
		VkDescriptorPool descriptorpool;
		VK_CHECK(vkCreateDescriptorPool(_device, &descriptorPoolCI, nullptr, &descriptorpool));


		// Descriptor sets
		VkDescriptorSet descriptorset;
		if (target != ENVIRONMENT)
		{
			VkDescriptorImageInfo environmentCubemapBufferInfo = {
				.sampler = _loadedTextures["CubemapSkybox"].sampler,  // @NOTE: CubemapSkybox is not available until it's generated by the `ENVIRONMENT` pbr texture generation step.
				.imageView = _loadedTextures["CubemapSkybox"].imageView,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};

			VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
			descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			descriptorSetAllocInfo.descriptorPool = descriptorpool;
			descriptorSetAllocInfo.pSetLayouts = &descriptorsetlayout;
			descriptorSetAllocInfo.descriptorSetCount = 1;
			VK_CHECK(vkAllocateDescriptorSets(_device, &descriptorSetAllocInfo, &descriptorset));
			VkWriteDescriptorSet writeDescriptorSet{};
			writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writeDescriptorSet.descriptorCount = 1;
			writeDescriptorSet.dstSet = descriptorset;
			writeDescriptorSet.dstBinding = 0;
			writeDescriptorSet.pImageInfo = &environmentCubemapBufferInfo;    // @TODO: @HACK: implement a proper system to get the environment cubemap!
			vkUpdateDescriptorSets(_device, 1, &writeDescriptorSet, 0, nullptr);
		}

		struct PushBlockEnvironment
		{
			mat4 mvp;
			vec3 lightDir;
			float_t sunRadius;
			float_t sunAlpha;
		} pushBlockEnvironment;

		struct PushBlockIrradiance
		{
			mat4 mvp;
			float_t deltaPhi = (2.0f * float_t(M_PI)) / 180.0f;
			float_t deltaTheta = (0.5f * float_t(M_PI)) / 64.0f;
		} pushBlockIrradiance;

		struct PushBlockPrefilterEnv
		{
			mat4 mvp;
			float_t roughness;
			uint32_t numSamples = 32u;
		} pushBlockPrefilterEnv;

		// Pipeline layout
		VkPipelineLayout pipelinelayout;
		VkPushConstantRange pushConstantRange = {
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		};

		switch (target)
		{
		case ENVIRONMENT:
			pushConstantRange.size = sizeof(PushBlockEnvironment);
			break;
		case IRRADIANCE:
			pushConstantRange.size = sizeof(PushBlockIrradiance);
			break;
		case PREFILTEREDENV:
			pushConstantRange.size = sizeof(PushBlockPrefilterEnv);
			break;
		};

		VkPipelineLayoutCreateInfo pipelineLayoutCI{};
		pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutCI.setLayoutCount = 1;
		pipelineLayoutCI.pSetLayouts = &descriptorsetlayout;
		pipelineLayoutCI.pushConstantRangeCount = 1;
		pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;
		VK_CHECK(vkCreatePipelineLayout(_device, &pipelineLayoutCI, nullptr, &pipelinelayout));

		// Pipeline
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI{};
		inputAssemblyStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		VkPipelineRasterizationStateCreateInfo rasterizationStateCI{};
		rasterizationStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
		rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterizationStateCI.lineWidth = 1.0f;

		VkPipelineColorBlendAttachmentState blendAttachmentState{};
		blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		blendAttachmentState.blendEnable = VK_FALSE;

		VkPipelineColorBlendStateCreateInfo colorBlendStateCI{};
		colorBlendStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlendStateCI.attachmentCount = 1;
		colorBlendStateCI.pAttachments = &blendAttachmentState;

		VkPipelineDepthStencilStateCreateInfo depthStencilStateCI{};
		depthStencilStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencilStateCI.depthTestEnable = VK_FALSE;
		depthStencilStateCI.depthWriteEnable = VK_FALSE;
		depthStencilStateCI.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		depthStencilStateCI.front = depthStencilStateCI.back;
		depthStencilStateCI.back.compareOp = VK_COMPARE_OP_ALWAYS;

		VkPipelineViewportStateCreateInfo viewportStateCI{};
		viewportStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportStateCI.viewportCount = 1;
		viewportStateCI.scissorCount = 1;

		VkPipelineMultisampleStateCreateInfo multisampleStateCI{};
		multisampleStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampleStateCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicStateCI{};
		dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamicStateCI.pDynamicStates = dynamicStateEnables.data();
		dynamicStateCI.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

		// Vertex input state
		VkVertexInputBindingDescription vertexInputBinding = { 0, sizeof(vkglTF::Model::Vertex), VK_VERTEX_INPUT_RATE_VERTEX };
		VkVertexInputAttributeDescription vertexInputAttribute = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };

		VkPipelineVertexInputStateCreateInfo vertexInputStateCI{};
		vertexInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInputStateCI.vertexBindingDescriptionCount = 1;
		vertexInputStateCI.pVertexBindingDescriptions = &vertexInputBinding;
		vertexInputStateCI.vertexAttributeDescriptionCount = 1;
		vertexInputStateCI.pVertexAttributeDescriptions = &vertexInputAttribute;

		VkShaderModule filtercubeVertShader,
						filtercubeFragShader;
		vkutil::pipelinebuilder::loadShaderModule("res/shaders/filtercube.vert.spv", filtercubeVertShader);
		switch (target)
		{
		case ENVIRONMENT:
			vkutil::pipelinebuilder::loadShaderModule("res/shaders/skyboxfiltercube.frag.spv", filtercubeFragShader);
			break;
		case IRRADIANCE:
			vkutil::pipelinebuilder::loadShaderModule("res/shaders/irradiancecube.frag.spv", filtercubeFragShader);
			break;
		case PREFILTEREDENV:
			vkutil::pipelinebuilder::loadShaderModule("res/shaders/prefilterenvmap.frag.spv", filtercubeFragShader);
			break;
		default:
			filtercubeFragShader = VK_NULL_HANDLE;
			break;
		};

		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {
			vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, filtercubeVertShader),
			vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, filtercubeFragShader),
		};

		VkGraphicsPipelineCreateInfo pipelineCI{};
		pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineCI.layout = pipelinelayout;
		pipelineCI.renderPass = renderpass;
		pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
		pipelineCI.pVertexInputState = &vertexInputStateCI;
		pipelineCI.pRasterizationState = &rasterizationStateCI;
		pipelineCI.pColorBlendState = &colorBlendStateCI;
		pipelineCI.pMultisampleState = &multisampleStateCI;
		pipelineCI.pViewportState = &viewportStateCI;
		pipelineCI.pDepthStencilState = &depthStencilStateCI;
		pipelineCI.pDynamicState = &dynamicStateCI;
		pipelineCI.stageCount = 2;
		pipelineCI.pStages = shaderStages.data();
		pipelineCI.renderPass = renderpass;

		VkPipeline pipeline;
		VK_CHECK(vkCreateGraphicsPipelines(_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &pipeline));
		for (auto shaderStage : shaderStages)
			vkDestroyShaderModule(_device, shaderStage.module, nullptr);

		//
		// Render cubemap
		//
		VkClearValue clearValues[1];
		clearValues[0].color = { { 0.0f, 0.0f, 0.2f, 0.0f } };    // @NOTE: the viewport doesn't resize, so when you see this clearcolor in renderdoc don't worry about it  -Timo

		VkRenderPassBeginInfo renderPassBeginInfo{};
		renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassBeginInfo.renderPass = renderpass;
		renderPassBeginInfo.framebuffer = offscreen.framebuffer;
		renderPassBeginInfo.renderArea.extent.width = dim;
		renderPassBeginInfo.renderArea.extent.height = dim;
		renderPassBeginInfo.clearValueCount = 1;
		renderPassBeginInfo.pClearValues = clearValues;

		mat4 matrices[] = {
			GLM_MAT4_IDENTITY_INIT,
			GLM_MAT4_IDENTITY_INIT,
			GLM_MAT4_IDENTITY_INIT,
			GLM_MAT4_IDENTITY_INIT,
			GLM_MAT4_IDENTITY_INIT,
			GLM_MAT4_IDENTITY_INIT,
		};

		vec3 up      = { 0.0f, 1.0f, 0.0f };
		vec3 right   = { 1.0f, 0.0f, 0.0f };
		vec3 forward = { 0.0f, 0.0f, 1.0f };

		glm_rotate(matrices[0], glm_rad(90.0f), up);
		glm_rotate(matrices[0], glm_rad(180.0f), right);

		glm_rotate(matrices[1], glm_rad(-90.0f), up);
		glm_rotate(matrices[1], glm_rad(180.0f), right);
		
		glm_rotate(matrices[2], glm_rad(-90.0f), right);
		glm_rotate(matrices[3], glm_rad(90.0f), right);
		glm_rotate(matrices[4], glm_rad(180.0f), right);
		glm_rotate(matrices[5], glm_rad(180.0f), forward);

		VkViewport viewport{};
		viewport.width = (float_t)dim;
		viewport.height = (float_t)dim;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		VkRect2D scissor{};
		scissor.extent.width = dim;
		scissor.extent.height = dim;

		VkImageSubresourceRange subresourceRange{};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.baseMipLevel = 0;
		subresourceRange.levelCount = numMips;
		subresourceRange.layerCount = 6;

		// Change image layout for all cubemap faces to transfer destination
		immediateSubmit([&](VkCommandBuffer cmd) {
			VkImageMemoryBarrier imageMemoryBarrier{};
			imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			imageMemoryBarrier.image = cubemapTexture.image._image;
			imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			imageMemoryBarrier.srcAccessMask = 0;
			imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			imageMemoryBarrier.subresourceRange = subresourceRange;
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
		});

		// Iterate thru all faces and all mips of cubemap convolution
		for (uint32_t m = 0; m < numMips; m++)
		{
			for (uint32_t f = 0; f < 6; f++)
			{
				immediateSubmit([&](VkCommandBuffer cmd) {
					viewport.width = static_cast<float_t>(dim * std::pow(0.5f, m));
					viewport.height = static_cast<float_t>(dim * std::pow(0.5f, m));
					vkCmdSetViewport(cmd, 0, 1, &viewport);
					vkCmdSetScissor(cmd, 0, 1, &scissor);

					// Render scene from cube face's point of view
					vkCmdBeginRenderPass(cmd, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

					// Pass parameters for current pass using a push constant block
					mat4 perspective;
					glm_perspective((float_t)(M_PI / 2.0), 1.0f, 0.1f, 512.0f, perspective);
					switch (target)
					{
					case ENVIRONMENT:
						glm_mat4_mul(perspective, matrices[f], pushBlockEnvironment.mvp);
						glm_vec3_copy(lightDir, pushBlockEnvironment.lightDir);
						pushBlockEnvironment.sunRadius = 0.15f;
						pushBlockEnvironment.sunAlpha = 1.0f;
						vkCmdPushConstants(cmd, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushBlockEnvironment), &pushBlockEnvironment);
						break;
					case IRRADIANCE:
						glm_mat4_mul(perspective, matrices[f], pushBlockIrradiance.mvp);
						vkCmdPushConstants(cmd, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushBlockIrradiance), &pushBlockIrradiance);
						break;
					case PREFILTEREDENV:
						glm_mat4_mul(perspective, matrices[f], pushBlockPrefilterEnv.mvp);
						pushBlockPrefilterEnv.roughness = (float_t)m / (float_t)(numMips - 1);
						vkCmdPushConstants(cmd, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushBlockPrefilterEnv), &pushBlockPrefilterEnv);
						break;
					};

					vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
					if (target != ENVIRONMENT)
						vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelinelayout, 0, 1, &descriptorset, 0, NULL);

					VkDeviceSize offsets[1] = { 0 };

					auto skybox = _roManager->getModel("Box", nullptr, [](){});
					skybox->bind(cmd);
					skybox->draw(cmd);

					vkCmdEndRenderPass(cmd);

					VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
					subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					subresourceRange.baseMipLevel = 0;
					subresourceRange.levelCount = numMips;
					subresourceRange.layerCount = 6;

					{
						VkImageMemoryBarrier imageMemoryBarrier{};
						imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
						imageMemoryBarrier.image = offscreen.texture.image._image;
						imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
						imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
						imageMemoryBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
						imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
						imageMemoryBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
						vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
					}

					// Copy region for transfer from framebuffer to cube face
					VkImageCopy copyRegion{};

					copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					copyRegion.srcSubresource.baseArrayLayer = 0;
					copyRegion.srcSubresource.mipLevel = 0;
					copyRegion.srcSubresource.layerCount = 1;
					copyRegion.srcOffset = { 0, 0, 0 };

					copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					copyRegion.dstSubresource.baseArrayLayer = f;
					copyRegion.dstSubresource.mipLevel = m;
					copyRegion.dstSubresource.layerCount = 1;
					copyRegion.dstOffset = { 0, 0, 0 };

					copyRegion.extent.width = static_cast<uint32_t>(viewport.width);
					copyRegion.extent.height = static_cast<uint32_t>(viewport.height);
					copyRegion.extent.depth = 1;

					vkCmdCopyImage(
						cmd,
						offscreen.texture.image._image,
						VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
						cubemapTexture.image._image,
						VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
						1, &copyRegion
					);

					{
						VkImageMemoryBarrier imageMemoryBarrier{};
						imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
						imageMemoryBarrier.image = offscreen.texture.image._image;
						imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
						imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
						imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
						imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
						imageMemoryBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
						vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
					}
				});
			}
		}

		// Change final texture to shader compatible
		immediateSubmit([&](VkCommandBuffer cmd) {
			VkImageMemoryBarrier imageMemoryBarrier{};
			imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			imageMemoryBarrier.image = cubemapTexture.image._image;
			imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			imageMemoryBarrier.dstAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
			imageMemoryBarrier.subresourceRange = subresourceRange;
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
		});

		//
		// Cleanup
		//
		vkDestroyRenderPass(_device, renderpass, nullptr);
		vkDestroyFramebuffer(_device, offscreen.framebuffer, nullptr);
		vkDestroyImageView(_device, offscreen.texture.imageView, nullptr);
		vmaDestroyImage(_allocator, offscreen.texture.image._image, offscreen.texture.image._allocation);
		vkDestroyDescriptorPool(_device, descriptorpool, nullptr);
		vkDestroyDescriptorSetLayout(_device, descriptorsetlayout, nullptr);
		vkDestroyPipeline(_device, pipeline, nullptr);
		vkDestroyPipelineLayout(_device, pipelinelayout, nullptr);

		_mainDeletionQueue.pushFunction([=]() {
			vkDestroySampler(_device, cubemapTexture.sampler, nullptr);
			vkDestroyImageView(_device, cubemapTexture.imageView, nullptr);
			vmaDestroyImage(_allocator, cubemapTexture.image._image, cubemapTexture.image._allocation);
		});

		// Apply the created texture/sampler to global scene
		std::string cubemapTypeName = "";
		switch (target)
		{
		case ENVIRONMENT:
			cubemapTypeName = "environment";
			_loadedTextures["CubemapSkybox"] = cubemapTexture;
			break;
		case IRRADIANCE:
			cubemapTypeName = "irradiance";
			_pbrSceneTextureSet.irradianceCubemap = cubemapTexture;
			break;
		case PREFILTEREDENV:
			cubemapTypeName = "prefilter";
			_pbrRendering.gpuSceneShadingProps.prefilteredCubemapMipLevels = static_cast<float_t>(numMips);
			_pbrSceneTextureSet.prefilteredCubemap = cubemapTexture;
			break;
		};

		// Report time it took
		auto tEnd = std::chrono::high_resolution_clock::now();
		auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
		std::cout << "[GENERATING PBR CUBEMAP]" << std::endl
			<< "type:               " << cubemapTypeName << std::endl
			<< "mip levels:         " << numMips << std::endl
			<< "execution duration: " << tDiff << " ms" << std::endl;
	}
}

void VulkanEngine::generateBRDFLUT()
{
	//
	// @NOTE: this function was copied and very slightly modified from Sascha Willem's Vulkan-glTF-PBR example.
	//
	auto tStart = std::chrono::high_resolution_clock::now();

	//
	// Setup
	//
	const VkFormat format = VK_FORMAT_R16G16_SFLOAT;
	const int32_t dim = 512;

	// Image
	VkImageCreateInfo imageCI{};
	imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageCI.imageType = VK_IMAGE_TYPE_2D;
	imageCI.format = format;
	imageCI.extent.width = dim;
	imageCI.extent.height = dim;
	imageCI.extent.depth = 1;
	imageCI.mipLevels = 1;
	imageCI.arrayLayers = 1;
	imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
	imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageCI.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	VmaAllocationCreateInfo imageAllocInfo = {
		.usage = VMA_MEMORY_USAGE_GPU_ONLY,
	};
	Texture brdfLUTTexture;
	vmaCreateImage(_allocator, &imageCI, &imageAllocInfo, &brdfLUTTexture.image._image, &brdfLUTTexture.image._allocation, nullptr);

	// ImageView
	VkImageViewCreateInfo viewCI{};
	viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewCI.format = format;
	viewCI.subresourceRange = {};
	viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewCI.subresourceRange.levelCount = 1;
	viewCI.subresourceRange.layerCount = 1;
	viewCI.image = brdfLUTTexture.image._image;
	VK_CHECK(vkCreateImageView(_device, &viewCI, nullptr, &brdfLUTTexture.imageView));

	// Sampler
	VkSamplerCreateInfo samplerCI = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.maxAnisotropy = 1.0f,
		.minLod = 0.0f,
		.maxLod = 1.0f,
		.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
	};
	VK_CHECK(vkCreateSampler(_device, &samplerCI, nullptr, &brdfLUTTexture.sampler));

	// FB, Att, RP, Pipe, etc.
	VkAttachmentDescription attDesc{};
	// Color attachment
	attDesc.format = format;
	attDesc.samples = VK_SAMPLE_COUNT_1_BIT;
	attDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attDesc.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

	VkSubpassDescription subpassDescription{};
	subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpassDescription.colorAttachmentCount = 1;
	subpassDescription.pColorAttachments = &colorReference;

	// Use subpass dependencies for layout transitions
	std::array<VkSubpassDependency, 2> dependencies;
	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
	dependencies[1].srcSubpass = 0;
	dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	// Create the actual renderpass
	VkRenderPassCreateInfo renderPassCI{};
	renderPassCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassCI.attachmentCount = 1;
	renderPassCI.pAttachments = &attDesc;
	renderPassCI.subpassCount = 1;
	renderPassCI.pSubpasses = &subpassDescription;
	renderPassCI.dependencyCount = 2;
	renderPassCI.pDependencies = dependencies.data();

	VkRenderPass renderpass;
	VK_CHECK(vkCreateRenderPass(_device, &renderPassCI, nullptr, &renderpass));

	VkFramebufferCreateInfo framebufferCI{};
	framebufferCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebufferCI.renderPass = renderpass;
	framebufferCI.attachmentCount = 1;
	framebufferCI.pAttachments = &brdfLUTTexture.imageView;
	framebufferCI.width = dim;
	framebufferCI.height = dim;
	framebufferCI.layers = 1;

	VkFramebuffer framebuffer;
	VK_CHECK(vkCreateFramebuffer(_device, &framebufferCI, nullptr, &framebuffer));

	// Desriptors
	VkDescriptorSetLayout descriptorsetlayout;
	VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
	descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	VK_CHECK(vkCreateDescriptorSetLayout(_device, &descriptorSetLayoutCI, nullptr, &descriptorsetlayout));

	// Pipeline layout
	VkPipelineLayout pipelinelayout;
	VkPipelineLayoutCreateInfo pipelineLayoutCI{};
	pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutCI.setLayoutCount = 1;
	pipelineLayoutCI.pSetLayouts = &descriptorsetlayout;
	VK_CHECK(vkCreatePipelineLayout(_device, &pipelineLayoutCI, nullptr, &pipelinelayout));

	// Pipeline
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI{};
	inputAssemblyStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineRasterizationStateCreateInfo rasterizationStateCI{};
	rasterizationStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
	rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizationStateCI.lineWidth = 1.0f;

	VkPipelineColorBlendAttachmentState blendAttachmentState{};
	blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	blendAttachmentState.blendEnable = VK_FALSE;

	VkPipelineColorBlendStateCreateInfo colorBlendStateCI{};
	colorBlendStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlendStateCI.attachmentCount = 1;
	colorBlendStateCI.pAttachments = &blendAttachmentState;

	VkPipelineDepthStencilStateCreateInfo depthStencilStateCI{};
	depthStencilStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencilStateCI.depthTestEnable = VK_FALSE;
	depthStencilStateCI.depthWriteEnable = VK_FALSE;
	depthStencilStateCI.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	depthStencilStateCI.front = depthStencilStateCI.back;
	depthStencilStateCI.back.compareOp = VK_COMPARE_OP_ALWAYS;

	VkPipelineViewportStateCreateInfo viewportStateCI{};
	viewportStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportStateCI.viewportCount = 1;
	viewportStateCI.scissorCount = 1;

	VkPipelineMultisampleStateCreateInfo multisampleStateCI{};
	multisampleStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampleStateCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicStateCI{};
	dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicStateCI.pDynamicStates = dynamicStateEnables.data();
	dynamicStateCI.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

	VkPipelineVertexInputStateCreateInfo emptyInputStateCI{};
	emptyInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

	VkShaderModule genBrdfLUTVertShader,
					genBrdfLUTFragShader;
	vkutil::pipelinebuilder::loadShaderModule("res/shaders/genbrdflut.vert.spv", genBrdfLUTVertShader);
	vkutil::pipelinebuilder::loadShaderModule("res/shaders/genbrdflut.frag.spv", genBrdfLUTFragShader);

	std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {
		vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, genBrdfLUTVertShader),
		vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, genBrdfLUTFragShader),
	};

	VkGraphicsPipelineCreateInfo pipelineCI{};
	pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineCI.layout = pipelinelayout;
	pipelineCI.renderPass = renderpass;
	pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
	pipelineCI.pVertexInputState = &emptyInputStateCI;
	pipelineCI.pRasterizationState = &rasterizationStateCI;
	pipelineCI.pColorBlendState = &colorBlendStateCI;
	pipelineCI.pMultisampleState = &multisampleStateCI;
	pipelineCI.pViewportState = &viewportStateCI;
	pipelineCI.pDepthStencilState = &depthStencilStateCI;
	pipelineCI.pDynamicState = &dynamicStateCI;
	pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
	pipelineCI.pStages = shaderStages.data();

	// Look-up-table (from BRDF) pipeline
	VkPipeline pipeline;
	VK_CHECK(vkCreateGraphicsPipelines(_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &pipeline));
	for (auto shaderStage : shaderStages)
		vkDestroyShaderModule(_device, shaderStage.module, nullptr);

	//
	// Render
	//
	VkClearValue clearValues[1];
	clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

	VkRenderPassBeginInfo renderPassBeginInfo{};
	renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassBeginInfo.renderPass = renderpass;
	renderPassBeginInfo.renderArea.extent.width = dim;
	renderPassBeginInfo.renderArea.extent.height = dim;
	renderPassBeginInfo.clearValueCount = 1;
	renderPassBeginInfo.pClearValues = clearValues;
	renderPassBeginInfo.framebuffer = framebuffer;

	immediateSubmit([&](VkCommandBuffer cmd) {
		vkCmdBeginRenderPass(cmd, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport viewport{};
		viewport.width = (float)dim;
		viewport.height = (float)dim;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		VkRect2D scissor{};
		scissor.extent.width = dim;
		scissor.extent.height = dim;

		vkCmdSetViewport(cmd, 0, 1, &viewport);
		vkCmdSetScissor(cmd, 0, 1, &scissor);
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		vkCmdDraw(cmd, 3, 1, 0, 0);

		vkCmdEndRenderPass(cmd);
		});

	//
	// Cleanup
	//
	vkQueueWaitIdle(_graphicsQueue);

	vkDestroyPipeline(_device, pipeline, nullptr);
	vkDestroyPipelineLayout(_device, pipelinelayout, nullptr);
	vkDestroyRenderPass(_device, renderpass, nullptr);
	vkDestroyFramebuffer(_device, framebuffer, nullptr);
	vkDestroyDescriptorSetLayout(_device, descriptorsetlayout, nullptr);

	_mainDeletionQueue.pushFunction([=]() {
		vkDestroySampler(_device, brdfLUTTexture.sampler, nullptr);
		vkDestroyImageView(_device, brdfLUTTexture.imageView, nullptr);
		vmaDestroyImage(_allocator, brdfLUTTexture.image._image, brdfLUTTexture.image._allocation);
		});

	// Apply the created texture/sampler to global scene
	_pbrSceneTextureSet.brdfLUTTexture = brdfLUTTexture;

	// Report time it took
	auto tEnd = std::chrono::high_resolution_clock::now();
	auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
	std::cout << "[GENERATING BRDF LUT]" << std::endl
		<< "execution duration: " << tDiff << " ms" << std::endl;
}

void VulkanEngine::initImgui()
{
	//
	// Create descriptor pool for imgui
	//
	VkDescriptorPoolSize poolSizes[] = {
		{ VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
	};
	VkDescriptorPoolCreateInfo poolInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
		.maxSets = 1000,
		.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes)),
		.pPoolSizes = poolSizes,
	};
	VkDescriptorPool imguiPool;
	VK_CHECK(vkCreateDescriptorPool(_device, &poolInfo, nullptr, &imguiPool));

	//
	// Init dear imgui
	//
	ImGui::CreateContext();
	ImPlot::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
	auto& style = ImGui::GetStyle();
	style.Alpha = 0.9f;
	ImGui_ImplSDL2_InitForVulkan(_window);

	ImGui_ImplVulkan_InitInfo initInfo = {
		.Instance = _instance,
		.PhysicalDevice = _chosenGPU,
		.Device = _device,
		.Queue = _graphicsQueue,
		.DescriptorPool = imguiPool,
		.MinImageCount = 3,
		.ImageCount = 3,
		.MSAASamples = VK_SAMPLE_COUNT_1_BIT,
	};
	ImGui_ImplVulkan_Init(&initInfo, _postprocessRenderPass);

	// Load in imgui font textures
	immediateSubmit([&](VkCommandBuffer cmd) {
		ImGui_ImplVulkan_CreateFontsTexture(cmd);
		});
	ImGui_ImplVulkan_DestroyFontUploadObjects();

	// Add destroy command for cleanup
	_mainDeletionQueue.pushFunction([=]() {
		vkDestroyDescriptorPool(_device, imguiPool, nullptr);
		ImGui_ImplVulkan_Shutdown();
		ImPlot::DestroyContext();
		ImGui::DestroyContext();
		});
}

void VulkanEngine::recreateSwapchain()
{
	int w, h;
	SDL_GetWindowSize(_window, &w, &h);

	if (w <= 0 || h <= 0)
		return;

	vkDeviceWaitIdle(_device);

	_windowExtent.width = w;
	_windowExtent.height = h;
	_camera->sceneCamera.aspect = (float_t)w / (float_t)h;
	_camera->sceneCamera.boxCastExtents[0] = _camera->sceneCamera.zNear * std::tan(_camera->sceneCamera.fov * 0.5f) * _camera->sceneCamera.aspect;
	_camera->sceneCamera.boxCastExtents[1] = _camera->sceneCamera.zNear * std::tan(_camera->sceneCamera.fov * 0.5f);
	_camera->sceneCamera.boxCastExtents[2] = _camera->sceneCamera.zNear * 0.5f;

	_swapchainDependentDeletionQueue.flush();

	initSwapchain();
	initShadowRenderpass();
	initMainRenderpass();
	initUIRenderpass();
	initPostprocessRenderpass();
	initPostprocessImages();
	initPickingRenderpass();
	initFramebuffers();
	initPipelines();
	loadMaterials();

	_camera->sceneCamera.recalculateSceneCamera(_pbrRendering.gpuSceneShadingProps);

	_recreateSwapchain = false;
}

FrameData& VulkanEngine::getCurrentFrame()
{
	return _frames[_frameNumber % FRAME_OVERLAP];
}

void VulkanEngine::loadMaterials()
{
	for (const auto& entry : std::filesystem::recursive_directory_iterator("res/materials/"))
	{
		const auto& path = entry.path();
		if (std::filesystem::is_directory(path) ||
			!path.has_extension())
			continue;

		if (path.extension().compare(".humba") == 0)
		{
			materialorganizer::loadMaterialBase(path);
		}
		else if (path.extension().compare(".hderriere") == 0)
		{
			materialorganizer::loadDerivedMaterialParam(path);
		}
	}
	materialorganizer::cookTextureIndices();
}

void VulkanEngine::loadMeshes()
{
	// @NOTE: `MULTITHREAD_MESH_LOADING` cannot be used if rapidjson is the json parser for tiny_gltf.h
#define MULTITHREAD_MESH_LOADING 0
#if MULTITHREAD_MESH_LOADING
	tf::Executor e;
	tf::Taskflow taskflow;
#endif

	std::vector<std::pair<std::string, vkglTF::Model*>> modelNameAndModels;
	for (const auto& entry : std::filesystem::recursive_directory_iterator("res/models_cooked/"))
	{
		const auto& path = entry.path();
		if (std::filesystem::is_directory(path))
			continue;		// Ignore directories
		if (!path.has_extension() ||
			path.extension().compare(".hthrobwoa") != 0)
			continue;		// @NOTE: ignore non-model files

		modelNameAndModels.push_back(
			std::make_pair<std::string, vkglTF::Model*>(path.stem().string(), nullptr)
		);

		size_t      targetIndex         = modelNameAndModels.size() - 1;
		std::string pathStringHthrobwoa = path.string();
		std::string pathStringHenema    = "res/models_cooked/" + path.stem().string() + ".henema";

#if MULTITHREAD_MESH_LOADING
		taskflow.emplace([&, targetIndex, pathString]() {
#endif
			modelNameAndModels[targetIndex].second = new vkglTF::Model();
			modelNameAndModels[targetIndex].second->loadHthrobwoaFromFile(this, pathStringHthrobwoa, pathStringHenema);
#if MULTITHREAD_MESH_LOADING
		});
#endif
	}
#if MULTITHREAD_MESH_LOADING
	e.run(taskflow).wait();
#endif

	for (auto& pair : modelNameAndModels)
		_roManager->createModel(pair.second, pair.first);
}

void VulkanEngine::uploadCurrentFrameToGPU(const FrameData& currentFrame)
{
	//
	// Upload Camera Data to GPU
	//
	void* data;
	vmaMapMemory(_allocator, currentFrame.cameraBuffer._allocation, &data);
	memcpy(data, &_camera->sceneCamera.gpuCameraData, sizeof(GPUCameraData));
	vmaUnmapMemory(_allocator, currentFrame.cameraBuffer._allocation);

	//
	// Upload pbr shading props to GPU
	//
	vmaMapMemory(_allocator, currentFrame.pbrShadingPropsBuffer._allocation, &data);
	memcpy(data, &_pbrRendering.gpuSceneShadingProps, sizeof(GPUPBRShadingProps));
	vmaUnmapMemory(_allocator, currentFrame.pbrShadingPropsBuffer._allocation);

	//
	// Fill in object data into current frame object buffer
	//
	{
		std::lock_guard<std::mutex> lg(_roManager->renderObjectIndicesAndPoolMutex);
		void* objectData;
		vmaMapMemory(_allocator, currentFrame.objectBuffer._allocation, &objectData);
		GPUObjectData* objectSSBO = (GPUObjectData*)objectData;    // @IMPROVE: perhaps multithread this? Or only update when the object moves?
		for (size_t poolIndex : _roManager->_renderObjectsIndices)  // @NOTE: bc of the pool system these indices will be scattered, but that should work just fine
		{
			// Another evil pointer trick I love... call me Dmitri the Evil!

			// Assign model matrix.
			mat4& modelMatrix = _roManager->_renderObjectPool[poolIndex].transformMatrix;
			glm_mat4_copy(
				modelMatrix,
				objectSSBO[poolIndex].modelMatrix
			);

			// Calc bounding sphere center.
			vec3& boundingSphereCenter = _roManager->_renderObjectPool[poolIndex].model->boundingSphere.center;
			vec4 boundingSphere = { boundingSphereCenter[0], boundingSphereCenter[1], boundingSphereCenter[2], 1.0f };
			glm_mat4_mulv(
				modelMatrix,
				boundingSphere,
				boundingSphere
			);

			// Calc bounding sphere radius.
			vec3 scale;
			glm_decompose_scalev(modelMatrix, scale);
			glm_vec3_abs(scale, scale);
			boundingSphere[3] =
				_roManager->_renderObjectPool[poolIndex].model->boundingSphere.radius
					* glm_vec3_max(scale);

			// Assign bounding sphere.
			glm_vec4_copy(
				boundingSphere,
				objectSSBO[poolIndex].boundingSphere
			);
		}
		vmaUnmapMemory(_allocator, currentFrame.objectBuffer._allocation);
	}
}

void VulkanEngine::createSkinningBuffers(FrameData& currentFrame)
{
	destroySkinningBuffersIfCreated(currentFrame);

	if (!_roManager->_skinnedMeshEntriesExist)
		return;  // Exit early bc buffers will be initialized to be empty.

	// Traverse buckets and count up skinned mesh indices.
	// (While also caching mesh vertices and indices).
	struct SkinnedMesh
	{
		size_t modelIdx;
		size_t meshIdx;
		size_t animatorNodeID;
		vkglTF::Model* model;
	};
	struct MeshVerticesIndices
	{
		std::set<uint32_t> uniqueVertexIndices;
		std::vector<uint32_t> indicesNormalized;
	};

	auto& s = currentFrame.skinning;
	s.numVertices = 0;
	s.numIndices = 0;
	std::vector<SkinnedMesh> skinnedMeshes;
	std::map<size_t, MeshVerticesIndices> modelMeshHashToVerticesIndices;

	for (size_t i = 0; i < _roManager->_numUmbBuckets; i++)
	{
		auto& umbBucket = _roManager->_umbBuckets[i];
		size_t j = 0;  // Only do skinned pass.
		{
			bool isSkinnedPass = (j == 0);
			for (size_t k = 0; k < _roManager->_numModelBuckets; k++)
			{
				auto& modelBucket = umbBucket.modelBucketSets[j].modelBuckets[k];
				for (size_t l = 0; l < _roManager->_numMeshBucketsByModelIdx[k]; l++)
				{
					auto& meshBucket = modelBucket.meshBuckets[l];
					if (meshBucket.renderObjectIndices.empty())
						continue;

					auto& meshDraw = _roManager->_modelMeshDraws[k][l];

					// Fetch/calc num vertices in mesh.
					size_t modelMeshHash = k | l << 32;
					if (modelMeshHashToVerticesIndices.find(modelMeshHash) == modelMeshHashToVerticesIndices.end())
					{
						// Calculate, then add into cache.
						std::set<uint32_t> uniqueVertexIndices;
						std::vector<uint32_t> indicesNormalized;

						// Insert in indices to create sorted, unique set.
						for (size_t vertex = meshDraw.meshFirstIndex;
							vertex < meshDraw.meshFirstIndex + meshDraw.meshIndexCount;
							vertex++)
						{
							uint32_t index = meshDraw.model->loaderInfo.indexBuffer[vertex];
							uniqueVertexIndices.emplace(index);
							indicesNormalized.push_back(index);
						}

						// Normalize indices using sorted set.
						uint32_t nextIndex = 0;
						for (uint32_t uniqueIndex : uniqueVertexIndices)
						{
							for (auto& normalizedIndex : indicesNormalized)
								if (uniqueIndex == normalizedIndex)
									normalizedIndex = nextIndex;
							nextIndex++;
						}

						// Cache.
						modelMeshHashToVerticesIndices[modelMeshHash] = {
							.uniqueVertexIndices = uniqueVertexIndices,
							.indicesNormalized = indicesNormalized,
						};
					}

					// Insert stats into skinnedMeshes and counts.
					size_t meshVertexCount = modelMeshHashToVerticesIndices[modelMeshHash].uniqueVertexIndices.size();
					for (auto& roIdx : meshBucket.renderObjectIndices)
					{
						s.numVertices += meshVertexCount;
						s.numIndices += meshDraw.meshIndexCount;
						skinnedMeshes.push_back({
							.modelIdx = k,
							.meshIdx = l,
							.animatorNodeID = _roManager->_renderObjectPool[roIdx].calculatedModelInstances[l].animatorNodeID,
							.model = meshDraw.model,
						});
					}
				}
			}
		}
	}

	// Create buffers.
	// @TODO: turn these into transfer/staging buffers.
	size_t inputVerticesBufferSize = sizeof(GPUInputSkinningMeshPrefixData) + sizeof(GPUInputSkinningMeshData) * s.numVertices;
	s.inputVerticesBuffer = createBuffer(inputVerticesBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

	s.outputBufferSize = sizeof(GPUOutputSkinningMeshData) * s.numVertices;
	s.outputVerticesBuffer = createBuffer(s.outputBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);  // @NOTE: no staging buffer for this bc all the data is loaded via compute shader on the gpu!

	size_t indicesBufferSize = sizeof(uint32_t) * s.numIndices;
	s.indicesBuffer = createBuffer(indicesBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

	// Upload input vertices.
	{
		uint8_t* data;
		vmaMapMemory(_allocator, s.inputVerticesBuffer._allocation, (void**)&data);

		// Insert prefix data.
		GPUInputSkinningMeshPrefixData ismpd = {
			.numVertices = (uint32_t)s.numVertices,
		};
		memcpy(data, &ismpd, sizeof(GPUInputSkinningMeshPrefixData));
		data += sizeof(GPUInputSkinningMeshPrefixData);

		// Insert remaining data.
		for (auto& sm : skinnedMeshes)
		{
			size_t modelMeshHash = sm.modelIdx | sm.meshIdx << 32;
			auto& vi = modelMeshHashToVerticesIndices[modelMeshHash];
			for (auto& idx : vi.uniqueVertexIndices)
			{
				auto& vert = sm.model->loaderInfo.vertexWithWeightsBuffer[idx];
				GPUInputSkinningMeshData ismd = {};
				glm_vec3_copy(vert.pos, ismd.pos);
				glm_vec3_copy(vert.normal, ismd.normal);
				glm_vec2_copy(vert.uv0, ismd.UV0);
				glm_vec2_copy(vert.uv1, ismd.UV1);
				glm_vec4_copy(vert.joint0, ismd.joint0);
				glm_vec4_copy(vert.weight0, ismd.weight0);
				glm_vec4_copy(vert.color, ismd.color0);
				ismd.animatorNodeID = sm.animatorNodeID;
				ismd.baseInstanceID = 0;  // @DEPRECATED: base instance id offset is unnecessary now.

				memcpy(data, &ismd, sizeof(GPUInputSkinningMeshData));
				data += sizeof(GPUInputSkinningMeshData);
			}
		}

		vmaUnmapMemory(_allocator, s.inputVerticesBuffer._allocation);
	}

	// Upload indices.
	{
		uint8_t* data;
		vmaMapMemory(_allocator, s.indicesBuffer._allocation, (void**)&data);

		// Insert data.
		uint32_t indexOffset = 0;
		for (auto& sm : skinnedMeshes)
		{
			size_t modelMeshHash = sm.modelIdx | sm.meshIdx << 32;
			auto& vi = modelMeshHashToVerticesIndices[modelMeshHash];
			for (auto& idx : vi.indicesNormalized)
			{
				uint32_t indexCooked = idx + indexOffset;
				memcpy(data, &indexCooked, sizeof(uint32_t));
				data += sizeof(uint32_t);
			}
			indexOffset += vi.uniqueVertexIndices.size();  // Offset by num vertices bc that's the max index.
		}

		vmaUnmapMemory(_allocator, s.indicesBuffer._allocation);
	}

	// Create descriptors.
	VkDescriptorBufferInfo inputVerticesBufferInfo = {
		.buffer = s.inputVerticesBuffer._buffer,
		.offset = 0,
		.range = inputVerticesBufferSize,
	};
	VkDescriptorBufferInfo outputVerticesBufferInfo = {
		.buffer = s.outputVerticesBuffer._buffer,
		.offset = 0,
		.range = s.outputBufferSize,
	};
	vkutil::DescriptorBuilder::begin()
		.bindBuffer(0, &inputVerticesBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
		.bindBuffer(1, &outputVerticesBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
		.build(s.inoutVerticesDescriptor);

	// Finish.
	s.created = true;
	s.recalculateSkinningBuffers = false;
}

void VulkanEngine::destroySkinningBuffersIfCreated(FrameData& currentFrame)
{
	auto& s = currentFrame.skinning;
	if (s.created)
	{
		vmaDestroyBuffer(_allocator, s.inputVerticesBuffer._buffer, s.inputVerticesBuffer._allocation);
		vmaDestroyBuffer(_allocator, s.outputVerticesBuffer._buffer, s.outputVerticesBuffer._allocation);
		vmaDestroyBuffer(_allocator, s.indicesBuffer._buffer, s.indicesBuffer._allocation);
		s.created = false;
	}
}

void VulkanEngine::compactRenderObjectsIntoDraws(FrameData& currentFrame, std::vector<size_t> onlyPoolIndices, std::vector<ModelWithIndirectDrawId>& outIndirectDrawCommandIdsForPoolIndex)
{
	GPUInstancePointer* instancePtrSSBO;
	vmaMapMemory(_allocator, currentFrame.instancePtrBuffer._allocation, (void**)&instancePtrSSBO);
	VkDrawIndexedIndirectCommand* indirectDrawCommands;
	vmaMapMemory(_allocator, currentFrame.indirectDrawCommandRawBuffer._allocation, (void**)&indirectDrawCommands);
	GPUIndirectDrawCommandOffsetsData* indirectDrawCommandOffsets;
	vmaMapMemory(_allocator, currentFrame.indirectDrawCommandOffsetsBuffer._allocation, (void**)&indirectDrawCommandOffsets);
	uint32_t* indirectDrawCommandCountsShadow;
	vmaMapMemory(_allocator, currentFrame.indirectShadowPass.indirectDrawCommandCountsBuffer._allocation, (void**)&indirectDrawCommandCountsShadow);
	uint32_t* indirectDrawCommandCountsMain;
	vmaMapMemory(_allocator, currentFrame.indirectMainPass.indirectDrawCommandCountsBuffer._allocation, (void**)&indirectDrawCommandCountsMain);

	// Traverse thru bucket to write commands.
	{
		std::vector<IndirectBatch> batches;
		size_t nextSkinnedIndex = 0;
		size_t instanceID = 0;

		std::lock_guard<std::mutex> lg(_roManager->renderObjectIndicesAndPoolMutex);

		for (size_t i = 0; i < _roManager->_numUmbBuckets; i++)
		{
			auto& umbBucket = _roManager->_umbBuckets[i];
			for (size_t j = 0; j < 2; j++)
			{
				bool isSkinnedPass = (j == 0);
				auto modelIter = _roManager->_renderObjectModels.begin();
				for (size_t k = 0; k < _roManager->_numModelBuckets; k++, modelIter++)
				{
					auto& modelBucket = umbBucket.modelBucketSets[j].modelBuckets[k];

					// Create new batch.
					IndirectBatch batch = {
						.model = (isSkinnedPass ? (vkglTF::Model*)&_roManager->_skinnedMeshModelMemAddr : modelIter->second),
						.uniqueMaterialBaseId = (uint32_t)i,
						.first = (uint32_t)instanceID,  // @NOTE: This is actually the draw command id.
						.count = 0,
					};

					for (size_t l = 0; l < _roManager->_numMeshBucketsByModelIdx[k]; l++)
					{
						auto& meshBucket = modelBucket.meshBuckets[l];
						for (auto& roIdx : meshBucket.renderObjectIndices)
						{
							auto& meshDraw = _roManager->_modelMeshDraws[k][l];
							*indirectDrawCommands = {
								.indexCount = meshDraw.meshIndexCount,
								.instanceCount = 1,
								.firstIndex = (isSkinnedPass ? (uint32_t)nextSkinnedIndex : meshDraw.meshFirstIndex),
								.vertexOffset = 0,
								.firstInstance = (uint32_t)instanceID,
							};

							*indirectDrawCommandOffsets = {
								.batchFirstIndex = batch.first,
								.countIndex = (uint32_t)batches.size(),
							};

							GPUInstancePointer& gip = _roManager->_renderObjectPool[roIdx].calculatedModelInstances[l];
							*instancePtrSSBO = gip;

#ifdef _DEVELOP
							if (!onlyPoolIndices.empty())  // Is this line even necessary? I'm doing this bc the for loop setup code could take longer than just checking whether the list is empty.
								for (size_t index : onlyPoolIndices)
									if (index == gip.objectID)
									{
										// Include this draw indirect command list for picking.
										outIndirectDrawCommandIdsForPoolIndex.push_back({
											meshDraw.model,
											(uint32_t)instanceID
										});
										break;
									}
#endif

							if (isSkinnedPass)
								nextSkinnedIndex += meshDraw.meshIndexCount;  // Jump num indices to go to next index group (if wanting to do an offset, use vertex count).
							indirectDrawCommands++;
							indirectDrawCommandOffsets++;
							instancePtrSSBO++;
							instanceID++;
							batch.count++;
						}
					}

					if (batch.count > 0)
					{
						batches.push_back(batch);  // Only add the batch in if there are instances in the batch.

						// Init as count of 0 so that culling can increment this value.
						*indirectDrawCommandCountsShadow = 0;
						indirectDrawCommandCountsShadow++;
						*indirectDrawCommandCountsMain = 0;
						indirectDrawCommandCountsMain++;
					}
				}
			}
		}

		currentFrame.numInstances = instanceID;
		indirectBatches = batches;
	}

	// Finish.
	vmaUnmapMemory(_allocator, currentFrame.instancePtrBuffer._allocation);
	vmaUnmapMemory(_allocator, currentFrame.indirectDrawCommandRawBuffer._allocation);
	vmaUnmapMemory(_allocator, currentFrame.indirectDrawCommandOffsetsBuffer._allocation);
	vmaUnmapMemory(_allocator, currentFrame.indirectShadowPass.indirectDrawCommandCountsBuffer._allocation);
	vmaUnmapMemory(_allocator, currentFrame.indirectMainPass.indirectDrawCommandCountsBuffer._allocation);
}

void VulkanEngine::renderRenderObjects(VkCommandBuffer cmd, const FrameData& currentFrame, bool materialOverride, bool useShadowIndirectPass)
{
	auto& pass = useShadowIndirectPass ? currentFrame.indirectShadowPass : currentFrame.indirectMainPass;

	// Iterate thru all the batches
	vkglTF::Model* lastModel = nullptr;
	size_t lastUMBIdx = (size_t)-1;
	uint32_t drawStride = sizeof(VkDrawIndexedIndirectCommand);
	uint32_t countStride = sizeof(uint32_t);
	uint32_t countIdx = 0;
	for (IndirectBatch& batch : indirectBatches)
	{
		if (lastModel != batch.model)
		{
			if (batch.model == (vkglTF::Model*)&_roManager->_skinnedMeshModelMemAddr)
			{
				// Bind the compute skinned intermediate buffer.
				const VkDeviceSize offsets[1] = { 0 };
				vkCmdBindVertexBuffers(cmd, 0, 1, &currentFrame.skinning.outputVerticesBuffer._buffer, offsets);
				vkCmdBindIndexBuffer(cmd, currentFrame.skinning.indicesBuffer._buffer, 0, VK_INDEX_TYPE_UINT32);
			}
			else
				batch.model->bind(cmd);
			lastModel = batch.model;
		}
		if (!materialOverride && lastUMBIdx != batch.uniqueMaterialBaseId)
		{
			// Bind material
			// @TODO: put this into its own function!
			Material& uMaterial = *getMaterial(materialorganizer::umbIdxToUniqueMaterialName(batch.uniqueMaterialBaseId));    // @HACK: @TODO: currently, the way that the pipeline is getting used is by just hardcode using it in the draw commands for models... however, each model should get its pipeline set to this material instead (or whatever material its using... that's why we can't hardcode stuff!!!)   @TODO: create some kind of way to propagate the newly created pipeline to the primMat (calculated material in the gltf model) instead of using uMaterial directly.  -Timo
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, uMaterial.pipeline);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, uMaterial.pipelineLayout, 0, 1, &currentFrame.globalDescriptor, 0, nullptr);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, uMaterial.pipelineLayout, 1, 1, &currentFrame.objectDescriptor, 0, nullptr);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, uMaterial.pipelineLayout, 2, 1, &currentFrame.instancePtrDescriptor, 0, nullptr);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, uMaterial.pipelineLayout, 3, 1, &uMaterial.textureSet, 0, nullptr);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, uMaterial.pipelineLayout, 4, 1, &_voxelFieldLightingGridTextureSet.descriptor, 0, nullptr);
			////////////////
			
			lastUMBIdx = batch.uniqueMaterialBaseId;
		}
		VkDeviceSize indirectOffset = batch.first * drawStride;
		VkDeviceSize countOffset = countIdx * countStride;
		// vkCmdDrawIndexedIndirect(cmd, currentFrame.indirectDrawCommandRawBuffer._buffer, indirectOffset, batch.count, drawStride);
		vkCmdDrawIndexedIndirectCount(cmd, pass.indirectDrawCommandsBuffer._buffer, indirectOffset, pass.indirectDrawCommandCountsBuffer._buffer, countOffset, batch.count, drawStride);
		countIdx++;
	}
}

bool VulkanEngine::searchForPickedObjectPoolIndex(size_t& outPoolIndex)
{
	for (size_t i = 0; i < _roManager->_renderObjectsIndices.size(); i++)
	{
		size_t poolIndex = _roManager->_renderObjectsIndices[i];
		if (_movingMatrix.matrixToMove == &_roManager->_renderObjectPool[poolIndex].transformMatrix)
		{
			outPoolIndex = poolIndex;
			return true;
		}
	}

	return false;
}

void VulkanEngine::renderPickedObject(VkCommandBuffer cmd, const FrameData& currentFrame, const std::vector<ModelWithIndirectDrawId>& indirectDrawCommandIds)
{
	//
	// Render it with the wireframe color pipeline
	// @BUG: recompacting and reuploading the picked pool index will overwrite the first mesh drawn's instance ptr information, so making a new system would be great, or not since this is just a debug feature.
	//
	constexpr size_t numRenders = 2;
	std::string materialNames[numRenders] = {
		"wireframeColorMaterial",
		"wireframeColorBehindMaterial"
	};
	vec4 materialColors[numRenders] = {
		{ 1, 0.25, 1, 1 },
		{ 0.535, 0.13, 0.535, 1 },
	};

	for (size_t i = 0; i < numRenders; i++)
	{
		Material& material = *getMaterial(materialNames[i]);

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, material.pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, material.pipelineLayout, 0, 1, &currentFrame.globalDescriptor, 0, nullptr);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, material.pipelineLayout, 1, 1, &currentFrame.objectDescriptor, 0, nullptr);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, material.pipelineLayout, 2, 1, &currentFrame.instancePtrDescriptor, 0, nullptr);

		// Push constants
		ColorPushConstBlock pc = {};
		glm_vec4_copy(materialColors[i], pc.color);
		vkCmdPushConstants(cmd, material.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ColorPushConstBlock), &pc);

		// Render objects
		uint32_t drawStride = sizeof(VkDrawIndexedIndirectCommand);
		for (const ModelWithIndirectDrawId& mwidid : indirectDrawCommandIds)
		{
			VkDeviceSize indirectOffset = mwidid.indirectDrawId * drawStride;
			if (mwidid.model == (vkglTF::Model*)&_roManager->_skinnedMeshModelMemAddr)  // @HACK: getting the right indirect draw command with the combined skinned mesh intermediate buffer!  -Timo 2023/12/16
			{
				// Bind the compute skinned intermediate buffer. @COPYPASTA
				const VkDeviceSize offsets[1] = { 0 };
				vkCmdBindVertexBuffers(cmd, 0, 1, &currentFrame.skinning.outputVerticesBuffer._buffer, offsets);
				vkCmdBindIndexBuffer(cmd, currentFrame.skinning.indicesBuffer._buffer, 0, VK_INDEX_TYPE_UINT32);
			}
			else
				mwidid.model->bind(cmd);
			vkCmdDrawIndexedIndirect(cmd, currentFrame.indirectDrawCommandRawBuffer._buffer, indirectOffset, 1, drawStride);
		}
	}
}

#ifdef _DEVELOP
void VulkanEngine::updateDebugStats(float_t deltaTime)
{
	_debugStats.currentFPS = (uint32_t)std::roundf(1.0f / deltaTime);
	_debugStats.renderTimesMSHeadIndex = (size_t)std::fmodf((float_t)_debugStats.renderTimesMSHeadIndex + 1, (float_t)_debugStats.renderTimesMSCount);

	// Find what the highest render time is
	float_t renderTime = deltaTime * 1000.0f;
	if (renderTime > _debugStats.highestRenderTime)
		_debugStats.highestRenderTime = renderTime;
	else if (_debugStats.renderTimesMS[_debugStats.renderTimesMSHeadIndex] == _debugStats.highestRenderTime)     // Former highest render time about to be overwritten
	{
		float_t nextHighestRenderTime = renderTime;
		for (size_t i = _debugStats.renderTimesMSHeadIndex + 1; i < _debugStats.renderTimesMSHeadIndex + _debugStats.renderTimesMSCount; i++)
				nextHighestRenderTime = std::max(nextHighestRenderTime, _debugStats.renderTimesMS[i]);
		_debugStats.highestRenderTime = nextHighestRenderTime;
	}

	// Apply render time to buffer
	_debugStats.renderTimesMS[_debugStats.renderTimesMSHeadIndex] =
		_debugStats.renderTimesMS[_debugStats.renderTimesMSHeadIndex + _debugStats.renderTimesMSCount] =
		renderTime;
}
#endif

void VulkanEngine::submitSelectedRenderObjectId(int32_t poolIndex)
{
	if (poolIndex < 0)
	{
		// Nullify the matrixToMove pointer
		_movingMatrix.matrixToMove = nullptr;
		std::cout << "[PICKING]" << std::endl
			<< "Selected object nullified" << std::endl;
		return;
	}

	// Set a new matrixToMove
	_movingMatrix.matrixToMove = &_roManager->_renderObjectPool[poolIndex].transformMatrix;
	std::cout << "[PICKING]" << std::endl
		<< "Selected object " << poolIndex << std::endl;
}

size_t INTERNALVULKANENGINEASSIGNEDMATERIAL_umbIdx;
size_t INTERNALVULKANENGINEASSIGNEDMATERIAL_dmpsIdx;

void VulkanEngine::changeEditorMode(EditorModes newEditorMode)
{
	_movingMatrix.matrixToMove = nullptr;

	// Spin down previous editor mode.
	switch (_currentEditorMode)
	{
		case EditorModes::LEVEL_EDITOR:
		{

		} break;

		case EditorModes::MATERIAL_EDITOR:
		{
			std::string path = materialorganizer::getListOfDerivedMaterials()[0];
			INTERNALVULKANENGINEASSIGNEDMATERIAL_umbIdx = materialorganizer::derivedMaterialNameToUMBIdx(path);
			INTERNALVULKANENGINEASSIGNEDMATERIAL_dmpsIdx = materialorganizer::derivedMaterialNameToDMPSIdx(path);
			EDITORTextureViewer::setAssignedMaterial(
				INTERNALVULKANENGINEASSIGNEDMATERIAL_umbIdx,
				INTERNALVULKANENGINEASSIGNEDMATERIAL_dmpsIdx
			);
		} break;
	}

	_currentEditorMode = newEditorMode;

	// Spin up new editor mode.
	switch (_currentEditorMode)
	{
		case EditorModes::LEVEL_EDITOR:
		{
			globalState::isEditingMode = true;
			physengine::requestSetRunPhysicsSimulation(false);
			_camera->requestCameraMode(_camera->_cameraMode_freeCamMode);
			scene::loadScene(globalState::savedActiveScene, true);
		} break;

		case EditorModes::MATERIAL_EDITOR:
		{
			_camera->requestCameraMode(_camera->_cameraMode_orbitSubjectCamMode);
			scene::loadScene("EDITOR_material_editor.hentais", true);
		} break;
	}
}

void VulkanEngine::renderImGuiContent(float_t deltaTime, ImGuiIO& io)
{
	constexpr float_t MAIN_MENU_PADDING = 18.0f;
	static bool showDemoWindows = false;
	static bool showPerfWindow = true;

	bool allowKeyboardShortcuts =
		_camera->getCameraMode() == Camera::_cameraMode_freeCamMode &&
		!_camera->freeCamMode.enabled &&
		!io.WantTextInput;

	// Top menu.
	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("Mode"))
		{
			if (ImGui::MenuItem("Level Editor", "", (_currentEditorMode == EditorModes::LEVEL_EDITOR)) && _currentEditorMode != EditorModes::LEVEL_EDITOR) changeEditorMode(EditorModes::LEVEL_EDITOR);
			if (ImGui::MenuItem("Material Editor", "", (_currentEditorMode == EditorModes::MATERIAL_EDITOR)) && _currentEditorMode != EditorModes::MATERIAL_EDITOR) changeEditorMode(EditorModes::MATERIAL_EDITOR);
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Window"))
		{
			ImGui::MenuItem("Do Culling stuff DEBUG", "", &doCullingStuff);
			ImGui::MenuItem("Performance Window", "", &showPerfWindow);
			ImGui::MenuItem("Demo Windows", "", &showDemoWindows);
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}

	// Demo windows.
	if (showDemoWindows)
	{
		ImGui::ShowDemoWindow();
		ImPlot::ShowDemoWindow();
	}

	// Debug messages.
	debug::renderImguiDebugMessages((float_t)_windowExtent.width, deltaTime);

	if (showPerfWindow)
	{
		// Debug Stats window.
		static float_t debugStatsWindowWidth = 0.0f;
		static float_t debugStatsWindowHeight = 0.0f;
		ImGui::SetNextWindowPos(ImVec2(_windowExtent.width - debugStatsWindowWidth, _windowExtent.height - debugStatsWindowHeight), ImGuiCond_Always);		// @NOTE: the ImGuiCond_Always means that this line will execute always, when set to once, this line will be ignored after the first time it's called
		ImGui::Begin("##Debug Statistics/Performance Window", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);
		{
			ImGui::Text((std::to_string(_debugStats.currentFPS) + " FPS").c_str());
			ImGui::Text(("Frame : " + std::to_string(_frameNumber)).c_str());

			ImGui::Separator();

			ImGui::Text(("Timescale: " + std::to_string(globalState::timescale)).c_str());

			ImGui::Separator();

			ImGui::Text("Render Times");
			ImGui::Text((std::format("{:.2f}", _debugStats.renderTimesMS[_debugStats.renderTimesMSHeadIndex]) + "ms").c_str());
			ImGui::PlotHistogram("##Render Times Histogram", _debugStats.renderTimesMS, (int32_t)_debugStats.renderTimesMSCount, (int32_t)_debugStats.renderTimesMSHeadIndex, "", 0.0f, _debugStats.highestRenderTime, ImVec2(256, 24.0f));
			ImGui::SameLine();
			ImGui::Text(("[0, " + std::format("{:.2f}", _debugStats.highestRenderTime) + "]").c_str());

			ImGui::Separator();

			physengine::renderImguiPerformanceStats();

			debugStatsWindowWidth = ImGui::GetWindowWidth();
			debugStatsWindowHeight = ImGui::GetWindowHeight();
		}
		ImGui::End();
	}

	switch (_currentEditorMode)
	{
		case EditorModes::LEVEL_EDITOR:
		{
			//
			// Scene Properties window (and play mode window).
			//
			static float_t scenePropertiesWindowWidth = 100.0f;
			static float_t scenePropertiesWindowHeight = 100.0f;

			static bool togglePlayEditModeFlag = false;
			if (togglePlayEditModeFlag || input::editorInputSet().togglePlayEditMode.onAction)
			{
				togglePlayEditModeFlag = false;
				globalState::isEditingMode = !globalState::isEditingMode;

				// React to this in and out of editing/play mode!
				static std::string tempSceneName = ".temp_scene_to_return_to_after_play_mode.temphentais";
				if (globalState::isEditingMode)
				{
					scene::loadScene(tempSceneName, true);
					physengine::requestSetRunPhysicsSimulation(false);
					_camera->requestCameraMode(_camera->_cameraMode_freeCamMode);
				}
				else
				{
					scene::saveScene(tempSceneName, _entityManager->_entities);
					{
						// Add character and set it as the main subject.
						// @TODO: move this somewhere else more appropriate.
						if (globalState::listOfSpawnPoints.empty())
						{
							std::cerr << "ERROR: no spawn points to use for spawning player in!" << std::endl;
							HAWSOO_CRASH();
						}
						auto& spd = globalState::listOfSpawnPoints[0];

						DataSerializer ds;
						ds.dumpString("00000000000000000000000000000000");
						ds.dumpString("PLAYER");            // Type.
						ds.dumpVec3(spd.position);          // Starting Position.
						ds.dumpFloat(spd.facingDirection);  // Facing Direction.
						ds.dumpFloat(100.0f);               // Health.
						ds.dumpFloat(0.0f);                 // Num Harvestable Items.
						ds.dumpFloat(0.0f);                 // Num Scannable Items.
						DataSerialized dsd = ds.getSerializedData();
						SimulationCharacter* entity = (SimulationCharacter*)scene::spinupNewObject(":character", &dsd);
						_camera->mainCamMode.setMainCamTargetObject(entity->getMainRenderObject());
					}
					physengine::requestSetRunPhysicsSimulation(true);
					_camera->requestCameraMode(_camera->_cameraMode_mainCamMode);
				}

				debug::pushDebugMessage({
					.message = (globalState::isEditingMode ? "===Stopped PLAY MODE===" : "===Started PLAY MODE==="),
				});
			}

			if (globalState::isEditingMode)
			{
				// Editing Mode properties.
				ImGui::SetNextWindowPos(ImVec2(_windowExtent.width - scenePropertiesWindowWidth, MAIN_MENU_PADDING), ImGuiCond_Always);
				ImGui::Begin("Scene Properties", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
				{
					ImGui::Text(globalState::savedActiveScene.c_str());

					static std::vector<std::string> listOfScenes;
					if (ImGui::Button("Open Scene.."))
					{
						listOfScenes = scene::getListOfScenes();
						ImGui::OpenPopup("open_scene_popup");
					}
					if (ImGui::BeginPopup("open_scene_popup"))
					{
						for (auto& path : listOfScenes)
							if (ImGui::Button(("Open \"" + path + "\"").c_str()))
							{
								_movingMatrix.matrixToMove = nullptr;  // @HACK: just a safeguard in case if the matrixtomove happened to be on an entity that will get deleted in the next line  -Timo 2022/11/05
								scene::loadScene(path, true);
								globalState::savedActiveScene = path;
								ImGui::CloseCurrentPopup();
							}
						ImGui::EndPopup();
					}

					ImGui::SameLine();
					if (ImGui::Button("Save Scene"))
						scene::saveScene(globalState::savedActiveScene, _entityManager->_entities);

					ImGui::SameLine();
					if (ImGui::Button("Save Scene As.."))
						ImGui::OpenPopup("save_scene_as_popup");
					if (ImGui::BeginPopup("save_scene_as_popup"))
					{
						static std::string saveSceneAsFname;
						ImGui::InputText(".hentais", &saveSceneAsFname);
						if (ImGui::Button(("Save As \"" + saveSceneAsFname + ".hentais\"").c_str()))
						{
							scene::saveScene(saveSceneAsFname + ".hentais", _entityManager->_entities);
							globalState::savedActiveScene = saveSceneAsFname + ".hentais";
							ImGui::CloseCurrentPopup();
						}
						ImGui::EndPopup();
					}

					static std::vector<std::string> listOfPrefabs;
					if (ImGui::Button("Open Prefab.."))
					{
						listOfPrefabs = scene::getListOfPrefabs();
						ImGui::OpenPopup("open_prefab_popup");
					}
					if (ImGui::BeginPopup("open_prefab_popup"))
					{
						for (auto& path : listOfPrefabs)
							if (ImGui::Button(("Open \"" + path + "\"").c_str()))
							{
								scene::loadPrefabNonOwned(path);
								ImGui::CloseCurrentPopup();
							}
						ImGui::EndPopup();
					}

					ImGui::Separator();
					ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.355556f, 0.5f, 0.4f));
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0.355556f, 0.7f, 0.5f));
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0.355556f, 0.8f, 0.6f));
					if (ImGui::Button("Start PLAY MODE (F1)"))
						togglePlayEditModeFlag = true;  // Switch to play mode.
					ImGui::PopStyleColor(3);

					scenePropertiesWindowWidth = ImGui::GetWindowWidth();
					scenePropertiesWindowHeight = ImGui::GetWindowHeight();
				}
				ImGui::End();
			}
			else
			{
				// Play Mode desu window.
				ImGui::SetNextWindowPos(ImVec2(_windowExtent.width - scenePropertiesWindowWidth, MAIN_MENU_PADDING), ImGuiCond_Always);
				ImGui::SetNextWindowSize(ImVec2(scenePropertiesWindowWidth, scenePropertiesWindowHeight), ImGuiCond_Always);
				ImGui::Begin("##Play Mode desu window", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
				{
					ImGui::SetWindowFontScale(1.5f);

					ImGui::Text("PLAY MODE is ");
					ImGui::SameLine();
					ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "ON");
					ImGui::SameLine();
					ImGui::Text(" F1 to stop");

					ImGui::Text("Simulation: ");
					ImGui::SameLine();
					ImGui::TextColored(
						physengine::getIsRunPhysicsSimulation() ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
						physengine::getIsRunPhysicsSimulation() ? "ON" : "OFF"
					);
					ImGui::SameLine();
					ImGui::Text(" (Shift+F1)");

					ImGui::Text("Game Camera: ");
					bool isCameraOn = (_camera->getCameraMode() == _camera->_cameraMode_mainCamMode);
					ImGui::SameLine();
					ImGui::TextColored(
						isCameraOn ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
						isCameraOn ? "ON" : "OFF"
					);
					ImGui::SameLine();
					ImGui::Text(" (F2)");
				}
				ImGui::End();
			}

			// Left side props windows.
			ImGui::SetNextWindowPos(ImVec2(0.0f, MAIN_MENU_PADDING), ImGuiCond_Always);
			ImGui::SetNextWindowSizeConstraints(ImVec2(-1.0f, 0.0f), ImVec2(-1.0f, _windowExtent.height - MAIN_MENU_PADDING));
			ImGui::Begin("Level Editor##Left side props windows", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
			{
				// PBR Shading props.
				if (ImGui::CollapsingHeader("PBR Shading Properties", ImGuiTreeNodeFlags_DefaultOpen))
				{
					if (ImGui::DragFloat3("Light Direction", _pbrRendering.gpuSceneShadingProps.lightDir))
					glm_normalize(_pbrRendering.gpuSceneShadingProps.lightDir);		

					ImGui::DragFloat("Exposure", &_pbrRendering.gpuSceneShadingProps.exposure, 0.1f, 0.1f, 10.0f);
					ImGui::DragFloat("Gamma", &_pbrRendering.gpuSceneShadingProps.gamma, 0.1f, 0.1f, 4.0f);
					ImGui::DragFloat("IBL Strength", &_pbrRendering.gpuSceneShadingProps.scaleIBLAmbient, 0.1f, 0.0f, 2.0f);

					ImGui::DragFloat("Shadow Jitter Strength", &_pbrRendering.gpuSceneShadingProps.shadowJitterMapOffsetScale, 0.1f);

					static int debugViewIndex = 0;
					if (ImGui::Combo("Debug View Input", &debugViewIndex, "none\0Base color\0Normal\0Occlusion\0Emissive\0Metallic\0Roughness"))
						_pbrRendering.gpuSceneShadingProps.debugViewInputs = (float_t)debugViewIndex;

					static int debugViewEquation = 0;
					if (ImGui::Combo("Debug View Equation", &debugViewEquation, "none\0Diff (l,n)\0F (l,h)\0G (l,v,h)\0D (h)\0Specular"))
						_pbrRendering.gpuSceneShadingProps.debugViewEquation = (float_t)debugViewEquation;

					ImGui::Text(("Prefiltered Cubemap Miplevels: " + std::to_string((int32_t)_pbrRendering.gpuSceneShadingProps.prefilteredCubemapMipLevels)).c_str());

					ImGui::Separator();

					ImGui::Text("Toggle Layers");

					static const ImVec2 imageButtonSize = ImVec2(64, 64);
					static const ImVec4 tintColorActive = ImVec4(1, 1, 1, 1);
					static const ImVec4 tintColorInactive = ImVec4(1, 1, 1, 0.25);

					//
					// Toggle Layers (Section: Rendering)
					//
					ImTextureID renderingLayersButtonIcons[] = {
						(ImTextureID)_imguiData.textureLayerVisible,
						(ImTextureID)_imguiData.textureLayerInvisible,
						(ImTextureID)_imguiData.textureLayerBuilder,
						(ImTextureID)_imguiData.textureLayerCollision,
					};
					std::string buttonTurnOnSfx[] = {
						"res/sfx/_develop/layer_visible_sfx.ogg",
						"res/sfx/_develop/layer_invisible_sfx.ogg",
						"res/sfx/_develop/layer_builder_sfx.ogg",
						"res/sfx/_develop/layer_collision_sfx.ogg",
					};

					for (size_t i = 0; i < std::size(renderingLayersButtonIcons); i++)
					{
						bool isLayerActive = false;
						switch (i)
						{
						case 0:
						case 1:
						case 2:
							isLayerActive = _roManager->_renderObjectLayersEnabled[i];
							break;

						case 3:
							isLayerActive = generateCollisionDebugVisualization;
							break;
						}

						if (ImGui::ImageButton(renderingLayersButtonIcons[i], imageButtonSize, ImVec2(0, 0), ImVec2(1, 1), -1, ImVec4(0, 0, 0, 0), isLayerActive ? tintColorActive : tintColorInactive))
						{
							switch (i)
							{
							case 0:
							case 1:
							case 2:
							{
								// Toggle render layer
								_roManager->_renderObjectLayersEnabled[i] = !_roManager->_renderObjectLayersEnabled[i];
								_roManager->flagMetaMeshListAsUnoptimized();
								if (!_roManager->_renderObjectLayersEnabled[i])
								{
									// Find object that matrixToMove is pulling from (if any)
									for (size_t poolIndex : _roManager->_renderObjectsIndices)
									{
										auto& ro = _roManager->_renderObjectPool[poolIndex];
										if (_movingMatrix.matrixToMove == &ro.transformMatrix)
										{
											// @HACK: Reset the _movingMatrix.matrixToMove
											//        if it's for one of the objects that just got disabled
											if ((size_t)ro.renderLayer == i)
												_movingMatrix.matrixToMove = nullptr;
											break;
										}
									}
								}
								break;
							}

							case 3:
								// Collision Layer debug draw toggle
								generateCollisionDebugVisualization = !generateCollisionDebugVisualization;
								break;
							}

							// Assume toggle occurred (so if layer wasn't active)
							if (!isLayerActive)
								AudioEngine::getInstance().playSound(buttonTurnOnSfx[i]);
						}

						if ((int32_t)fmodf((float_t)(i + 1), 3.0f) != 0)
							ImGui::SameLine();
					}
				}

				// Physics Props.
				ImGui::Separator();
				if (ImGui::CollapsingHeader("Physics Properties", ImGuiTreeNodeFlags_DefaultOpen))
				{
					static vec3 worldGravity = GLM_VEC3_ZERO_INIT;
					static bool first = true;  // @HACK
					if (first)
					{
						physengine::getWorldGravity(worldGravity);
						first = false;
					}                          // End @HACK
					if (ImGui::DragFloat3("worldGravity", worldGravity))
						physengine::setWorldGravity(worldGravity);
				}

				// Camera props.
				ImGui::Separator();
				if (ImGui::CollapsingHeader("Camera Properties", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::Text("NOTE: press F10 to change camera types");

					ImGui::SliderFloat("lookDistance", &_camera->mainCamMode.lookDistance, 1.0f, 100.0f);
					ImGui::DragFloat("lookDistanceSmoothTime", &_camera->mainCamMode.lookDistanceSmoothTime, 0.01f);
					ImGui::DragFloat("focusSmoothTimeXZ", &_camera->mainCamMode.focusSmoothTimeXZ, 0.01f);
					ImGui::DragFloat("focusSmoothTimeY", &_camera->mainCamMode.focusSmoothTimeY, 0.01f);
					ImGui::DragFloat3("focusPositionOffset", _camera->mainCamMode.focusPositionOffset);
					ImGui::DragFloat("opponentTargetTransition.targetYOrbitAngleSideOffset", &_camera->mainCamMode.opponentTargetTransition.targetYOrbitAngleSideOffset, 0.01f);
					ImGui::DragFloat("opponentTargetTransition.xOrbitAngleSmoothTime", &_camera->mainCamMode.opponentTargetTransition.xOrbitAngleSmoothTime, 0.01f);
					ImGui::DragFloat("opponentTargetTransition.yOrbitAngleSmoothTimeSlow", &_camera->mainCamMode.opponentTargetTransition.yOrbitAngleSmoothTimeSlow, 0.01f);
					ImGui::DragFloat("opponentTargetTransition.yOrbitAngleSmoothTimeFast", &_camera->mainCamMode.opponentTargetTransition.yOrbitAngleSmoothTimeFast, 0.01f);
					ImGui::DragFloat("opponentTargetTransition.slowFastTransitionRadius", &_camera->mainCamMode.opponentTargetTransition.slowFastTransitionRadius, 0.1f);
					ImGui::DragFloat("opponentTargetTransition.lookDistanceBaseAmount", &_camera->mainCamMode.opponentTargetTransition.lookDistanceBaseAmount, 0.1f);
					ImGui::DragFloat("opponentTargetTransition.lookDistanceObliqueAmount", &_camera->mainCamMode.opponentTargetTransition.lookDistanceObliqueAmount, 0.1f);
					ImGui::DragFloat("opponentTargetTransition.lookDistanceHeightAmount", &_camera->mainCamMode.opponentTargetTransition.lookDistanceHeightAmount, 0.1f);
					ImGui::DragFloat("opponentTargetTransition.focusPositionExtraYOffsetWhenTargeting", &_camera->mainCamMode.opponentTargetTransition.focusPositionExtraYOffsetWhenTargeting, 0.1f);
					ImGui::DragFloat("opponentTargetTransition.depthOfFieldSmoothTime", &_camera->mainCamMode.opponentTargetTransition.depthOfFieldSmoothTime, 0.1f);
					ImGui::DragFloat3("opponentTargetTransition.DOFPropsRelaxedState", _camera->mainCamMode.opponentTargetTransition.DOFPropsRelaxedState, 0.1f);
				}

				// DOF props.
				ImGui::Separator();
				if (ImGui::CollapsingHeader("Depth of Field Properties", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::DragFloat("CoC Focus Depth", &globalState::DOFFocusDepth, 0.1f);
					ImGui::DragFloat("CoC Focus Extent", &globalState::DOFFocusExtent, 0.1f);
					ImGui::DragFloat("CoC Blur Extent", &globalState::DOFBlurExtent, 0.1f);
					ImGui::DragFloat("DOF Gather Sample Radius", &_DOFSampleRadiusMultiplier, 0.1f);
				}

				// Textbox props.
				ImGui::Separator();
				if (ImGui::CollapsingHeader("Textbox Properties", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::DragFloat3("mainRenderPosition", textbox::mainRenderPosition);
					ImGui::DragFloat3("mainRenderExtents", textbox::mainRenderExtents);
					ImGui::DragFloat3("querySelectionsRenderPosition", textbox::querySelectionsRenderPosition);
				}

				// Entity creation. @TODO: this needs to be moved out.
				ImGui::Separator();
				if (ImGui::CollapsingHeader("Create Entity", ImGuiTreeNodeFlags_DefaultOpen))
				{
					static int32_t entityToCreateIndex = 1;  // @NOTE: don't want default setting to be `:player` or else you could accidentally create another player entity... and that is not needed for the levels
					std::vector<std::string> listEntityTypes = scene::getListOfEntityTypes();
					std::stringstream allEntityTypes;
					for (auto entType : listEntityTypes)
						allEntityTypes << entType << '\0';

					ImGui::Combo("##Entity to create", &entityToCreateIndex, allEntityTypes.str().c_str());

					static Entity* _flagAttachToThisEntity = nullptr;  // One frame lag fetch bc the `INTERNALaddRemoveRequestedEntities()` gets run once a frame instead of immediate add when an entity gets constructed.
					if (_flagAttachToThisEntity)
					{
						for (size_t poolIndex : _roManager->_renderObjectsIndices)
						{
							auto& ro = _roManager->_renderObjectPool[poolIndex];
							if (ro.attachedEntityGuid == _flagAttachToThisEntity->getGUID())
								_movingMatrix.matrixToMove = &ro.transformMatrix;
						}
						_flagAttachToThisEntity = nullptr;
					}

					if (ImGui::Button("Create!"))
					{
						auto newEnt = scene::spinupNewObject(listEntityTypes[(size_t)entityToCreateIndex], nullptr);
						_flagAttachToThisEntity = newEnt;  // @HACK: ... but if it works?
					}

					// Manipulate the selected entity
					Entity* selectedEntity = nullptr;
					for (size_t poolIndex : _roManager->_renderObjectsIndices)
					{
						auto& ro = _roManager->_renderObjectPool[poolIndex];
						if (_movingMatrix.matrixToMove == &ro.transformMatrix)
							for (auto& ent : _entityManager->_entities)
								if (ro.attachedEntityGuid == ent->getGUID())
								{
									selectedEntity = ent;
									break;
								}
						if (selectedEntity)
							break;
					}
					if (selectedEntity)
					{
						// Duplicate
						static bool canRunDuplicateProc = true;
						if (ImGui::Button("Duplicate Selected Entity") || (allowKeyboardShortcuts && input::editorInputSet().duplicateObject.onAction))
						{
							if (canRunDuplicateProc)
							{
								DataSerializer ds;
								selectedEntity->dump(ds);
								auto dsd = ds.getSerializedData();
								auto newEnt = scene::spinupNewObject(selectedEntity->getTypeName(), &dsd);
								_flagAttachToThisEntity = newEnt;
							}

							canRunDuplicateProc = false;
						}
						else
							canRunDuplicateProc = true;

						// Delete
						static bool canRunDeleteProc = true;
						
						ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.0f, 0.5f, 0.6f));
						ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0.0f, 0.7f, 0.7f));
						ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0.0f, 0.8f, 0.8f));

						if (ImGui::Button("Delete Selected Entity!") || (allowKeyboardShortcuts && input::editorInputSet().deleteObject.onAction))
						{
							if (canRunDeleteProc)
							{
								_entityManager->destroyEntity(selectedEntity);
								_movingMatrix.matrixToMove = nullptr;
							}

							canRunDeleteProc = false;
						}
						else
							canRunDeleteProc = true;

						ImGui::PopStyleColor(3);
					}
				}

				if (_movingMatrix.matrixToMove != nullptr)
				{
					// Move the picked matrix via ImGuizmo.
					mat4 projection;
					glm_mat4_copy(_camera->sceneCamera.gpuCameraData.projection, projection);
					projection[1][1] *= -1.0f;

					static ImGuizmo::OPERATION manipulateOperation = ImGuizmo::OPERATION::TRANSLATE;
					static ImGuizmo::MODE manipulateMode           = ImGuizmo::MODE::WORLD;

					vec3 snapValues(0.0f);
					if (input::editorInputSet().snapModifier.holding)
						if (manipulateOperation == ImGuizmo::OPERATION::ROTATE)
							snapValues[0] = snapValues[1] = snapValues[2] = 45.0f;
						else
							snapValues[0] = snapValues[1] = snapValues[2] = 0.5f;

					bool matrixToMoveMoved =
						ImGuizmo::Manipulate(
							(const float_t*)_camera->sceneCamera.gpuCameraData.view,
							(const float_t*)projection,
							manipulateOperation,
							manipulateMode,
							(float_t*)*_movingMatrix.matrixToMove,
							nullptr,
							(const float_t*)snapValues
						);

					// Edit Selected Entity.
					ImGui::Separator();
					if (ImGui::CollapsingHeader("Edit Selected Entity", ImGuiTreeNodeFlags_DefaultOpen))
					{
						if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
						{
							vec3 position, eulerAngles, scale;
							ImGuizmo::DecomposeMatrixToComponents(
								(const float_t*)* _movingMatrix.matrixToMove,
								position,
								eulerAngles,
								scale
							);

							bool changed = false;
							changed |= ImGui::DragFloat3("Pos##ASDFASDFASDFJAKSDFKASDHF", position);
							changed |= ImGui::DragFloat3("Rot##ASDFASDFASDFJAKSDFKASDHF", eulerAngles);
							changed |= ImGui::DragFloat3("Sca##ASDFASDFASDFJAKSDFKASDHF", scale);

							if (changed)
							{
								// Recompose the matrix
								// @TODO: Figure out when to invalidate the cache bc the euler angles will reset!
								//        Or... maybe invalidating the cache isn't necessary for this window????
								ImGuizmo::RecomposeMatrixFromComponents(
									position,
									eulerAngles,
									scale,
									(float_t*)*_movingMatrix.matrixToMove
								);

								matrixToMoveMoved = true;
							}
						}

						static bool forceRecalculation = false;    // @NOTE: this is a flag for the key bindings below
						static int operationIndex = 0;
						static int modeIndex = 0;
						if (ImGui::CollapsingHeader("Manipulation Gizmo", ImGuiTreeNodeFlags_DefaultOpen))
						{
							if (ImGui::Combo("Operation", &operationIndex, "Translate\0Rotate\0Scale") || forceRecalculation)
							{
								switch (operationIndex)
								{
								case 0:
									manipulateOperation = ImGuizmo::OPERATION::TRANSLATE;
									break;
								case 1:
									manipulateOperation = ImGuizmo::OPERATION::ROTATE;
									break;
								case 2:
									manipulateOperation = ImGuizmo::OPERATION::SCALE;
									break;
								}
							}
							if (ImGui::Combo("Mode", &modeIndex, "World\0Local") || forceRecalculation)
							{
								switch (modeIndex)
								{
								case 0:
									manipulateMode = ImGuizmo::MODE::WORLD;
									break;
								case 1:
									manipulateMode = ImGuizmo::MODE::LOCAL;
									break;
								}
							}
						}

						// Key bindings for switching the operation and mode
						forceRecalculation = false;

						bool hasMouseButtonDown = false;
						for (size_t i = 0; i < 5; i++)
							hasMouseButtonDown |= io.MouseDown[i];    // @NOTE: this covers cases of gizmo operation changing while left clicking on the gizmo (or anywhere else) or flying around with right click.  -Timo
						if (!hasMouseButtonDown && allowKeyboardShortcuts)
						{
							static bool qKeyLock = false;
							if (input::editorInputSet().toggleTransformManipulationMode.onAction)
							{
								if (!qKeyLock)
								{
									modeIndex = (int)!(bool)modeIndex;
									qKeyLock = true;
									forceRecalculation = true;
								}
							}
							else
							{
								qKeyLock = false;
							}

							if (input::editorInputSet().switchToTransformPosition.onAction)
							{
								operationIndex = 0;
								forceRecalculation = true;
							}
							if (input::editorInputSet().switchToTransformRotation.onAction)
							{
								operationIndex = 1;
								forceRecalculation = true;
							}
							if (input::editorInputSet().switchToTransformScale.onAction)
							{
								operationIndex = 2;
								forceRecalculation = true;
							}
						}

						//
						// Edit props exclusive to render objects
						//
						RenderObject* foundRO = nullptr;
						for (size_t poolIndex : _roManager->_renderObjectsIndices)
						{
							auto& ro = _roManager->_renderObjectPool[poolIndex];
							if (_movingMatrix.matrixToMove == &ro.transformMatrix)
							{
								foundRO = &ro;
								break;
							}
						}

						if (foundRO)
						{
							if (ImGui::CollapsingHeader("Render Object", ImGuiTreeNodeFlags_DefaultOpen))
							{
								int32_t temp = (int32_t)foundRO->renderLayer;
								if (ImGui::Combo("Render Layer##asdfasdfasgasgcombo", &temp, "VISIBLE\0INVISIBLE\0BUILDER"))
									foundRO->renderLayer = RenderLayer(temp);
							}

							//
							// @TODO: see if you can't implement one for physics objects
							// @REPLY: I can't. I don't want to. I don't see a point.
							//

							//
							// @NOTE: first see if there is an entity attached to the renderobject via guid
							// Edit props connected to the entity
							//
							Entity* foundEnt = nullptr;
							if (!foundRO->attachedEntityGuid.empty())
							{
								for (auto& ent : _entityManager->_entities)
								{
									if (ent->getGUID() == foundRO->attachedEntityGuid)
									{
										foundEnt = ent;
										break;
									}
								}

								if (foundEnt && ImGui::CollapsingHeader(("Entity " + foundEnt->getGUID()).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
								{
									if (matrixToMoveMoved)
										foundEnt->reportMoved(_movingMatrix.matrixToMove);

									std::string guidCopy = foundEnt->getGUID();
									ImGui::InputText("GUID", &guidCopy);

									foundEnt->renderImGui();
								}
							}
						}
					}
				}
			}
			ImGui::End();
		} break;

		case EditorModes::MATERIAL_EDITOR:
		{
			ImGui::SetNextWindowPos(ImVec2(0.0f, MAIN_MENU_PADDING), ImGuiCond_Always);
			ImGui::SetNextWindowSizeConstraints(ImVec2(-1.0f, 0.0f), ImVec2(-1.0f, _windowExtent.height - MAIN_MENU_PADDING));
			ImGui::Begin(("MATERIAL EDITOR (" + materialorganizer::getMaterialName(INTERNALVULKANENGINEASSIGNEDMATERIAL_dmpsIdx) + ")##Material editor window.").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
			{
				bool disableNormalControls = materialorganizer::isDMPSDirty();
				if (disableNormalControls)
					ImGui::BeginDisabled();

				static std::string newMaterialName;
				if (ImGui::Button("Make copy of current material.."))
				{
					newMaterialName = "New Material.hderriere";
					ImGui::OpenPopup("new_material_popup");
				}
				ImGui::SameLine();

				static std::vector<std::string> listOfMaterials;
				if (ImGui::Button("Edit material.."))
				{
					listOfMaterials = materialorganizer::getListOfDerivedMaterials();
					ImGui::OpenPopup("edit_material_popup");
				}
				if (true)
				{
					ImGui::SameLine();
					ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.0f, 0.5f, 0.6f));
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0.0f, 0.7f, 0.7f));
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0.0f, 0.8f, 0.8f));
					bool doIt = ImGui::Button("Delete material!");
					ImGui::PopStyleColor(3);
					if (doIt)
					{
						ImGui::OpenPopup("delete_material_popup");
					}
				}
				if (disableNormalControls)
					ImGui::EndDisabled();
				
				// Controls only when the material is dirty (i.e. saving or discarding material changes).
				if (!disableNormalControls)
					ImGui::BeginDisabled();
				if (ImGui::Button("Save material changes"))
				{
					materialorganizer::saveDMPSToFile(
						INTERNALVULKANENGINEASSIGNEDMATERIAL_dmpsIdx
					);
				}
				ImGui::SameLine();
				if (ImGui::Button("Discard material changes"))
				{
					_recreateSwapchain = true;
					materialorganizer::clearDMPSDirtyFlag();
				}
				if (!disableNormalControls)
					ImGui::EndDisabled();

				// Popups.
				if (ImGui::BeginPopup("new_material_popup"))
				{
					ImGui::InputText("New Material Name", &newMaterialName);
					if (std::filesystem::exists("res/materials/" + newMaterialName))
						ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "ERROR: filename exists.");

					static bool showDMPSCopyError = false;
					if (ImGui::Button("Create material based off of current material"))
					{
						showDMPSCopyError =
							!materialorganizer::makeDMPSFileCopy(
								INTERNALVULKANENGINEASSIGNEDMATERIAL_dmpsIdx,
								"res/materials/" + newMaterialName
							);
						if (!showDMPSCopyError)
						{
							ImGui::CloseCurrentPopup();
						}
					}
					if (showDMPSCopyError)
						ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "ERROR: copy failed.");

					ImGui::EndPopup();
				}

				if (ImGui::BeginPopup("edit_material_popup"))
				{
					for (auto& path : listOfMaterials)
						if (ImGui::Button(("Open \"" + path + "\"").c_str()))
						{
							INTERNALVULKANENGINEASSIGNEDMATERIAL_umbIdx = materialorganizer::derivedMaterialNameToUMBIdx(path);
							INTERNALVULKANENGINEASSIGNEDMATERIAL_dmpsIdx = materialorganizer::derivedMaterialNameToDMPSIdx(path);
							EDITORTextureViewer::setAssignedMaterial(
								INTERNALVULKANENGINEASSIGNEDMATERIAL_umbIdx,
								INTERNALVULKANENGINEASSIGNEDMATERIAL_dmpsIdx
							);
							ImGui::CloseCurrentPopup();
						}
					ImGui::EndPopup();
				}

				if (ImGui::BeginPopup("delete_material_popup"))
				{
					ImGui::Text("Hi, personal message from Dmitri.... this program doesn't have the authority to delete material. Please navigate to the `res/materials/` folder to delete a material");
					ImGui::EndPopup();
				}

				// Selected material properties.
				ImGui::Separator();

				materialorganizer::renderImGuiForMaterial(
					INTERNALVULKANENGINEASSIGNEDMATERIAL_umbIdx,
					INTERNALVULKANENGINEASSIGNEDMATERIAL_dmpsIdx
				);
			}
			ImGui::End();
		} break;
	}
	
}

void VulkanEngine::renderImGui(float_t deltaTime)
{
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplSDL2_NewFrame(_window);
	ImGui::NewFrame();

	ImGuizmo::SetOrthographic(false);
	ImGuizmo::AllowAxisFlip(false);
	ImGuizmo::BeginFrame();
	ImGuiIO& io = ImGui::GetIO();
	ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);

	static bool showImguiRender = true;
	if (input::editorInputSet().toggleEditorUI.onAction)
		showImguiRender = !showImguiRender;

	if (showImguiRender)
		renderImGuiContent(deltaTime, io);

	ImGui::Render();
}
