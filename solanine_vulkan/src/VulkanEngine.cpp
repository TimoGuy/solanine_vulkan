#include "VulkanEngine.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vma/vk_mem_alloc.h>
#include "VkBootstrap.h"
#include "VkInitializers.h"
#include "VkDescriptorBuilderUtil.h"
#include "VkPipelineBuilderUtil.h"
#include "VkTextures.h"
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
#include "imgui/imgui.h"
#include "imgui/imgui_stdlib.h"
#include "imgui/imgui_impl_sdl.h"
#include "imgui/imgui_impl_vulkan.h"
#include "imgui/implot.h"
#include "imgui/ImGuizmo.h"


constexpr uint64_t TIMEOUT_1_SEC = 1000000000;

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
	hotswapres::buildResourceList();
#endif

	initVulkan();
	initSwapchain();
	initCommands();
	initShadowRenderpass();
	initShadowImages();  // @NOTE: this isn't screen space, so no need to recreate images on swapchain recreation
	initMainRenderpass();
	initUIRenderpass();
	initPostprocessRenderpass();
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

	AudioEngine::getInstance().initialize();
	physengine::start(_entityManager);
	globalState::initGlobalState(_camera->sceneCamera);

	SDL_ShowWindow(_window);

	_isInitialized = true;

	scene::loadScene(globalState::savedActiveScene, this);
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
	std::mutex* hotswapMutex = hotswapres::startResourceChecker(this, &_recreateSwapchain, _roManager);
#endif

	while (isRunning)
	{
		perfs[0] = SDL_GetPerformanceCounter();
		// Poll events from the window
		input::processInput(&isRunning, &_isWindowMinimized);
		perfs[0] = SDL_GetPerformanceCounter() - perfs[0];


		perfs[1] = SDL_GetPerformanceCounter();
		// Update time multiplier
		if (input::onKeyLSBPress || input::onKeyRSBPress)
		{
			globalState::timescale *= input::onKeyLSBPress ? 0.5f : 2.0f;
			debug::pushDebugMessage({
				.message = "Set timescale to " + std::to_string(globalState::timescale),
			});
		}
		perfs[1] = SDL_GetPerformanceCounter() - perfs[1];


		perfs[2] = SDL_GetPerformanceCounter();
		// Update DeltaTime
		uint64_t currentFrame = SDL_GetPerformanceCounter();
		const float_t deltaTime = (float_t)(currentFrame - lastFrame) * ticksFrequency;
		const float_t scaledDeltaTime = deltaTime * globalState::timescale;
		lastFrame = currentFrame;
		perfs[2] = SDL_GetPerformanceCounter() - perfs[2];


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

		// Update entities
		_entityManager->update(scaledDeltaTime);
		perfs[4] = SDL_GetPerformanceCounter() - perfs[4];


		perfs[5] = SDL_GetPerformanceCounter();
		// Update animators
		_roManager->updateAnimators(scaledDeltaTime);
		perfs[5] = SDL_GetPerformanceCounter() - perfs[5];


		perfs[6] = SDL_GetPerformanceCounter();
		// Late update (i.e. after animators are run)
		_entityManager->lateUpdate(scaledDeltaTime);
		perfs[6] = SDL_GetPerformanceCounter() - perfs[6];


		perfs[7] = SDL_GetPerformanceCounter();
		// Update camera
		_camera->update(deltaTime);
		perfs[7] = SDL_GetPerformanceCounter() - perfs[7];


		perfs[8] = SDL_GetPerformanceCounter();
		// Add/Remove requested entities
		_entityManager->INTERNALaddRemoveRequestedEntities();

		// Add/Change/Remove text meshes
		textmesh::INTERNALprocessChangeQueue();
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


		//
		// Calculate performance
		//
		if (input::keyCtrlPressed)
		{
			uint64_t totalPerf = 0;
			for (size_t i = 0; i < numPerfs; i++)
				totalPerf += perfs[i];

			std::cout << "Performance:";
			for (size_t i = 0; i < numPerfs; i++)
				std::cout << "\t" << (perfs[i] * 100 / totalPerf) << "% (" << perfs[i] << ")";
			std::cout << std::endl;
		}
	}
}

void VulkanEngine::cleanup()
{
#ifdef _DEVELOP
	hotswapres::flagStopRunning();
#endif

	if (_isInitialized)
	{
		vkDeviceWaitIdle(_device);

		globalState::cleanupGlobalState();
		physengine::cleanup();
		AudioEngine::getInstance().cleanup();

		delete _entityManager;
		delete _roManager;

		vkglTF::Animator::destroyEmpty(this);

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

		SDL_DestroyWindow(_window);

#ifdef _DEVELOP
		hotswapres::waitForShutdownAndTeardownResourceList();
#endif
	}
}

void VulkanEngine::render()
{
	const auto& currentFrame = getCurrentFrame();
	VkResult result;

	// Wait until GPU finishes rendering the previous frame
	result = vkWaitForFences(_device, 1, &currentFrame.renderFence, true, TIMEOUT_1_SEC);
	if (result == VK_ERROR_DEVICE_LOST)
		return;

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
	perfs[14] = SDL_GetPerformanceCounter();
	recreateVoxelLightingDescriptor();
	uploadCurrentFrameToGPU(currentFrame);
	textmesh::uploadUICameraDataToGPU();
#ifdef _DEVELOP
	std::vector<size_t> pickedPoolIndices = { 0 };
	if (!searchForPickedObjectPoolIndex(pickedPoolIndices[0]))
		pickedPoolIndices.clear();
	std::vector<ModelWithIndirectDrawId> pickingIndirectDrawCommandIds;
#endif
	compactRenderObjectsIntoDraws(currentFrame, pickedPoolIndices, pickingIndirectDrawCommandIds);
	perfs[14] = SDL_GetPerformanceCounter() - perfs[14];

	//
	// Shadow Render Pass
	//
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

		Material& shadowDepthPassMaterial = *getMaterial("shadowDepthPassMaterial");
		for (uint32_t i = 0; i < SHADOWMAP_CASCADES; i++)
		{
			renderpassInfo.framebuffer = _shadowCascades[i].framebuffer;
			vkCmdBeginRenderPass(cmd, &renderpassInfo, VK_SUBPASS_CONTENTS_INLINE);

			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowDepthPassMaterial.pipeline);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowDepthPassMaterial.pipelineLayout, 0, 1, &currentFrame.cascadeViewProjsDescriptor, 0, nullptr);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowDepthPassMaterial.pipelineLayout, 1, 1, &currentFrame.objectDescriptor, 0, nullptr);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowDepthPassMaterial.pipelineLayout, 2, 1, &currentFrame.instancePtrDescriptor, 0, nullptr);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowDepthPassMaterial.pipelineLayout, 3, 1, &getMaterial("pbrMaterial")->textureSet, 0, nullptr);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowDepthPassMaterial.pipelineLayout, 4, 1, vkglTF::Animator::getGlobalAnimatorNodeCollectionDescriptorSet(), 0, nullptr);

			CascadeIndexPushConstBlock pc = { i };
			vkCmdPushConstants(cmd, shadowDepthPassMaterial.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(CascadeIndexPushConstBlock), &pc);

			renderRenderObjects(cmd, currentFrame);
			
			vkCmdEndRenderPass(cmd);
		}
	}

	//
	// Main Render Pass
	//
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

		Material& defaultMaterial = *getMaterial("pbrMaterial");    // @HACK: @TODO: currently, the way that the pipeline is getting used is by just hardcode using it in the draw commands for models... however, each model should get its pipeline set to this material instead (or whatever material its using... that's why we can't hardcode stuff!!!)   @TODO: create some kind of way to propagate the newly created pipeline to the primMat (calculated material in the gltf model) instead of using defaultMaterial directly.  -Timo
		Material& defaultZPrepassMaterial = *getMaterial("pbrZPrepassMaterial");
		Material& skyboxMaterial = *getMaterial("skyboxMaterial");

		// Render z prepass //
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, defaultZPrepassMaterial.pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, defaultZPrepassMaterial.pipelineLayout, 0, 1, &currentFrame.globalDescriptor, 0, nullptr);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, defaultZPrepassMaterial.pipelineLayout, 1, 1, &currentFrame.objectDescriptor, 0, nullptr);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, defaultZPrepassMaterial.pipelineLayout, 2, 1, &currentFrame.instancePtrDescriptor, 0, nullptr);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, defaultZPrepassMaterial.pipelineLayout, 3, 1, &defaultMaterial.textureSet, 0, nullptr);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, defaultZPrepassMaterial.pipelineLayout, 4, 1, vkglTF::Animator::getGlobalAnimatorNodeCollectionDescriptorSet(), 0, nullptr);
		renderRenderObjects(cmd, currentFrame);
		//////////////////////

		// Switch from zprepass subpass to main subpass
		vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_INLINE);

		// Render skybox //
		// @TODO: put this into its own function!
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxMaterial.pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxMaterial.pipelineLayout, 0, 1, &currentFrame.globalDescriptor, 0, nullptr);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxMaterial.pipelineLayout, 1, 1, &skyboxMaterial.textureSet, 0, nullptr);

		auto skybox = _roManager->getModel("Box", nullptr, [](){});
		skybox->bind(cmd);
		skybox->draw(cmd);
		///////////////////

		// Bind material
		// @TODO: put this into its own function!
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, defaultMaterial.pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, defaultMaterial.pipelineLayout, 0, 1, &currentFrame.globalDescriptor, 0, nullptr);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, defaultMaterial.pipelineLayout, 1, 1, &currentFrame.objectDescriptor, 0, nullptr);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, defaultMaterial.pipelineLayout, 2, 1, &currentFrame.instancePtrDescriptor, 0, nullptr);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, defaultMaterial.pipelineLayout, 3, 1, &defaultMaterial.textureSet, 0, nullptr);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, defaultMaterial.pipelineLayout, 4, 1, vkglTF::Animator::getGlobalAnimatorNodeCollectionDescriptorSet(), 0, nullptr);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, defaultMaterial.pipelineLayout, 5, 1, &_voxelFieldLightingGridTextureSet.descriptor, 0, nullptr);
		////////////////

		renderRenderObjects(cmd, currentFrame);
		if (!pickingIndirectDrawCommandIds.empty())
			renderPickedObject(cmd, currentFrame, pickingIndirectDrawCommandIds);
		physengine::renderDebugVisualization(cmd);

		// End renderpass
		vkCmdEndRenderPass(cmd);
	}

	//
	// UI Render Pass
	//
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

	//
	// Postprocess Render Pass
	//
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
				.image = _bloomPostprocessImage.image._image,
				.subresourceRange = {
					.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel   = 0,
					.levelCount     = static_cast<uint32_t>(_bloomPostprocessImage.image._mipLevels),
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
					(int32_t)_bloomPostprocessImageExtent.width,
					(int32_t)_bloomPostprocessImageExtent.height,
					1
				},
			},
		};
		vkCmdBlitImage(cmd,
			_mainImage.image._image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,  // @NOTE: the renderpass for the _mainImage makes it turn into VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL at the end, so a manual change to SHADER_READ_ONLY_OPTIMAL is necessary
			_bloomPostprocessImage.image._image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &blitRegion,
			VK_FILTER_LINEAR
		);

		// Change mainRenderPass image to shader optimal image layout
		// Change mip0 of bloom image to src transfer layout
		{
			VkImageMemoryBarrier imageBarrier = {
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.srcAccessMask = 0,
				.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				.image = _mainImage.image._image,
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

		// Blit out all remaining mip levels of the chain
		VkImageMemoryBarrier imageBarrier = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.image = _bloomPostprocessImage.image._image,
			.subresourceRange = {
				.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel   = 0,
				.levelCount     = 1,
				.baseArrayLayer = 0,
				.layerCount     = 1,
			},
		};

		int32_t mipWidth = _bloomPostprocessImageExtent.width;
		int32_t mipHeight = _bloomPostprocessImageExtent.height;

		for (uint32_t mipLevel = 1; mipLevel < _bloomPostprocessImage.image._mipLevels; mipLevel++)
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
				_bloomPostprocessImage.image._image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				_bloomPostprocessImage.image._image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
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
		imageBarrier.subresourceRange.baseMipLevel = _bloomPostprocessImage.image._mipLevels - 1;
		imageBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		imageBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
			0, nullptr,
			0, nullptr,
			1, &imageBarrier
		);

		//
		// Apply all postprocessing
		//
		VkClearValue clearValue;
		clearValue.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

		VkRenderPassBeginInfo renderpassInfo = {
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.pNext = nullptr,

			.renderPass = _postprocessRenderPass,
			.framebuffer = _swapchainFramebuffers[swapchainImageIndex],		// @NOTE: Framebuffer of the index the swapchain gave
			.renderArea = {
				.offset = VkOffset2D{ 0, 0 },
				.extent = _windowExtent,
			},

			.clearValueCount = 1,
			.pClearValues = &clearValue,
		};

		// Begin renderpass
		vkCmdBeginRenderPass(cmd, &renderpassInfo, VK_SUBPASS_CONTENTS_INLINE);

		Material& postprocessMaterial = *getMaterial("postprocessMaterial");
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, postprocessMaterial.pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, postprocessMaterial.pipelineLayout, 0, 1, &currentFrame.globalDescriptor, 0, nullptr);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, postprocessMaterial.pipelineLayout, 1, 1, &postprocessMaterial.textureSet, 0, nullptr);
		vkCmdDraw(cmd, 3, 1, 0, 0);

		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

		// End renderpass
		vkCmdEndRenderPass(cmd);
	}

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

	result = vkQueueSubmit(_graphicsQueue, 1, &submit, currentFrame.renderFence);		// Submit work to gpu

	if (result == VK_ERROR_DEVICE_LOST)
		return;

	//
	// Picking Render Pass (OPTIONAL AND SEPARATE)
	//
	if (input::onLMBPress &&
		_camera->getCameraMode() == Camera::_cameraMode_freeCamMode &&
		!_camera->freeCamMode.enabled &&
		!ImGui::GetIO().WantCaptureMouse &&
		(_movingMatrix.matrixToMove != nullptr ?
			!ImGuizmo::IsUsing() && !ImGuizmo::IsOver() :
			true) &&
		ImGui::IsMousePosValid())
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
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pickingMaterial.pipelineLayout, 4, 1, vkglTF::Animator::getGlobalAnimatorNodeCollectionDescriptorSet(), 0, nullptr);

		// Set dynamic scissor
		VkRect2D scissor = {};
		scissor.offset.x = (int32_t)ImGui::GetIO().MousePos.x;
		scissor.offset.y = (int32_t)ImGui::GetIO().MousePos.y;
		scissor.extent = { 1, 1 };
		vkCmdSetScissor(cmd, 0, 1, &scissor);    // @NOTE: the scissor is set to be dynamic state for this pipeline

		std::cout << "[PICKING]" << std::endl
			<< "set picking scissor to: x=" << scissor.offset.x << "  y=" << scissor.offset.y << "  w=" << scissor.extent.width << "  h=" << scissor.extent.height << std::endl;

		renderRenderObjects(cmd, currentFrame);

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

		result = vkQueueSubmit(_graphicsQueue, 1, &submit, currentFrame.pickingRenderFence);		// Submit work to gpu
		if (result == VK_ERROR_DEVICE_LOST)
			return;

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
	// Load empty
	{
		Texture empty;
		vkutil::loadImageFromFile(*this, "res/textures/empty.png", VK_FORMAT_R8G8B8A8_UNORM, 1, empty.image);

		VkImageViewCreateInfo imageInfo = vkinit::imageviewCreateInfo(VK_FORMAT_R8G8B8A8_UNORM, empty.image._image, VK_IMAGE_ASPECT_COLOR_BIT, empty.image._mipLevels);
		vkCreateImageView(_device, &imageInfo, nullptr, &empty.imageView);

		VkSamplerCreateInfo samplerInfo = vkinit::samplerCreateInfo(static_cast<float_t>(empty.image._mipLevels), VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);
		vkCreateSampler(_device, &samplerInfo, nullptr, &empty.sampler);

		_mainDeletionQueue.pushFunction([=]() {
			vkDestroySampler(_device, empty.sampler, nullptr);
			vkDestroyImageView(_device, empty.imageView, nullptr);
		});

		_loadedTextures["empty"] = empty;
	}
	{
		Texture empty;
		vkutil::loadImage3DFromFile(*this, { "res/textures/empty.png" }, VK_FORMAT_R8G8B8A8_UNORM, empty.image);

		VkImageViewCreateInfo imageInfo = vkinit::imageview3DCreateInfo(VK_FORMAT_R8G8B8A8_UNORM, empty.image._image, VK_IMAGE_ASPECT_COLOR_BIT, empty.image._mipLevels);
		vkCreateImageView(_device, &imageInfo, nullptr, &empty.imageView);

		VkSamplerCreateInfo samplerInfo = vkinit::samplerCreateInfo(static_cast<float_t>(empty.image._mipLevels), VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);
		vkCreateSampler(_device, &samplerInfo, nullptr, &empty.sampler);

		_mainDeletionQueue.pushFunction([=]() {
			vkDestroySampler(_device, empty.sampler, nullptr);
			vkDestroyImageView(_device, empty.imageView, nullptr);
		});

		_loadedTextures["empty3d"] = empty;
	}

	// Load woodFloor057
	{
		Texture woodFloor057;
		vkutil::loadImageFromFile(*this, "res/textures/WoodFloor057_1K-JPG/WoodFloor057_1K_Color.jpg", VK_FORMAT_R8G8B8A8_SRGB, 0, woodFloor057.image);

		VkImageViewCreateInfo imageInfo = vkinit::imageviewCreateInfo(VK_FORMAT_R8G8B8A8_SRGB, woodFloor057.image._image, VK_IMAGE_ASPECT_COLOR_BIT, woodFloor057.image._mipLevels);
		vkCreateImageView(_device, &imageInfo, nullptr, &woodFloor057.imageView);

		VkSamplerCreateInfo samplerInfo = vkinit::samplerCreateInfo(static_cast<float_t>(woodFloor057.image._mipLevels), VK_FILTER_LINEAR);
		vkCreateSampler(_device, &samplerInfo, nullptr, &woodFloor057.sampler);

		_mainDeletionQueue.pushFunction([=]() {
			vkDestroySampler(_device, woodFloor057.sampler, nullptr);
			vkDestroyImageView(_device, woodFloor057.imageView, nullptr);
			});

		_loadedTextures["WoodFloor057"] = woodFloor057;
	}

	// Load imguiTextureLayerVisible
	{
		Texture textureLayerVisible;
		vkutil::loadImageFromFile(*this, "res/_develop/icon_layer_visible.png", VK_FORMAT_R8G8B8A8_SRGB, 0, textureLayerVisible.image);

		VkImageViewCreateInfo imageInfo = vkinit::imageviewCreateInfo(VK_FORMAT_R8G8B8A8_SRGB, textureLayerVisible.image._image, VK_IMAGE_ASPECT_COLOR_BIT, textureLayerVisible.image._mipLevels);
		vkCreateImageView(_device, &imageInfo, nullptr, &textureLayerVisible.imageView);

		VkSamplerCreateInfo samplerInfo = vkinit::samplerCreateInfo(static_cast<float_t>(textureLayerVisible.image._mipLevels), VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);
		vkCreateSampler(_device, &samplerInfo, nullptr, &textureLayerVisible.sampler);

		_mainDeletionQueue.pushFunction([=]() {
			vkDestroySampler(_device, textureLayerVisible.sampler, nullptr);
			vkDestroyImageView(_device, textureLayerVisible.imageView, nullptr);
			});

		_loadedTextures["imguiTextureLayerVisible"] = textureLayerVisible;
	}

	// Load imguiTextureLayerInvisible
	{
		Texture textureLayerInvisible;
		vkutil::loadImageFromFile(*this, "res/_develop/icon_layer_invisible.png", VK_FORMAT_R8G8B8A8_SRGB, 0, textureLayerInvisible.image);

		VkImageViewCreateInfo imageInfo = vkinit::imageviewCreateInfo(VK_FORMAT_R8G8B8A8_SRGB, textureLayerInvisible.image._image, VK_IMAGE_ASPECT_COLOR_BIT, textureLayerInvisible.image._mipLevels);
		vkCreateImageView(_device, &imageInfo, nullptr, &textureLayerInvisible.imageView);

		VkSamplerCreateInfo samplerInfo = vkinit::samplerCreateInfo(static_cast<float_t>(textureLayerInvisible.image._mipLevels), VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);
		vkCreateSampler(_device, &samplerInfo, nullptr, &textureLayerInvisible.sampler);

		_mainDeletionQueue.pushFunction([=]() {
			vkDestroySampler(_device, textureLayerInvisible.sampler, nullptr);
			vkDestroyImageView(_device, textureLayerInvisible.imageView, nullptr);
			});

		_loadedTextures["imguiTextureLayerInvisible"] = textureLayerInvisible;
	}

	// Load imguiTextureLayerBuilder
	{
		Texture textureLayerBuilder;
		vkutil::loadImageFromFile(*this, "res/_develop/icon_layer_builder.png", VK_FORMAT_R8G8B8A8_SRGB, 0, textureLayerBuilder.image);

		VkImageViewCreateInfo imageInfo = vkinit::imageviewCreateInfo(VK_FORMAT_R8G8B8A8_SRGB, textureLayerBuilder.image._image, VK_IMAGE_ASPECT_COLOR_BIT, textureLayerBuilder.image._mipLevels);
		vkCreateImageView(_device, &imageInfo, nullptr, &textureLayerBuilder.imageView);

		VkSamplerCreateInfo samplerInfo = vkinit::samplerCreateInfo(static_cast<float_t>(textureLayerBuilder.image._mipLevels), VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);
		vkCreateSampler(_device, &samplerInfo, nullptr, &textureLayerBuilder.sampler);

		_mainDeletionQueue.pushFunction([=]() {
			vkDestroySampler(_device, textureLayerBuilder.sampler, nullptr);
			vkDestroyImageView(_device, textureLayerBuilder.imageView, nullptr);
			});

		_loadedTextures["imguiTextureLayerBuilder"] = textureLayerBuilder;
	}

	// Load imguiTextureLayerCollision  @NOTE: this is a special case. It's not a render layer but rather a toggle to see the debug shapes rendered
	{
		Texture textureLayerCollision;
		vkutil::loadImageFromFile(*this, "res/_develop/icon_layer_collision.png", VK_FORMAT_R8G8B8A8_SRGB, 0, textureLayerCollision.image);

		VkImageViewCreateInfo imageInfo = vkinit::imageviewCreateInfo(VK_FORMAT_R8G8B8A8_SRGB, textureLayerCollision.image._image, VK_IMAGE_ASPECT_COLOR_BIT, textureLayerCollision.image._mipLevels);
		vkCreateImageView(_device, &imageInfo, nullptr, &textureLayerCollision.imageView);

		VkSamplerCreateInfo samplerInfo = vkinit::samplerCreateInfo(static_cast<float_t>(textureLayerCollision.image._mipLevels), VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);
		vkCreateSampler(_device, &samplerInfo, nullptr, &textureLayerCollision.sampler);

		_mainDeletionQueue.pushFunction([=]() {
			vkDestroySampler(_device, textureLayerCollision.sampler, nullptr);
			vkDestroyImageView(_device, textureLayerCollision.imageView, nullptr);
			});

		_loadedTextures["imguiTextureLayerCollision"] = textureLayerCollision;
	}

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
			vmaDestroyImage(_allocator, _pbrSceneTextureSet.shadowJitterMap.image._image, _pbrSceneTextureSet.shadowJitterMap.image._allocation);
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
		.require_api_version(1, 2, 0)
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
		.set_minimum_version(1, 2)
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
	//
	vkb::DeviceBuilder deviceBuilder{ physicalDevice };
	VkPhysicalDeviceShaderDrawParametersFeatures shaderDrawParametersFeatures = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES,
		.pNext = nullptr,
		.shaderDrawParameters = VK_TRUE,
	};
	vkb::Device vkbDevice = deviceBuilder.add_pNext(&shaderDrawParametersFeatures).build().value();

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
	VkImageCreateInfo depthImgInfo = vkinit::imageCreateInfo(_depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthImgExtent, 1);
	VmaAllocationCreateInfo depthImgAllocInfo = {
		.usage = VMA_MEMORY_USAGE_GPU_ONLY,
		.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
	};
	vmaCreateImage(_allocator, &depthImgInfo, &depthImgAllocInfo, &_depthImage._image, &_depthImage._allocation, nullptr);

	VkImageViewCreateInfo depthViewInfo = vkinit::imageviewCreateInfo(_depthFormat, _depthImage._image, VK_IMAGE_ASPECT_DEPTH_BIT, 1);
	VK_CHECK(vkCreateImageView(_device, &depthViewInfo, nullptr, &_depthImageView));

	// Add destroy command
	_swapchainDependentDeletionQueue.pushFunction([=]() {
		vkDestroyImageView(_device, _depthImageView, nullptr);
		vmaDestroyImage(_allocator, _depthImage._image, _depthImage._allocation);
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
		_frames[i].indirectDrawCommandBuffer = createBuffer(sizeof(VkDrawIndexedIndirectCommand) * INSTANCE_PTR_MAX_CAPACITY, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

		// Add destroy command for cleanup
		_mainDeletionQueue.pushFunction([=]() {
			vkDestroyCommandPool(_device, _frames[i].commandPool, nullptr);
			vmaDestroyBuffer(_allocator, _frames[i].indirectDrawCommandBuffer._buffer, _frames[i].indirectDrawCommandBuffer._allocation);
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

		VkFramebufferCreateInfo framebufferInfo = {
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.pNext = nullptr,

			.renderPass = _shadowRenderPass,
			.attachmentCount = 1,
			.pAttachments = &_shadowCascades[i].imageView,
			.width = SHADOWMAP_DIMENSION,
			.height = SHADOWMAP_DIMENSION,
			.layers = 1,
		};
		VK_CHECK(vkCreateFramebuffer(_device, &framebufferInfo, nullptr, &_shadowCascades[i].framebuffer));

		_mainDeletionQueue.pushFunction([=]() {
			vkDestroyFramebuffer(_device, _shadowCascades[i].framebuffer, nullptr);
			vkDestroyImageView(_device, _shadowCascades[i].imageView, nullptr);
			});
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
		.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
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

	VkSamplerCreateInfo samplerInfo = vkinit::samplerCreateInfo(1.0f, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);
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

void VulkanEngine::initPostprocessRenderpass()    // @NOTE: @COPYPASTA: This is really copypasta of the above function (initMainRenderpass)
{
	//
	// Color Attachment  (@NOTE: based off the swapchain images)
	//
	VkAttachmentDescription colorAttachment = {
		.format = _swapchainImageFormat,
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
	// Define the subpass to render to the default renderpass
	//
	VkSubpassDescription subpass = {
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
	VkRenderPassCreateInfo renderPassInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &colorAttachment,
		.subpassCount = 1,
		.pSubpasses = &subpass,
		.dependencyCount = 1,
		.pDependencies = &colorDependency
	};

	VK_CHECK(vkCreateRenderPass(_device, &renderPassInfo, nullptr, &_postprocessRenderPass));

	// Add destroy command for cleanup
	_swapchainDependentDeletionQueue.pushFunction([=]() {
		vkDestroyRenderPass(_device, _postprocessRenderPass, nullptr);
		});

	//
	// Create bloom image
	//
	uint32_t numBloomMips = 5;
	uint32_t startingBloomBufferHeight = _windowExtent.height / 2;
	_bloomPostprocessImageExtent = {
		.width = (uint32_t)((float_t)startingBloomBufferHeight * (float_t)_windowExtent.width / (float_t)_windowExtent.height),
		.height = startingBloomBufferHeight,
	};
	VkExtent3D bloomImgExtent = { _bloomPostprocessImageExtent.width, _bloomPostprocessImageExtent.height, 1 };
	VkImageCreateInfo bloomImgInfo = vkinit::imageCreateInfo(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, bloomImgExtent, numBloomMips);
	VmaAllocationCreateInfo bloomImgAllocInfo = {
		.usage = VMA_MEMORY_USAGE_GPU_ONLY,
	};
	vmaCreateImage(_allocator, &bloomImgInfo, &bloomImgAllocInfo, &_bloomPostprocessImage.image._image, &_bloomPostprocessImage.image._allocation, nullptr);
	_bloomPostprocessImage.image._mipLevels = numBloomMips;

	VkImageViewCreateInfo bloomImgViewInfo = vkinit::imageviewCreateInfo(VK_FORMAT_R16G16B16A16_SFLOAT, _bloomPostprocessImage.image._image, VK_IMAGE_ASPECT_COLOR_BIT, numBloomMips);
	VK_CHECK(vkCreateImageView(_device, &bloomImgViewInfo, nullptr, &_bloomPostprocessImage.imageView));

	VkSamplerCreateInfo samplerInfo = vkinit::samplerCreateInfo((float_t)numBloomMips, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);
	VK_CHECK(vkCreateSampler(_device, &samplerInfo, nullptr, &_bloomPostprocessImage.sampler));

	//
	// Postprocessing
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
	VkDescriptorSet postprocessingTextureSet;
	vkutil::DescriptorBuilder::begin()
		.bindImage(0, &mainHDRImageInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
		.bindImage(1, &uiImageInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
		.bindImage(2, &bloomImageInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
		.build(postprocessingTextureSet, _postprocessSetLayout);
	attachTextureSetToMaterial(postprocessingTextureSet, "postprocessMaterial");

	_swapchainDependentDeletionQueue.pushFunction([=]() {

		vkDestroySampler(_device, _bloomPostprocessImage.sampler, nullptr);
		vkDestroyImageView(_device, _bloomPostprocessImage.imageView, nullptr);
		vmaDestroyImage(_allocator, _bloomPostprocessImage.image._image, _bloomPostprocessImage.image._allocation);
		});
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
	//
	// Create framebuffers for postprocess renderpass
	// @NOTE: this one writes straight to the khr framebuffers
	//
	VkFramebufferCreateInfo fbInfo = {};
	fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fbInfo.pNext = nullptr;

	fbInfo.renderPass = _postprocessRenderPass;
	fbInfo.attachmentCount = 1;
	fbInfo.width = _windowExtent.width;
	fbInfo.height = _windowExtent.height;
	fbInfo.layers = 1;

	const uint32_t swapchainImagecount = static_cast<uint32_t>(_swapchainImages.size());
	_swapchainFramebuffers = std::vector<VkFramebuffer>(swapchainImagecount);

	for (size_t i = 0; i < (size_t)swapchainImagecount; i++)
	{
		VkImageView attachments[] = {
			_swapchainImageViews[i],
		};
		fbInfo.pAttachments = &attachments[0];

		VK_CHECK(vkCreateFramebuffer(_device, &fbInfo, nullptr, &_swapchainFramebuffers[i]));

		// Add destroy command for cleanup
		_swapchainDependentDeletionQueue.pushFunction([=]() {
			vkDestroyFramebuffer(_device, _swapchainFramebuffers[i], nullptr);
			vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
			});
	}

	//
	// Create framebuffer for main renderpass
	//
	{
		VkImageView attachments[] = {
			_mainImage.imageView,
			_depthImageView,
		};
		fbInfo.renderPass = _mainRenderPass;
		fbInfo.attachmentCount = 2;
		fbInfo.pAttachments = &attachments[0];

		VK_CHECK(vkCreateFramebuffer(_device, &fbInfo, nullptr, &_mainFramebuffer));

		_swapchainDependentDeletionQueue.pushFunction([=]() {
			vkDestroyFramebuffer(_device, _mainFramebuffer, nullptr);
			});
	}

	//
	// Create framebuffer for ui renderpass
	//
	{
		VkImageView attachments[] = {
			_uiImage.imageView,
		};
		fbInfo.renderPass = _uiRenderPass;
		fbInfo.attachmentCount = 1;
		fbInfo.pAttachments = &attachments[0];

		VK_CHECK(vkCreateFramebuffer(_device, &fbInfo, nullptr, &_uiFramebuffer));

		_swapchainDependentDeletionQueue.pushFunction([=]() {
			vkDestroyFramebuffer(_device, _uiFramebuffer, nullptr);
			});
	}

	//
	// Create framebuffer for the picking renderpass
	//
	{
		VkImageView attachments[] = {
			_pickingImageView,
			_pickingDepthImageView,
		};
		fbInfo.renderPass = _pickingRenderPass;
		fbInfo.attachmentCount = 2;
		fbInfo.pAttachments = &attachments[0];

		VK_CHECK(vkCreateFramebuffer(_device, &fbInfo, nullptr, &_pickingFramebuffer));

		_swapchainDependentDeletionQueue.pushFunction([=]() {
			vkDestroyFramebuffer(_device, _pickingFramebuffer, nullptr);
			});
	}
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

void VulkanEngine::initDescriptors()    // @TODO: don't destroy and then recreate descriptors when recreating the swapchain. Only pipelines (not even pipelinelayouts), framebuffers, and the corresponding image/imageviews/samplers need to get recreated.  -Timo
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
			.bindBuffer(0, &objectBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
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
			.bindBuffer(0, &instancePtrBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
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
	// All PBR Textures
	//
	AllocatedBuffer materialParamsBuffer = createBuffer(sizeof(PBRMaterialParam) * MAX_NUM_MATERIALS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

	VkDescriptorImageInfo* textureImageInfos = new VkDescriptorImageInfo[MAX_NUM_MAPS];  // @TODO: make this an expandable array.
	for (size_t i = 0; i < MAX_NUM_MAPS; i++)
		textureImageInfos[i] =
			(i < vkglTF::Model::pbrTextureCollection.textures.size()) ?
			vkinit::textureToDescriptorImageInfo(vkglTF::Model::pbrTextureCollection.textures[i]) :
			vkinit::textureToDescriptorImageInfo(vkglTF::Model::pbrTextureCollection.textures[0]);

	VkDescriptorBufferInfo materialParamsBufferInfo = {
		.buffer = materialParamsBuffer._buffer,
		.offset = 0,
		.range = sizeof(PBRMaterialParam) * MAX_NUM_MATERIALS,
	};

	VkDescriptorSet allPBRTexturesDescriptorSet;
	vkutil::DescriptorBuilder::begin()
		.bindImageArray(0, MAX_NUM_MAPS, textureImageInfos, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
		.bindBuffer(1, &materialParamsBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
		.build(allPBRTexturesDescriptorSet, _pbrTexturesSetLayout);
	attachTextureSetToMaterial(allPBRTexturesDescriptorSet, "pbrMaterial");

	//
	// Copy over material information
	//
	void* materialParamsData;
	vmaMapMemory(_allocator, materialParamsBuffer._allocation, &materialParamsData);
	PBRMaterialParam* materialParamsSSBO = (PBRMaterialParam*)materialParamsData;
	for (size_t i = 0; i < MAX_NUM_MATERIALS; i++)
	{
		size_t index = i;
		if (index >= vkglTF::Model::pbrMaterialCollection.materials.size())
			index = 0;

		vkglTF::PBRMaterial* mat = vkglTF::Model::pbrMaterialCollection.materials[index];

		PBRMaterialParam p = {
			.colorMapIndex              = mat->texturePtr.colorMapIndex,
			.physicalDescriptorMapIndex = mat->texturePtr.physicalDescriptorMapIndex,
			.normalMapIndex             = mat->texturePtr.normalMapIndex,
			.aoMapIndex                 = mat->texturePtr.aoMapIndex,
			.emissiveMapIndex           = mat->texturePtr.emissiveMapIndex,
		};

		glm_vec4_copy(mat->emissiveFactor, p.emissiveFactor);
		// To save space, availabilty and texture coordinates set are combined
		// -1 = texture not used for this material, >= 0 texture used and index of texture coordinate set
		p.colorTextureSet = mat->baseColorTexture != nullptr ? mat->texCoordSets.baseColor : -1;
		p.normalTextureSet = mat->normalTexture != nullptr ? mat->texCoordSets.normal : -1;
		p.occlusionTextureSet = mat->occlusionTexture != nullptr ? mat->texCoordSets.occlusion : -1;
		p.emissiveTextureSet = mat->emissiveTexture != nullptr ? mat->texCoordSets.emissive : -1;
		p.alphaMask = static_cast<float>(mat->alphaMode == vkglTF::PBRMaterial::ALPHAMODE_MASK);
		p.alphaMaskCutoff = mat->alphaCutoff;

		// TODO: glTF specs states that metallic roughness should be preferred, even if specular glossiness is present

		if (mat->pbrWorkflows.metallicRoughness)
		{
			// Metallic roughness workflow
			p.workflow = static_cast<float>(PBR_WORKFLOW_METALLIC_ROUGHNESS);
			glm_vec4_copy(mat->baseColorFactor, p.baseColorFactor);
			p.metallicFactor = mat->metallicFactor;
			p.roughnessFactor = mat->roughnessFactor;
			p.PhysicalDescriptorTextureSet = mat->metallicRoughnessTexture != nullptr ? mat->texCoordSets.metallicRoughness : -1;
			p.colorTextureSet = mat->baseColorTexture != nullptr ? mat->texCoordSets.baseColor : -1;
		}

		if (mat->pbrWorkflows.specularGlossiness)
		{
			// Specular glossiness workflow
			p.workflow = static_cast<float>(PBR_WORKFLOW_SPECULAR_GLOSSINESS);
			p.PhysicalDescriptorTextureSet = mat->extension.specularGlossinessTexture != nullptr ? mat->texCoordSets.specularGlossiness : -1;
			p.colorTextureSet = mat->extension.diffuseTexture != nullptr ? mat->texCoordSets.baseColor : -1;
			glm_vec4_copy(mat->extension.diffuseFactor, p.diffuseFactor);
			glm_vec4(mat->extension.specularFactor, 1.0f, p.specularFactor);
		}

		*materialParamsSSBO = p;

		// Increment along, sir!
		materialParamsSSBO++;
	}
	vmaUnmapMemory(_allocator, materialParamsBuffer._allocation);

	//
	// Voxel Field Lightgrids Descriptor Set
	//
	initVoxelLightingDescriptor();

	//
	// Joint Descriptor
	//
	vkglTF::Animator::initializeEmpty(this);

	// Add cleanup procedure
	_mainDeletionQueue.pushFunction([=]() {
		vmaDestroyBuffer(_allocator, materialParamsBuffer._buffer, materialParamsBuffer._allocation);
	});

	//
	// Text Mesh Fonts
	//
	textmesh::loadFontSDF("res/textures/font_sdf_rgba.png", "res/font.fnt", "defaultFont");

	physengine::initDebugVisDescriptors(this);
}

void VulkanEngine::initPipelines()
{
	//
	// Load shader modules
	//
	/*VkShaderModule debugPhysicsObjectVertShader,
					debugPhysicsObjectFragShader;
	loadShaderModule("shader/debug_physics_object.vert.spv", debugPhysicsObjectVertShader);
	loadShaderModule("shader/debug_physics_object.frag.spv", debugPhysicsObjectFragShader);*/



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

	// Mesh ZPrepass Pipeline
	VkPipeline meshZPrepassPipeline;
	VkPipelineLayout meshZPrepassPipelineLayout;
	vkutil::pipelinebuilder::build(
		{},
		{ _globalSetLayout, _objectSetLayout, _instancePtrSetLayout, _pbrTexturesSetLayout, _skeletalAnimationSetLayout },
		{
			{ VK_SHADER_STAGE_VERTEX_BIT, "shader/pbr_zprepass.vert.spv" },
			{ VK_SHADER_STAGE_FRAGMENT_BIT, "shader/pbr_khr_zprepass.frag.spv" },
		},
		modelVertexDescription.attributes,
		modelVertexDescription.bindings,
		vkinit::inputAssemblyCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
		screenspaceViewport,
		screenspaceScissor,
		vkinit::rasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT),
		{}, // No color attachment for the z prepass pipeline; only writing to depth!
		vkinit::multisamplingStateCreateInfo(),
		vkinit::depthStencilCreateInfo(true, true, VK_COMPARE_OP_LESS),
		{},
		_mainRenderPass,
		0,
		meshZPrepassPipeline,
		meshZPrepassPipelineLayout
		);
	attachPipelineToMaterial(meshZPrepassPipeline, meshZPrepassPipelineLayout, "pbrZPrepassMaterial");

	// Mesh Pipeline
	VkPipeline meshPipeline;
	VkPipelineLayout meshPipelineLayout;
	vkutil::pipelinebuilder::build(
		{},
		{ _globalSetLayout, _objectSetLayout, _instancePtrSetLayout, _pbrTexturesSetLayout, _skeletalAnimationSetLayout, _voxelFieldLightingGridTextureSet.layout },
		{
			{ VK_SHADER_STAGE_VERTEX_BIT, "shader/pbr.vert.spv" },
			{ VK_SHADER_STAGE_FRAGMENT_BIT, "shader/pbr_khr.frag.spv" },
		},
		modelVertexDescription.attributes,
		modelVertexDescription.bindings,
		vkinit::inputAssemblyCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
		screenspaceViewport,
		screenspaceScissor,
		vkinit::rasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT),
		{ vkinit::colorBlendAttachmentState() },
		vkinit::multisamplingStateCreateInfo(),
		vkinit::depthStencilCreateInfo(true, false, VK_COMPARE_OP_EQUAL),
		{},
		_mainRenderPass,
		1,
		meshPipeline,
		meshPipelineLayout
		);
	attachPipelineToMaterial(meshPipeline, meshPipelineLayout, "pbrMaterial");

	// Skybox pipeline
	VkPipeline skyboxPipeline;
	VkPipelineLayout skyboxPipelineLayout;
	vkutil::pipelinebuilder::build(
		{},
		{ _globalSetLayout, _singleTextureSetLayout },
		{
			{ VK_SHADER_STAGE_VERTEX_BIT, "shader/skybox.vert.spv" },
			{ VK_SHADER_STAGE_FRAGMENT_BIT, "shader/skybox.frag.spv" },
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
		skyboxPipelineLayout
		);
	attachPipelineToMaterial(skyboxPipeline, skyboxPipelineLayout, "skyboxMaterial");

	// Picking pipeline
	VkPipeline pickingPipeline;
	VkPipelineLayout pickingPipelineLayout;
	vkutil::pipelinebuilder::build(
		{},
		{ _globalSetLayout, _objectSetLayout, _instancePtrSetLayout, _pickingReturnValueSetLayout, _skeletalAnimationSetLayout },
		{
			{ VK_SHADER_STAGE_VERTEX_BIT, "shader/picking.vert.spv" },
			{ VK_SHADER_STAGE_FRAGMENT_BIT, "shader/picking.frag.spv" },
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
		pickingPipelineLayout
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
		{ _globalSetLayout, _objectSetLayout, _instancePtrSetLayout, _skeletalAnimationSetLayout },
		{
			{ VK_SHADER_STAGE_VERTEX_BIT, "shader/wireframe_color.vert.spv" },
			{ VK_SHADER_STAGE_FRAGMENT_BIT, "shader/color.frag.spv" },
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
		wireframePipelineLayout
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
		{ _globalSetLayout, _objectSetLayout, _instancePtrSetLayout, _skeletalAnimationSetLayout },
		{
			{ VK_SHADER_STAGE_VERTEX_BIT, "shader/wireframe_color.vert.spv" },
			{ VK_SHADER_STAGE_FRAGMENT_BIT, "shader/color.frag.spv" },
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
		wireframePipelineLayout
		);
	attachPipelineToMaterial(wireframeBehindPipeline, wireframePipelineLayout, "wireframeColorBehindMaterial");

	// Shadow Depth Pass pipeline
	auto shadowRasterizer = vkinit::rasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE);
	shadowRasterizer.depthClampEnable = VK_TRUE;

	VkPipeline shadowDepthPassPipeline;
	VkPipelineLayout shadowDepthPassPipelineLayout;
	vkutil::pipelinebuilder::build(
		{
			VkPushConstantRange{
				.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
				.offset = 0,
				.size = sizeof(CascadeIndexPushConstBlock)
			}
		},
		{ _cascadeViewProjsSetLayout, _objectSetLayout, _instancePtrSetLayout, _pbrTexturesSetLayout, _skeletalAnimationSetLayout },
		{
			{ VK_SHADER_STAGE_VERTEX_BIT, "shader/shadow_depthpass.vert.spv" },
			{ VK_SHADER_STAGE_FRAGMENT_BIT, "shader/shadow_depthpass.frag.spv" },
		},
		modelVertexDescription.attributes,
		modelVertexDescription.bindings,
		vkinit::inputAssemblyCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
		VkViewport{
			0.0f, 0.0f,
			(float_t)SHADOWMAP_DIMENSION, (float_t)SHADOWMAP_DIMENSION,
			0.0f, 1.0f,
		},
		VkRect2D{
			{ 0, 0 },
			VkExtent2D{ SHADOWMAP_DIMENSION, SHADOWMAP_DIMENSION },
		},
		shadowRasterizer,
		{},  // No color attachment for this pipeline
		vkinit::multisamplingStateCreateInfo(),
		vkinit::depthStencilCreateInfo(true, true, VK_COMPARE_OP_LESS_OR_EQUAL),
		{},
		_shadowRenderPass,
		0,
		shadowDepthPassPipeline,
		shadowDepthPassPipelineLayout
		);
	attachPipelineToMaterial(shadowDepthPassPipeline, shadowDepthPassPipelineLayout, "shadowDepthPassMaterial");

	// Postprocess pipeline
	VkPipeline postprocessPipeline;
	VkPipelineLayout postprocessPipelineLayout;
	vkutil::pipelinebuilder::build(
		{},
		{ _globalSetLayout, _postprocessSetLayout },
		{
			{ VK_SHADER_STAGE_VERTEX_BIT, "shader/genbrdflut.vert.spv" },
			{ VK_SHADER_STAGE_FRAGMENT_BIT, "shader/postprocess.frag.spv" },
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
		postprocessPipelineLayout
	);
	attachPipelineToMaterial(postprocessPipeline, postprocessPipelineLayout, "postprocessMaterial");

	// Destroy pipelines when recreating swapchain
	_swapchainDependentDeletionQueue.pushFunction([=]() {
		vkDestroyPipeline(_device, meshZPrepassPipeline, nullptr);
		vkDestroyPipeline(_device, meshPipeline, nullptr);
		vkDestroyPipeline(_device, skyboxPipeline, nullptr);
		vkDestroyPipeline(_device, pickingPipeline, nullptr);
		vkDestroyPipeline(_device, wireframePipeline, nullptr);
		vkDestroyPipeline(_device, wireframeBehindPipeline, nullptr);
		vkDestroyPipeline(_device, shadowDepthPassPipeline, nullptr);
		vkDestroyPipeline(_device, postprocessPipeline, nullptr);
	});

	//
	// Other pipelines
	//
	textmesh::initPipeline(screenspaceViewport, screenspaceScissor);
	textbox::initPipeline(screenspaceViewport, screenspaceScissor);
	physengine::initDebugVisPipelines(_mainRenderPass, screenspaceViewport, screenspaceScissor);
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
		vkutil::pipelinebuilder::loadShaderModule("shader/filtercube.vert.spv", filtercubeVertShader);
		switch (target)
		{
		case ENVIRONMENT:
			vkutil::pipelinebuilder::loadShaderModule("shader/skyboxfiltercube.frag.spv", filtercubeFragShader);
			break;
		case IRRADIANCE:
			vkutil::pipelinebuilder::loadShaderModule("shader/irradiancecube.frag.spv", filtercubeFragShader);
			break;
		case PREFILTEREDENV:
			vkutil::pipelinebuilder::loadShaderModule("shader/prefilterenvmap.frag.spv", filtercubeFragShader);
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
			_mainDeletionQueue.pushFunction([=]() {
				vkDestroySampler(_device, cubemapTexture.sampler, nullptr);
				vkDestroyImageView(_device, cubemapTexture.imageView, nullptr);
			});
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
	vkutil::pipelinebuilder::loadShaderModule("shader/genbrdflut.vert.spv", genBrdfLUTVertShader);
	vkutil::pipelinebuilder::loadShaderModule("shader/genbrdflut.frag.spv", genBrdfLUTFragShader);

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
	initPickingRenderpass();
	initFramebuffers();
	initPipelines();

	_camera->sceneCamera.recalculateSceneCamera(_pbrRendering.gpuSceneShadingProps);

	_recreateSwapchain = false;
}

FrameData& VulkanEngine::getCurrentFrame()
{
	return _frames[_frameNumber % FRAME_OVERLAP];
}

void VulkanEngine::loadMeshes()
{
#define MULTITHREAD_MESH_LOADING 1
#if MULTITHREAD_MESH_LOADING
	tf::Executor e;
	tf::Taskflow taskflow;
#endif

	std::vector<std::pair<std::string, vkglTF::Model*>> modelNameAndModels;
	for (const auto& entry : std::filesystem::recursive_directory_iterator("res/models/"))
	{
		const auto& path = entry.path();
		if (std::filesystem::is_directory(path))
			continue;		// Ignore directories
		if (!path.has_extension() ||
			(path.extension().compare(".gltf") != 0 &&
			path.extension().compare(".glb") != 0))
			continue;		// @NOTE: ignore non-model files

		modelNameAndModels.push_back(
			std::make_pair<std::string, vkglTF::Model*>(path.stem().string(), nullptr)
		);

		size_t      targetIndex = modelNameAndModels.size() - 1;
		std::string pathString  = path.string();

#if MULTITHREAD_MESH_LOADING
		taskflow.emplace([&, targetIndex, pathString]() {
#endif
			modelNameAndModels[targetIndex].second = new vkglTF::Model();
			modelNameAndModels[targetIndex].second->loadFromFile(this, pathString);
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
}

void VulkanEngine::compactRenderObjectsIntoDraws(const FrameData& currentFrame, std::vector<size_t> onlyPoolIndices, std::vector<ModelWithIndirectDrawId>& outIndirectDrawCommandIdsForPoolIndex)
{
	std::lock_guard<std::mutex> lg(_roManager->renderObjectIndicesAndPoolMutex);

	//
	// Fill in object data into current frame object buffer
	// @NOTE: this kinda makes more sense to do this in `uploadCurrentFrameToGPU()`,
	//        however, that would require applying multiple lock guards, and would make the program slower.
	//
	void* objectData;
	vmaMapMemory(_allocator, currentFrame.objectBuffer._allocation, &objectData);
	GPUObjectData* objectSSBO = (GPUObjectData*)objectData;    // @IMPROVE: perhaps multithread this? Or only update when the object moves?
	for (size_t poolIndex : _roManager->_renderObjectsIndices)  // @NOTE: bc of the pool system these indices will be scattered, but that should work just fine
		glm_mat4_copy(
			_roManager->_renderObjectPool[poolIndex].transformMatrix,
			objectSSBO[poolIndex].modelMatrix
		);		// Another evil pointer trick I love... call me Dmitri the Evil
	vmaUnmapMemory(_allocator, currentFrame.objectBuffer._allocation);

	//
	// Cull out render object indices that are not marked as visible
	//
	std::vector<size_t> visibleIndices;
	for (size_t i = 0; i < _roManager->_renderObjectsIndices.size(); i++)
	{
		size_t poolIndex = _roManager->_renderObjectsIndices[i];
		RenderObject& object = _roManager->_renderObjectPool[poolIndex];

		// See if render object itself is visible
		if (!_roManager->_renderObjectLayersEnabled[(size_t)object.renderLayer])
			continue;
		if (object.model == nullptr)
			continue;

		// It's visible!!!!
		visibleIndices.push_back(poolIndex);
	}

	//
	// Gather the number of times a model is drawn
	//
	struct ModelDrawCount
	{
		vkglTF::Model* model;
		size_t drawCount;
		size_t baseModelRenderObjectIndex;
	};
	std::vector<ModelDrawCount> mdcs;

	vkglTF::Model* lastModel = nullptr;
	for (size_t roIdx = 0; roIdx < visibleIndices.size(); roIdx++)
	{
		RenderObject& ro = _roManager->_renderObjectPool[visibleIndices[roIdx]];
		if (ro.model == lastModel)
		{
			mdcs.back().drawCount++;
		}
		else
		{
			mdcs.push_back({
				.model = ro.model,
				.drawCount = 1,
				.baseModelRenderObjectIndex = roIdx,
				});
			lastModel = ro.model;
		}
	}

	//
	// Gather each model's meshes and collate them into their own draw commands
	//
	std::vector<MeshCapturedInfo> meshDraws;
	for (ModelDrawCount& mdc : mdcs)
	{
		uint32_t numMeshes = 0;
		mdc.model->appendPrimitiveDraws(meshDraws, numMeshes);

		for (size_t i = 0; i < numMeshes; i++)
		{
			// Tell each mesh to draw as many times as the model exists, thus
			// collating the mesh draws inside the model drawing window.
			meshDraws[meshDraws.size() - numMeshes + i].modelDrawCount = mdc.drawCount;
			meshDraws[meshDraws.size() - numMeshes + i].baseModelRenderObjectIndex = mdc.baseModelRenderObjectIndex;
			meshDraws[meshDraws.size() - numMeshes + i].meshNumInModel = numMeshes;
		}
	}

	//
	// Open up memory map for instance level data
	//
	void* instancePtrData;
	vmaMapMemory(_allocator, currentFrame.instancePtrBuffer._allocation, &instancePtrData);
	GPUInstancePointer* instancePtrSSBO = (GPUInstancePointer*)instancePtrData;

	void* indirectDrawCommandsData;
	vmaMapMemory(_allocator, currentFrame.indirectDrawCommandBuffer._allocation, &indirectDrawCommandsData);
	VkDrawIndexedIndirectCommand* indirectDrawCommands = (VkDrawIndexedIndirectCommand*)indirectDrawCommandsData;

	//
	// Write indirect commands
	//
	std::vector<IndirectBatch> batches;
	lastModel = nullptr;
	size_t instanceID = 0;
	size_t meshIndex = 0;
	size_t baseModelRenderObjectIndex = 0;
	for (MeshCapturedInfo& ib : meshDraws)
	{
		// Combine the mesh-level draw commands into model-level draw commands.
		if (lastModel == ib.model)
		{
			batches.back().count += ib.modelDrawCount;
		}
		else
		{
			batches.push_back({
				.model = ib.model,
				.first = (uint32_t)instanceID,
				.count = ib.modelDrawCount,  // @NOTE: since the meshes are collated, we need to draw each mesh the number of times the model is going to get drawn.
				});

			lastModel = ib.model;
			meshIndex = 0;  // New model, so reset the mesh index counter.
			baseModelRenderObjectIndex = ib.baseModelRenderObjectIndex;
		}

		// Create draw command for each mesh instance.
		for (size_t modelIndex = 0; modelIndex < ib.modelDrawCount; modelIndex++)
		{
			*indirectDrawCommands = {
				.indexCount = ib.meshIndexCount,
				.instanceCount = 1,
				.firstIndex = ib.meshFirstIndex,
				.vertexOffset = 0,
				.firstInstance = (uint32_t)instanceID,
			};
			indirectDrawCommands++;

			GPUInstancePointer& gip = _roManager->_renderObjectPool[visibleIndices[baseModelRenderObjectIndex + modelIndex]].calculatedModelInstances[meshIndex];
#ifdef _DEVELOP
			if (!onlyPoolIndices.empty())
				for (size_t index : onlyPoolIndices)
					if (index == gip.objectID)
					{
						// Include this draw indirect command.
						outIndirectDrawCommandIdsForPoolIndex.push_back({ ib.model, (uint32_t)instanceID });
						break;
					}
#endif
			*instancePtrSSBO = gip;
			instancePtrSSBO++;

			instanceID++;
		}

		// Increment to the next mesh
		meshIndex++;
	}

	// Cleanup and return
	vmaUnmapMemory(_allocator, currentFrame.instancePtrBuffer._allocation);
	vmaUnmapMemory(_allocator, currentFrame.indirectDrawCommandBuffer._allocation);
	indirectBatches = batches;
}

void VulkanEngine::renderRenderObjects(VkCommandBuffer cmd, const FrameData& currentFrame)
{
	// Iterate thru all the batches
	uint32_t drawStride = sizeof(VkDrawIndexedIndirectCommand);
	for (IndirectBatch& batch : indirectBatches)
	{
		VkDeviceSize indirectOffset = batch.first * drawStride;
		batch.model->bind(cmd);
		vkCmdDrawIndexedIndirect(cmd, currentFrame.indirectDrawCommandBuffer._buffer, indirectOffset, batch.count, drawStride);
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
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, material.pipelineLayout, 3, 1, vkglTF::Animator::getGlobalAnimatorNodeCollectionDescriptorSet(), 0, nullptr);

		// Push constants
		ColorPushConstBlock pc = {};
		glm_vec4_copy(materialColors[i], pc.color);
		vkCmdPushConstants(cmd, material.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ColorPushConstBlock), &pc);

		// Render objects
		uint32_t drawStride = sizeof(VkDrawIndexedIndirectCommand);
		for (const ModelWithIndirectDrawId& mwidid : indirectDrawCommandIds)
		{
			VkDeviceSize indirectOffset = mwidid.indirectDrawId * drawStride;
			mwidid.model->bind(cmd);
			vkCmdDrawIndexedIndirect(cmd, currentFrame.indirectDrawCommandBuffer._buffer, indirectOffset, 1, drawStride);
		}
	}
}

#ifdef _DEVELOP
void VulkanEngine::updateDebugStats(const float_t& deltaTime)
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

void VulkanEngine::renderImGuiContent(float_t deltaTime, ImGuiIO& io)
{
	static bool showDemoWindows = false;
	// if (input::onKeyF1Press)  // @DEBUG: enable this to allow toggling showing demo windows.
	// 	showDemoWindows = !showDemoWindows;
	if (showDemoWindows)
	{
		ImGui::ShowDemoWindow();
		ImPlot::ShowDemoWindow();
	}

	bool allowKeyboardShortcuts =
		_camera->getCameraMode() == Camera::_cameraMode_freeCamMode &&
		!_camera->freeCamMode.enabled &&
		!io.WantTextInput;

	//
	// Debug Messages window
	//
	debug::renderImguiDebugMessages((float_t)_windowExtent.width, deltaTime);

	//
	// Scene Properties window
	//
	static std::string _flagNextStepLoadThisPathAsAScene = "";
	if (!_flagNextStepLoadThisPathAsAScene.empty())
	{
		scene::loadScene(_flagNextStepLoadThisPathAsAScene, this);
		_flagNextStepLoadThisPathAsAScene = "";
	}

	static float_t scenePropertiesWindowWidth = 0.0f;
	ImGui::SetNextWindowPos(ImVec2(_windowExtent.width - scenePropertiesWindowWidth, 0.0f), ImGuiCond_Always);
	ImGui::Begin((globalState::savedActiveScene + " Properties").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
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
					for (auto& ent : _entityManager->_entities)
						_entityManager->destroyEntity(ent);
					_flagNextStepLoadThisPathAsAScene = path;  // @HACK: mireba wakaru... but it's needed bc it works when delaying the load by a step...  -Timo 2022/10/30
					ImGui::CloseCurrentPopup();
				}
			ImGui::EndPopup();
		}

		ImGui::SameLine();
		if (ImGui::Button("Save Scene"))
			scene::saveScene(globalState::savedActiveScene, _entityManager->_entities, this);

		ImGui::SameLine();
		if (ImGui::Button("Save Scene As.."))
			ImGui::OpenPopup("save_scene_as_popup");
		if (ImGui::BeginPopup("save_scene_as_popup"))
		{
			static std::string saveSceneAsFname;
			ImGui::InputText(".ssdat", &saveSceneAsFname);
			if (ImGui::Button(("Save As \"" + saveSceneAsFname + ".ssdat\"").c_str()))
			{
				scene::saveScene(saveSceneAsFname + ".ssdat", _entityManager->_entities, this);
				globalState::savedActiveScene = saveSceneAsFname + ".ssdat";
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		scenePropertiesWindowWidth = ImGui::GetWindowWidth();
	}
	ImGui::End();

	//
	// Debug Stats window
	//
	constexpr float_t windowPadding = 8.0f;
	static float_t debugStatsWindowWidth = 0.0f;
	static float_t debugStatsWindowHeight = 0.0f;

	ImGui::SetNextWindowPos(ImVec2(_windowExtent.width * 0.5f - debugStatsWindowWidth - windowPadding, _windowExtent.height - debugStatsWindowHeight), ImGuiCond_Always);		// @NOTE: the ImGuiCond_Always means that this line will execute always, when set to once, this line will be ignored after the first time it's called
	ImGui::Begin("##Debug Statistics", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);
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

	//
	// GameState info window
	//
	static float_t gamestateInfoWindowHeight = 0.0f;
	ImGui::SetNextWindowPos(ImVec2(_windowExtent.width * 0.5f + windowPadding, _windowExtent.height - gamestateInfoWindowHeight), ImGuiCond_Always);		// @NOTE: the ImGuiCond_Always means that this line will execute always, when set to once, this line will be ignored after the first time it's called
	ImGui::Begin("##GameState Info", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
	{
		ImGui::Text(("Health: " + std::to_string(globalState::savedPlayerHealth) + " / " + std::to_string(globalState::savedPlayerMaxHealth)).c_str());

		gamestateInfoWindowHeight = ImGui::GetWindowHeight();
	}
	ImGui::End();


	//
	// PBR Shading Properties
	//
	static float_t scrollSpeed = 40.0f;
	static float_t windowOffsetY = 0.0f;
	float_t accumulatedWindowHeight = 0.0f;
	float_t maxWindowWidth = 0.0f;

	ImGui::SetNextWindowPos(ImVec2(0, accumulatedWindowHeight + windowOffsetY), ImGuiCond_Always);
	ImGui::Begin("PBR Shading Properties", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
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
			"res/_develop/layer_visible_sfx.ogg",
			"res/_develop/layer_invisible_sfx.ogg",
			"res/_develop/layer_builder_sfx.ogg",
			"res/_develop/layer_collision_sfx.ogg",
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

		accumulatedWindowHeight += ImGui::GetWindowHeight() + windowPadding;
		maxWindowWidth = std::max(maxWindowWidth, ImGui::GetWindowWidth());
	}
	ImGui::End();

	//
	// Global Properties
	//
	ImGui::SetNextWindowPos(ImVec2(0, accumulatedWindowHeight + windowOffsetY), ImGuiCond_Always);
	ImGui::Begin("Global Properties", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
	{
		if (ImGui::CollapsingHeader("Debug Properties", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::DragFloat("scrollSpeed", &scrollSpeed);
		}

		if (ImGui::CollapsingHeader("Camera Properties", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Text("NOTE: press F10 to change camera types");

			ImGui::SliderFloat("lookDistance", &_camera->mainCamMode.lookDistance, 1.0f, 100.0f);
			ImGui::DragFloat("focusRadiusXZ", &_camera->mainCamMode.focusRadiusXZ, 1.0f, 0.0f);
			ImGui::DragFloat("focusRadiusY", &_camera->mainCamMode.focusRadiusY, 1.0f, 0.0f);
			ImGui::SliderFloat("focusCentering", &_camera->mainCamMode.focusCentering, 0.0f, 1.0f);
			ImGui::DragFloat3("focusPositionOffset", _camera->mainCamMode.focusPositionOffset);
			ImGui::DragFloat("opponentTargetingAngles.theta1", &_camera->mainCamMode.opponentTargetingAngles.theta1, 0.01f);
			ImGui::DragFloat("opponentTargetingAngles.theta2", &_camera->mainCamMode.opponentTargetingAngles.theta2, 0.01f);
		}

		if (ImGui::CollapsingHeader("Textbox Properties", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::DragFloat3("mainRenderPosition", textbox::mainRenderPosition);
			ImGui::DragFloat3("mainRenderExtents", textbox::mainRenderExtents);
			ImGui::DragFloat3("querySelectionsRenderPosition", textbox::querySelectionsRenderPosition);
		}

		accumulatedWindowHeight += ImGui::GetWindowHeight() + windowPadding;
		maxWindowWidth = std::max(maxWindowWidth, ImGui::GetWindowWidth());
	}
	ImGui::End();

	//
	// Create Entity
	//
	ImGui::SetNextWindowPos(ImVec2(0, accumulatedWindowHeight + windowOffsetY), ImGuiCond_Always);
	ImGui::Begin("Create Entity", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
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
			auto newEnt = scene::spinupNewObject(listEntityTypes[(size_t)entityToCreateIndex], this, nullptr);
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
			if (ImGui::Button("Duplicate Selected Entity") || (allowKeyboardShortcuts && input::keyCtrlPressed && input::keyDPressed))
			{
				if (canRunDuplicateProc)
				{
					DataSerializer ds;
					selectedEntity->dump(ds);
					auto dsd = ds.getSerializedData();
					auto newEnt = scene::spinupNewObject(selectedEntity->getTypeName(), this, &dsd);
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

			if (ImGui::Button("Delete Selected Entity!") || (allowKeyboardShortcuts && input::keyDelPressed))
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


		accumulatedWindowHeight += ImGui::GetWindowHeight() + windowPadding;
		maxWindowWidth = std::max(maxWindowWidth, ImGui::GetWindowWidth());
	}
	ImGui::End();

	//
	// Moving stuff around window (using ImGuizmo)
	//
	if (_movingMatrix.matrixToMove != nullptr)
	{
		//
		// Move the matrix via ImGuizmo
		//
		mat4 projection;
		glm_mat4_copy(_camera->sceneCamera.gpuCameraData.projection, projection);
		projection[1][1] *= -1.0f;

		static ImGuizmo::OPERATION manipulateOperation = ImGuizmo::OPERATION::TRANSLATE;
		static ImGuizmo::MODE manipulateMode           = ImGuizmo::MODE::WORLD;

		vec3 snapValues(0.0f);
		if (input::keyCtrlPressed)
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

		//
		// Move the matrix via the decomposed values
		//
		ImGui::SetNextWindowPos(ImVec2(0, accumulatedWindowHeight + windowOffsetY), ImGuiCond_Always);
		ImGui::Begin("Edit Selected", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
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
				if (input::keyQPressed)
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

				if (input::keyWPressed)
				{
					operationIndex = 0;
					forceRecalculation = true;
				}
				if (input::keyEPressed)
				{
					operationIndex = 1;
					forceRecalculation = true;
				}
				if (input::keyRPressed)
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

			accumulatedWindowHeight += ImGui::GetWindowHeight() + windowPadding;
			maxWindowWidth = std::max(maxWindowWidth, ImGui::GetWindowWidth());
		}
		ImGui::End();
	}

	//
	// Scroll the left pane
	//
	if (io.MousePos.x <= maxWindowWidth)
		windowOffsetY += input::mouseScrollDelta[1] * scrollSpeed;
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
	if (input::onKeyF1Press)
		showImguiRender = !showImguiRender;

	if (showImguiRender)
		renderImGuiContent(deltaTime, io);

	ImGui::Render();
}
