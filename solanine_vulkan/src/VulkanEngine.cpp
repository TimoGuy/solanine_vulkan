#include "VulkanEngine.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vma/vk_mem_alloc.h>
#include "VkBootstrap.h"
#include "VkInitializers.h"
#include "VkTextures.h"
#include "GLSLToSPIRVHelper.h"
#include "VkglTFModel.h"
#include "AudioEngine.h"
#include "PhysicsEngine.h"
#include "InputManager.h"
#include "RenderObject.h"
#include "Entity.h"
#include "EntityManager.h"
#include "Camera.h"
#include "SceneManagement.h"
#include "DataSerialization.h"
#include "Debug.h"
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
	buildResourceList();
#endif

	initVulkan();
	initSwapchain();
	initCommands();
	initShadowRenderpass();  // @TODO: @NOTE: technically this doesn't have to be recreated bc it's not a screen space from swapchain based renderpass/framebuffer/image/imageview that goes bad/changes size (unless if settings are changed eh!)
	initDefaultRenderpass();
	initPickingRenderpass();
	initFramebuffers();
	initSyncStructures();
	initDescriptors();
	initImgui();
	initPipelines();
	loadImages();
	loadMeshes();
	initScene();
	generatePBRCubemaps();
	generateBRDFLUT();
	attachPBRDescriptors();

	AudioEngine::getInstance().initialize();
	PhysicsEngine::getInstance().initialize(this);

	SDL_ShowWindow(_window);

	_isInitialized = true;

	std::string startupSceneFname = "sample_scene_simplified.ssdat";
	scene::loadScene(startupSceneFname, this);
}

void VulkanEngine::run()
{
	//
	// Initialize Scene Camera
	//
	_camera->sceneCamera.aspect = (float_t)_windowExtent.width / (float_t)_windowExtent.height;
	_camera->sceneCamera.gpuCameraData.cameraPosition = {5.43231487, 13.2406960, 1.41502118};
	_camera->sceneCamera.facingDirection = {-0.570508420, -0.390730739, 0.722388268};
	
	// @HARDCODED: Set the initial light direction
	_pbrRendering.gpuSceneShadingProps.lightDir = glm::normalize(glm::vec4(0.432f, 0.864f, 0.259f, 0.0f));

	_camera->sceneCamera.recalculateSceneCamera(_pbrRendering.gpuSceneShadingProps);

	//
	// Main Loop
	//
	bool isRunning = true;
	float_t ticksFrequency = 1.0f / (float_t)SDL_GetPerformanceFrequency();
	uint64_t lastFrame = SDL_GetPerformanceCounter();

	while (isRunning)
	{
#ifdef _DEVELOP
		// Routine to reload any resources when they're updated
		// NOTE: We mayyyy have to move this to another thread, but it
		//       really doesn't seem to be slowing anything down. We'll
		//       see though  -Timo 2022/10/22
		checkIfResourceUpdatedThenHotswapRoutine();
#endif

		// Poll events from the window
		input::processInput(&isRunning, &_isWindowMinimized);

		// Update DeltaTime
		uint64_t currentFrame = SDL_GetPerformanceCounter();
		const float_t deltaTime = (float_t)(currentFrame - lastFrame) * ticksFrequency;
		lastFrame = currentFrame;

		// Stop anything from updating when window is minimized
		// @NOTE: this prevents the VK_ERROR_DEVICE_LOST(-4) error
		//        once the rendering code gets run while the window
		//        is minimized.  -Timo 2022/10/23
		// @NOTE: well, if you wanted the game to keep running in the
		//        background, you could just have this `continue;` block
		//        right after all of the simulations...
		if (_isWindowMinimized)
			continue;

		// Update physics
		auto& entities = _entityManager->_entities;
		PhysicsEngine::getInstance().update(deltaTime, &entities);

		// Collect debug stats
		updateDebugStats(deltaTime);

		// Update entities
		// @TODO: multithread this sucker!
		// @INCOMPLETE: put this inside the entity manager!
		for (auto it = entities.begin(); it != entities.end(); it++)
		{
			Entity* ent = *it;
			if (ent->_enableUpdate)
				ent->update(deltaTime);
		}

		// Update camera
		_camera->update(deltaTime);

		// Add/Remove requested entities
		_entityManager->INTERNALaddRemoveRequestedEntities();

		//
		// @TODO: loop thru animators and update them!
		//
		// @TEMP: Play Slimegirl animation
		//
		static uint32_t animationIndex = 31;    // @NOTE: this is Slimegirl's running inmotion animation
		static float_t animationTimer = 0.0f;
		animationTimer += deltaTime;
		auto slimeGirl = _roManager->getModel("slimeGirl");
		if (animationTimer > slimeGirl->animations[animationIndex].end)    // Loop animation
			animationTimer -= slimeGirl->animations[animationIndex].end;
		slimeGirl->updateAnimation(animationIndex, animationTimer);

		// Update Audio Engine
		AudioEngine::getInstance().update();

		// Render
		if (_recreateSwapchain)
			recreateSwapchain();
		PhysicsEngine::getInstance().lazyRecreateDebugDrawBuffer();

		renderImGui(deltaTime);
		render();
	}
}

void VulkanEngine::cleanup()
{
#ifdef _DEVELOP
	teardownResourceList();
#endif

	if (_isInitialized)
	{
		vkDeviceWaitIdle(_device);

		delete _entityManager;  // @NOTE: all entities must be deleted before the physicsengine can shut down

		PhysicsEngine::getInstance().cleanup();
		AudioEngine::getInstance().cleanup();

		delete _roManager;

		_mainDeletionQueue.flush();
		_swapchainDependentDeletionQueue.flush();

		vmaDestroyAllocator(_allocator);
		vkDestroySurfaceKHR(_instance, _surface, nullptr);
		vkb::destroy_debug_utils_messenger(_instance, _debugMessenger);
		vkDestroyDevice(_device, nullptr);
		vkDestroyInstance(_instance, nullptr);

		SDL_DestroyWindow(_window);
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

	// Request image from swapchain
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

	// Reset command buffer to start recording commands again
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

		Material* shadowDepthPassMaterial = getMaterial("shadowDepthPassMaterial");  // @TODO: @IMPLEMENT this material so we can use the correct shaders
		for (uint32_t i = 0; i < SHADOWMAP_CASCADES; i++)
		{
			renderpassInfo.framebuffer = _shadowCascades[i].framebuffer;
			vkCmdBeginRenderPass(cmd, &renderpassInfo, VK_SUBPASS_CONTENTS_INLINE);

			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowDepthPassMaterial->pipeline);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowDepthPassMaterial->pipelineLayout, 0, 1, &currentFrame.cascadeViewProjsDescriptor, 0, nullptr);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowDepthPassMaterial->pipelineLayout, 1, 1, &currentFrame.objectDescriptor, 0, nullptr);
			CascadeIndexPushConstBlock pc = { i };
			vkCmdPushConstants(cmd, shadowDepthPassMaterial->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(CascadeIndexPushConstBlock), &pc);

			renderRenderObjects(cmd, currentFrame, 0, _roManager->_renderObjects.size(), false, true, &shadowDepthPassMaterial->pipelineLayout, true);
			
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
			.framebuffer = _framebuffers[swapchainImageIndex],		// @NOTE: Framebuffer of the index the swapchain gave
			.renderArea = {
				.offset = VkOffset2D{ 0, 0 },
				.extent = _windowExtent,
			},

			.clearValueCount = 2,
			.pClearValues = &clearValues[0],
		};

		// Begin renderpass
		vkCmdBeginRenderPass(cmd, &renderpassInfo, VK_SUBPASS_CONTENTS_INLINE);

		uploadCurrentFrameToGPU(currentFrame);
		renderRenderObjects(cmd, currentFrame, 0, _roManager->_renderObjects.size(), true, false, nullptr, false);
		renderPickedObject(cmd, currentFrame);
		if (_showCollisionDebugDraw)
			PhysicsEngine::getInstance().renderDebugDraw(cmd, currentFrame.globalDescriptor);
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
	// Picking Render Pass
	//
	if (input::onLMBPress &&
		_camera->getCameraMode() == Camera::_cameraMode_freeCamMode &&
		!_camera->freeCamMode.enabled &&
		!ImGui::GetIO().WantCaptureMouse &&
		!ImGuizmo::IsUsing() &&
		!ImGuizmo::IsOver() &&
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

		// Global data descriptor             (set = 0)
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pickingMaterial.pipelineLayout, 0, 1, &currentFrame.globalDescriptor, 0, nullptr);

		// Object data descriptor             (set = 1)
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pickingMaterial.pipelineLayout, 1, 1, &currentFrame.objectDescriptor, 0, nullptr);

		// Picking Return value id descriptor (set = 3)
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pickingMaterial.pipelineLayout, 3, 1, &currentFrame.pickingReturnValueDescriptor, 0, nullptr);

		// Set dynamic scissor
		VkRect2D scissor = {};
		scissor.offset.x = (int32_t)ImGui::GetIO().MousePos.x;
		scissor.offset.y = (int32_t)ImGui::GetIO().MousePos.y;
		scissor.extent = { 1, 1 };
		vkCmdSetScissor(cmd, 0, 1, &scissor);    // @NOTE: the scissor is set to be dynamic state for this pipeline

		renderRenderObjects(cmd, currentFrame, 0, _roManager->_renderObjects.size(), false, true, &pickingMaterial.pipelineLayout, false);    // @NOTE: the joint descriptorset will still be bound in here   @HACK: it's using the wrong pipelinelayout but.... it should be fine? Bc the slot is still set=3 for the joints on the picking pipelinelayout too??

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
		GPUPickingSelectedIdData resetData = { 0 };
		GPUPickingSelectedIdData p;

		void* data;
		vmaMapMemory(_allocator, currentFrame.pickingSelectedIdBuffer._allocation, &data);
		memcpy(&p, data, sizeof(GPUPickingSelectedIdData));            // It's not Dmitri, it's Irtimd this time
		memcpy(data, &resetData, sizeof(GPUPickingSelectedIdData));    // @NOTE: if you don't reset the buffer, then you won't get 0 if you click on an empty spot next time bc you end up just getting garbage data.  -Dmitri
		vmaUnmapMemory(_allocator, currentFrame.pickingSelectedIdBuffer._allocation);

		submitSelectedRenderObjectId(static_cast<int32_t>(p.selectedId) - 1);
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

		VkSamplerCreateInfo samplerInfo = vkinit::samplerCreateInfo(static_cast<float_t>(empty.image._mipLevels), VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
		vkCreateSampler(_device, &samplerInfo, nullptr, &empty.sampler);

		_mainDeletionQueue.pushFunction([=]() {
			vkDestroySampler(_device, empty.sampler, nullptr);
			vkDestroyImageView(_device, empty.imageView, nullptr);
			});

		_loadedTextures["empty"] = empty;
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

	// Load cubemapSkybox
	{
		Texture cubemapSkybox;
		//vkutil::loadImageCubemapFromFile(
		//	*this,
		//	{
		//		"res/textures/CubemapSkybox/left.png",
		//		"res/textures/CubemapSkybox/right.png",
		//		"res/textures/CubemapSkybox/top.png",       // @NOTE: had to be 180deg rotated
		//		"res/textures/CubemapSkybox/bottom.png",    // @NOTE: had to be 180deg rotated
		//		"res/textures/CubemapSkybox/back.png",
		//		"res/textures/CubemapSkybox/front.png",
		//	},
		//	false,
		//	VK_FORMAT_R8G8B8A8_SRGB,
		//	1,
		//	cubemapSkybox.image
		//);
		vkutil::loadImageCubemapFromFile(
			*this,
			{
				// Generated by https://matheowis.github.io/HDRI-to-CubeMap/
				"res/textures/BrownPhotostudio07HDRI/px.hdr",
				"res/textures/BrownPhotostudio07HDRI/nx.hdr",
				"res/textures/BrownPhotostudio07HDRI/py.hdr",
				"res/textures/BrownPhotostudio07HDRI/ny.hdr",
				"res/textures/BrownPhotostudio07HDRI/pz.hdr",
				"res/textures/BrownPhotostudio07HDRI/nz.hdr",
			},
			true,
			VK_FORMAT_R32G32B32A32_SFLOAT,
			1,
			cubemapSkybox.image
		);

		VkImageViewCreateInfo imageInfo = vkinit::imageviewCubemapCreateInfo(VK_FORMAT_R32G32B32A32_SFLOAT, cubemapSkybox.image._image, VK_IMAGE_ASPECT_COLOR_BIT, cubemapSkybox.image._mipLevels);
		vkCreateImageView(_device, &imageInfo, nullptr, &cubemapSkybox.imageView);

		VkSamplerCreateInfo samplerInfo = vkinit::samplerCreateInfo(static_cast<float_t>(cubemapSkybox.image._mipLevels), VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
		vkCreateSampler(_device, &samplerInfo, nullptr, &cubemapSkybox.sampler);

		_mainDeletionQueue.pushFunction([=]() {
			vkDestroySampler(_device, cubemapSkybox.sampler, nullptr);
			vkDestroyImageView(_device, cubemapSkybox.imageView, nullptr);
			});

		_loadedTextures["CubemapSkybox"] = cubemapSkybox;
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

	//
	// @TODO: add a thing to destroy all the loaded images from _loadedTextures hashmap
	//
}

Material* VulkanEngine::createMaterial(VkPipeline pipeline, VkPipelineLayout layout, const std::string& name)
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
		.set_minimum_version(1, 3)
		.set_surface(_surface)
		.set_required_features({
			// @NOTE: @FEATURES: Enable required features right here
			.depthClamp = VK_TRUE,				// @NOTE: for shadow maps, this is really nice
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
	// Spit out phsyical device properties
	//
	std::cout << "[Chosen Physical Device Properties]" << std::endl;
	std::cout << "API_VERSION                 " << VK_API_VERSION_MAJOR(_gpuProperties.apiVersion) << "." << VK_API_VERSION_MINOR(_gpuProperties.apiVersion) << "." << VK_API_VERSION_PATCH(_gpuProperties.apiVersion) << "." << VK_API_VERSION_VARIANT(_gpuProperties.apiVersion) << std::endl;
	std::cout << "DRIVER_VERSION              " << _gpuProperties.driverVersion << std::endl;
	std::cout << "VENDOR_ID                   " << _gpuProperties.vendorID << std::endl;
	std::cout << "DEVICE_ID                   " << _gpuProperties.deviceID << std::endl;
	std::cout << "DEVICE_TYPE                 " << _gpuProperties.deviceType << std::endl;
	std::cout << "DEVICE_NAME                 " << _gpuProperties.deviceName << std::endl;
	std::cout << "MAX_IMAGE_DIMENSION_1D      " << _gpuProperties.limits.maxImageDimension1D << std::endl;
	std::cout << "MAX_IMAGE_DIMENSION_2D      " << _gpuProperties.limits.maxImageDimension2D << std::endl;
	std::cout << "MAX_IMAGE_DIMENSION_3D      " << _gpuProperties.limits.maxImageDimension3D << std::endl;
	std::cout << "MAX_IMAGE_DIMENSION_CUBE    " << _gpuProperties.limits.maxImageDimensionCube << std::endl;
	std::cout << "MAX_IMAGE_ARRAY_LAYERS      " << _gpuProperties.limits.maxImageArrayLayers << std::endl;
	std::cout << "MAX_SAMPLER_ANISOTROPY      " << _gpuProperties.limits.maxSamplerAnisotropy << std::endl;
	std::cout << "MAX_BOUND_DESCRIPTOR_SETS   " << _gpuProperties.limits.maxBoundDescriptorSets << std::endl;
	std::cout << "MINIMUM_BUFFER_ALIGNMENT    " << _gpuProperties.limits.minUniformBufferOffsetAlignment << std::endl;
	std::cout << "MAX_COLOR_ATTACHMENTS       " << _gpuProperties.limits.maxColorAttachments << std::endl;
	std::cout << std::endl;

	vkinit::_maxSamplerAnisotropy = _gpuProperties.limits.maxSamplerAnisotropy;
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

		// Add destroy command for cleanup
		_mainDeletionQueue.pushFunction([=]() {
			vkDestroyCommandPool(_device, _frames[i].commandPool, nullptr);
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

	_swapchainDependentDeletionQueue.pushFunction([=]() {
		vkDestroySampler(_device, _pbrSceneTextureSet.shadowMap.sampler, nullptr);
		vkDestroyImageView(_device, _pbrSceneTextureSet.shadowMap.imageView, nullptr);
		vmaDestroyImage(_allocator, _pbrSceneTextureSet.shadowMap.image._image, _pbrSceneTextureSet.shadowMap.image._allocation);
		});

	// Once framebuffer and imageview per layer of shadow image
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

		_swapchainDependentDeletionQueue.pushFunction([=]() {
			vkDestroyFramebuffer(_device, _shadowCascades[i].framebuffer, nullptr);
			vkDestroyImageView(_device, _shadowCascades[i].imageView, nullptr);
			});
	}
}

void VulkanEngine::initDefaultRenderpass()
{
	//
	// Color Attachment
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

	VK_CHECK(vkCreateRenderPass(_device, &renderPassInfo, nullptr, &_mainRenderPass));

	// Add destroy command for cleanup
	_swapchainDependentDeletionQueue.pushFunction([=]() {
		vkDestroyRenderPass(_device, _mainRenderPass, nullptr);
		});
}

void VulkanEngine::initPickingRenderpass()    // @NOTE: @COPYPASTA: This is really copypasta of the above function (initDefaultRenderpass)
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
	VkFramebufferCreateInfo fbInfo = {};
	fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fbInfo.pNext = nullptr;

	fbInfo.renderPass = _mainRenderPass;
	fbInfo.attachmentCount = 2;
	fbInfo.width = _windowExtent.width;
	fbInfo.height = _windowExtent.height;
	fbInfo.layers = 1;

	const uint32_t swapchainImagecount = static_cast<uint32_t>(_swapchainImages.size());
	_framebuffers = std::vector<VkFramebuffer>(swapchainImagecount);

	for (size_t i = 0; i < swapchainImagecount; i++)
	{
		VkImageView attachments[] = {
			_swapchainImageViews[i],
			_depthImageView
		};
		fbInfo.pAttachments = &attachments[0];

		VK_CHECK(vkCreateFramebuffer(_device, &fbInfo, nullptr, &_framebuffers[i]));

		// Add destroy command for cleanup
		_swapchainDependentDeletionQueue.pushFunction([=]() {
			vkDestroyFramebuffer(_device, _framebuffers[i], nullptr);
			vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
			});
	}

	//
	// Create framebuffer for the picking renderpass
	//
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
	// Create Descriptor Pool
	//
	std::vector<VkDescriptorPoolSize> sizes = {
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 10000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10000 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10000 },
	};
	VkDescriptorPoolCreateInfo poolInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = 0,
		.maxSets = 10000,										// @NOTE: for the new/better descriptorpool, there NEEDS to be one for all of the information that will be recreated when the window is resized and then one that's different that will be just for shader inputs etc. bc afaik removing those descriptors isn't necessary.   @NOTE: I need a better descriptorpool allocator... @TODO: @FIXME: crate teh one that's in the vkguide extra chapter
		.poolSizeCount = (uint32_t)sizes.size(),				//        @REPLY: and... I think only the pipelines (specifically the rasterizers in the pipelines which have the viewport & scissors fields) are the only things that need to get recreated upon resizing (and of course the textures that are backing up those pipelines), so perhaps a deallocation of the descriptorsets is needed only and then reallocate the necessary descriptorsets (maybe it means that the descriptorset needs to be destroyed too... Idk... I guess it all depends on what the vulkan spec requires eh!)
		.pPoolSizes = sizes.data(),
	};
	vkCreateDescriptorPool(_device, &poolInfo, nullptr, &_descriptorPool);

	//
	// Create Descriptor Set Layout for camera buffers
	//
	VkDescriptorSetLayoutBinding cameraBufferBinding          = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0);
	VkDescriptorSetLayoutBinding shadingPropsBufferBinding    = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 1);
	VkDescriptorSetLayoutBinding pbrIrradianceTextureBinding  = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2);
	VkDescriptorSetLayoutBinding pbrPrefilteredTextureBinding = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 3);
	VkDescriptorSetLayoutBinding pbrBRDFLUTTextureBinding     = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 4);
	VkDescriptorSetLayoutBinding shadowMap                    = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 5);
	VkDescriptorSetLayoutBinding bindings[] = {
		cameraBufferBinding,
		shadingPropsBufferBinding,
		pbrIrradianceTextureBinding,
		pbrPrefilteredTextureBinding,
		pbrBRDFLUTTextureBinding,
		shadowMap,
	};
	VkDescriptorSetLayoutCreateInfo setInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.bindingCount = 6,
		.pBindings = bindings,
	};
	vkCreateDescriptorSetLayout(_device, &setInfo, nullptr, &_globalSetLayout);

	//
	// Create Descriptor Set Layout for cascade view projections buffer
	//
	VkDescriptorSetLayoutBinding cascadeViewProjsBufferBinding = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0);
	VkDescriptorSetLayoutCreateInfo cascadeViewProjsSetInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.bindingCount = 1,
		.pBindings = &cascadeViewProjsBufferBinding,
	};
	vkCreateDescriptorSetLayout(_device, &cascadeViewProjsSetInfo, nullptr, &_cascadeViewProjsSetLayout);

	//
	// Create Descriptor Set Layout for object buffer
	//
	VkDescriptorSetLayoutBinding objectBufferBinding = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0);
	VkDescriptorSetLayoutCreateInfo setInfo2 = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.bindingCount = 1,
		.pBindings = &objectBufferBinding,
	};
	vkCreateDescriptorSetLayout(_device, &setInfo2, nullptr, &_objectSetLayout);

	//
	// Create Descriptor Set Layout for singletexture buffer
	//
	VkDescriptorSetLayoutBinding singleTextureBufferBinding = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0);
	VkDescriptorSetLayoutCreateInfo setInfo3 = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.bindingCount = 1,
		.pBindings = &singleTextureBufferBinding,
	};
	vkCreateDescriptorSetLayout(_device, &setInfo3, nullptr, &_singleTextureSetLayout);

	//
	// Create Descriptor Set Layout for pbr textures set buffer
	// @NOTE: these will be allocated and actual buffers created
	//        on a material basis (i.e. it's not allocated here)
	//
	VkDescriptorSetLayoutBinding pbrColorMapBufferBinding = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0);
	VkDescriptorSetLayoutBinding pbrPhysicalDescriptorMapBufferBinding = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1);
	VkDescriptorSetLayoutBinding pbrNormalMapBufferBinding = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2);
	VkDescriptorSetLayoutBinding pbrAOMapBufferBinding = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 3);
	VkDescriptorSetLayoutBinding pbrEmissiveMapBufferBinding = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 4);
	VkDescriptorSetLayoutBinding pbrTexturesBufferBindings[] = {
		pbrColorMapBufferBinding,
		pbrPhysicalDescriptorMapBufferBinding,
		pbrNormalMapBufferBinding,
		pbrAOMapBufferBinding,
		pbrEmissiveMapBufferBinding,
	};
	VkDescriptorSetLayoutCreateInfo setInfo4 = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.bindingCount = 5,
		.pBindings = pbrTexturesBufferBindings,
	};
	vkCreateDescriptorSetLayout(_device, &setInfo4, nullptr, &_pbrTexturesSetLayout);

	//
	// Create Descriptor Set Layout for pbr textures set buffer
	// @NOTE: these will be allocated and actual buffers created
	//        on a material basis (i.e. it's not allocated here)
	//
	VkDescriptorSetLayoutBinding pickingSelectedIdBufferBinding = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 0);
	VkDescriptorSetLayoutCreateInfo setInfo5 = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.bindingCount = 1,
		.pBindings = &pickingSelectedIdBufferBinding,
	};
	vkCreateDescriptorSetLayout(_device, &setInfo5, nullptr, &_pickingReturnValueSetLayout);

	//
	// Create Descriptor Set Layout for skeletal animation joint matrices
	// @NOTE: similar to the pbr textures set buffer, these skeletal joint
	//        buffers will be created on a mesh basis (@TODO: actually, on
	//        an animator basis that will own a pointer to a mesh)
	//
	VkDescriptorSetLayoutBinding skeletalAnimationBinding = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0);
	VkDescriptorSetLayoutCreateInfo setInfo6 = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.bindingCount = 1,
		.pBindings = &skeletalAnimationBinding,
	};
	vkCreateDescriptorSetLayout(_device, &setInfo6, nullptr, &_skeletalAnimationSetLayout);

	//
	// NOTE: The supposed guaranteed maximum number of bindable descriptor sets is 4... so here we are at 3.
	// Start thinking/studying about what these descriptor sets really mean and how I can consolidate them...
	// 
	// Either way, it likely isn't a problem bc it's just the pbr shader that will have the maximum bindable descriptor
	// sets so it could just be that it will be fine? Kitto. And I'm sure it will be just fine. Will just have to be
	// careful... plus I haven't found a device that has that few allowed descriptor sets to be bound. The lowest I
	// found was 32 even for intel hd 4000 it's 32... weird. And I'd think that that would for sure be 4 bc it's such
	// a low-end chip.
	// 
	// Lmk know, future me!  -Timo
	// 
	// EDIT: I had a thought, I wonder if this talk about really low descriptor set binding sizes is just talking about the
	//       Nintendo Smitch? Idk... should I even care about that thing?  -Timo
	//

	//
	// Create buffers
	//
	for (size_t i = 0; i < FRAME_OVERLAP; i++)
	{
		// Create buffers
		_frames[i].cameraBuffer = createBuffer(sizeof(GPUCameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
		_frames[i].pbrShadingPropsBuffer = createBuffer(sizeof(GPUPBRShadingProps), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
		_frames[i].cascadeViewProjsBuffer = createBuffer(sizeof(GPUCascadeViewProjsData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
		_frames[i].objectBuffer = createBuffer(sizeof(GPUObjectData) * RENDER_OBJECTS_MAX_CAPACITY, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

		_frames[i].pickingSelectedIdBuffer = createBuffer(sizeof(GPUPickingSelectedIdData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU);    // @NOTE: primary focus is to read from gpu, so gpu_to_cpu

		// Allocate descriptor sets
		VkDescriptorSetAllocateInfo allocInfo = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.pNext = nullptr,
			.descriptorPool = _descriptorPool,
			.descriptorSetCount = 1,
			.pSetLayouts = &_globalSetLayout,
		};
		vkAllocateDescriptorSets(_device, &allocInfo, &_frames[i].globalDescriptor);

		VkDescriptorSetAllocateInfo cascadeViewProjsAllocInfo = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.pNext = nullptr,
			.descriptorPool = _descriptorPool,
			.descriptorSetCount = 1,
			.pSetLayouts = &_cascadeViewProjsSetLayout,
		};
		vkAllocateDescriptorSets(_device, &cascadeViewProjsAllocInfo, &_frames[i].cascadeViewProjsDescriptor);

		VkDescriptorSetAllocateInfo objectSetAllocInfo = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.pNext = nullptr,
			.descriptorPool = _descriptorPool,
			.descriptorSetCount = 1,
			.pSetLayouts = &_objectSetLayout,
		};
		vkAllocateDescriptorSets(_device, &objectSetAllocInfo, &_frames[i].objectDescriptor);

		VkDescriptorSetAllocateInfo pickingReturnValueSetAllocInfo = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.pNext = nullptr,
			.descriptorPool = _descriptorPool,
			.descriptorSetCount = 1,
			.pSetLayouts = &_pickingReturnValueSetLayout,
		};
		vkAllocateDescriptorSets(_device, &pickingReturnValueSetAllocInfo, &_frames[i].pickingReturnValueDescriptor);

		// Point descriptor set to camera buffer
		VkDescriptorBufferInfo cameraInfo = {
			.buffer = _frames[i].cameraBuffer._buffer,
			.offset = 0,
			.range = sizeof(GPUCameraData),
		};
		VkDescriptorBufferInfo shadingPropsInfo = {
			.buffer = _frames[i].pbrShadingPropsBuffer._buffer,
			.offset = 0,
			.range = sizeof(GPUPBRShadingProps),
		};
		VkDescriptorBufferInfo cascadeViewProjsBufferInfo = {
			.buffer = _frames[i].cascadeViewProjsBuffer._buffer,
			.offset = 0,
			.range = sizeof(GPUCascadeViewProjsData),
		};
		VkDescriptorBufferInfo objectBufferInfo = {
			.buffer = _frames[i].objectBuffer._buffer,
			.offset = 0,
			.range = sizeof(GPUObjectData) * RENDER_OBJECTS_MAX_CAPACITY,
		};
		VkDescriptorBufferInfo pickingSelectedIdBufferInfo = {
			.buffer = _frames[i].pickingSelectedIdBuffer._buffer,
			.offset = 0,
			.range = sizeof(GPUPickingSelectedIdData),
		};
		VkWriteDescriptorSet cameraWrite = vkinit::writeDescriptorBuffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, _frames[i].globalDescriptor, &cameraInfo, 0);
		VkWriteDescriptorSet shadingPropsWrite = vkinit::writeDescriptorBuffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, _frames[i].globalDescriptor, &shadingPropsInfo, 1);
		VkWriteDescriptorSet cascadeViewProjsWrite = vkinit::writeDescriptorBuffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, _frames[i].cascadeViewProjsDescriptor, &cascadeViewProjsBufferInfo, 0);
		VkWriteDescriptorSet objectWrite = vkinit::writeDescriptorBuffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _frames[i].objectDescriptor, &objectBufferInfo, 0);
		VkWriteDescriptorSet pickingSelectedIdWrite = vkinit::writeDescriptorBuffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _frames[i].pickingReturnValueDescriptor, &pickingSelectedIdBufferInfo, 0);
		VkWriteDescriptorSet setWrites[] = { cameraWrite, shadingPropsWrite, cascadeViewProjsWrite, objectWrite, pickingSelectedIdWrite };
		vkUpdateDescriptorSets(_device, 5, setWrites, 0, nullptr);
	}

	// Add destroy command for cleanup
	_mainDeletionQueue.pushFunction([=]() {
		vkDestroyDescriptorSetLayout(_device, _globalSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(_device, _cascadeViewProjsSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(_device, _objectSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(_device, _singleTextureSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(_device, _pbrTexturesSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(_device, _pickingReturnValueSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(_device, _skeletalAnimationSetLayout, nullptr);
		vkDestroyDescriptorPool(_device, _descriptorPool, nullptr);          // This deletes all of the descriptor sets

		for (uint32_t i = 0; i < FRAME_OVERLAP; i++)
		{
			vmaDestroyBuffer(_allocator, _frames[i].cameraBuffer._buffer, _frames[i].cameraBuffer._allocation);
			vmaDestroyBuffer(_allocator, _frames[i].pbrShadingPropsBuffer._buffer, _frames[i].pbrShadingPropsBuffer._allocation);
			vmaDestroyBuffer(_allocator, _frames[i].cascadeViewProjsBuffer._buffer, _frames[i].cascadeViewProjsBuffer._allocation);
			vmaDestroyBuffer(_allocator, _frames[i].objectBuffer._buffer, _frames[i].objectBuffer._allocation);
			vmaDestroyBuffer(_allocator, _frames[i].pickingSelectedIdBuffer._buffer, _frames[i].pickingSelectedIdBuffer._allocation);
		}
		});
}

void VulkanEngine::initPipelines()
{
	//
	// Load shader modules
	//
	VkShaderModule defaultLitVertShader,
					defaultLitFragShader;
	loadShaderModule("shader/pbr.vert.spv", &defaultLitVertShader);
	loadShaderModule("shader/pbr_khr.frag.spv", &defaultLitFragShader);

	VkShaderModule skyboxVertShader,
					skyboxFragShader;
	loadShaderModule("shader/skybox.vert.spv", &skyboxVertShader);
	loadShaderModule("shader/skybox.frag.spv", &skyboxFragShader);

	VkShaderModule pickingVertShader,
					pickingFragShader;
	loadShaderModule("shader/picking.vert.spv", &pickingVertShader);
	loadShaderModule("shader/picking.frag.spv", &pickingFragShader);

	VkShaderModule wireframeColorVertShader,
					wireframeColorFragShader;
	loadShaderModule("shader/pbr.vert.spv", &wireframeColorVertShader);
	loadShaderModule("shader/color.frag.spv", &wireframeColorFragShader);

	VkShaderModule debugPhysicsObjectVertShader,
					debugPhysicsObjectFragShader;
	loadShaderModule("shader/debug_physics_object.vert.spv", &debugPhysicsObjectVertShader);
	loadShaderModule("shader/debug_physics_object.frag.spv", &debugPhysicsObjectFragShader);

	VkShaderModule shadowDepthPassVertShader,
					shadowDepthPassFragShader;
	loadShaderModule("shader/shadow_depthpass.vert.spv", &shadowDepthPassVertShader);
	loadShaderModule("shader/shadow_depthpass.frag.spv", &shadowDepthPassFragShader);

	//
	// Mesh Pipeline
	//
	VkPipelineLayoutCreateInfo meshPipelineLayoutInfo = vkinit::pipelineLayoutCreateInfo();

	VkPushConstantRange pushConstant = {
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		.offset = 0,
		.size = sizeof(PBRMaterialPushConstBlock)
	};
	meshPipelineLayoutInfo.pPushConstantRanges = &pushConstant;
	meshPipelineLayoutInfo.pushConstantRangeCount = 1;

	VkDescriptorSetLayout setLayouts[] = { _globalSetLayout, _objectSetLayout, _skeletalAnimationSetLayout, _pbrTexturesSetLayout };
	meshPipelineLayoutInfo.pSetLayouts = setLayouts;
	meshPipelineLayoutInfo.setLayoutCount = 4;

	VkPipelineLayout _meshPipelineLayout;
	VK_CHECK(vkCreatePipelineLayout(_device, &meshPipelineLayoutInfo, nullptr, &_meshPipelineLayout));

	vkglTF::VertexInputDescription vertexDescription = vkglTF::Model::Vertex::getVertexDescription();

	PipelineBuilder pipelineBuilder;
	pipelineBuilder._shaderStages.clear();
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, defaultLitVertShader));
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, defaultLitFragShader));

	pipelineBuilder._vertexInputInfo = vkinit::vertexInputStateCreateInfo();
	pipelineBuilder._vertexInputInfo.pVertexAttributeDescriptions = vertexDescription.attributes.data();
	pipelineBuilder._vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexDescription.attributes.size());
	pipelineBuilder._vertexInputInfo.pVertexBindingDescriptions = vertexDescription.bindings.data();
	pipelineBuilder._vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexDescription.bindings.size());

	pipelineBuilder._inputAssembly = vkinit::inputAssemblyCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

	pipelineBuilder._viewport.x = 0.0f;
	pipelineBuilder._viewport.y = 0.0f;
	pipelineBuilder._viewport.width = (float_t)_windowExtent.width;
	pipelineBuilder._viewport.height = (float_t)_windowExtent.height;
	pipelineBuilder._viewport.minDepth = 0.0f;
	pipelineBuilder._viewport.maxDepth = 1.0f;

	pipelineBuilder._scissor.offset = { 0, 0 };
	pipelineBuilder._scissor.extent = _windowExtent;

	pipelineBuilder._rasterizer = vkinit::rasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT);
	pipelineBuilder._colorBlendAttachment.push_back(vkinit::colorBlendAttachmentState());
	pipelineBuilder._multisampling = vkinit::multisamplingStateCreateInfo();
	pipelineBuilder._pipelineLayout = _meshPipelineLayout;
	pipelineBuilder._depthStencil = vkinit::depthStencilCreateInfo(true, true, VK_COMPARE_OP_LESS_OR_EQUAL);
	pipelineBuilder._dynamicState = {    // @TODO: for now we'll have this, but make a "..." initializing function in the vkinit namespace, bc honestly you just need one param and then just make it an expanding list!
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = 0,
		.pDynamicStates = nullptr,
	};

	auto _meshPipeline = pipelineBuilder.buildPipeline(_device, _mainRenderPass);
	createMaterial(_meshPipeline, _meshPipelineLayout, "pbrMaterial");

	for (auto shaderStage : pipelineBuilder._shaderStages)
		vkDestroyShaderModule(_device, shaderStage.module, nullptr);

	//
	// Skybox pipeline
	//
	VkPipelineLayoutCreateInfo skyboxPipelineLayoutInfo = meshPipelineLayoutInfo;
	skyboxPipelineLayoutInfo.pPushConstantRanges = nullptr;
	skyboxPipelineLayoutInfo.pushConstantRangeCount = 0;

	VkDescriptorSetLayout setLayouts2[] = { _globalSetLayout, _singleTextureSetLayout };
	skyboxPipelineLayoutInfo.pSetLayouts = setLayouts2;
	skyboxPipelineLayoutInfo.setLayoutCount = 2;

	VkPipelineLayout _skyboxPipelineLayout;
	VK_CHECK(vkCreatePipelineLayout(_device, &skyboxPipelineLayoutInfo, nullptr, &_skyboxPipelineLayout));

	pipelineBuilder._shaderStages.clear();
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, skyboxVertShader));
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, skyboxFragShader));

	pipelineBuilder._rasterizer = vkinit::rasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_FRONT_BIT);    // Bc we're rendering a box inside-out
	pipelineBuilder._depthStencil = vkinit::depthStencilCreateInfo(false, false, VK_COMPARE_OP_NEVER);
	pipelineBuilder._pipelineLayout = _skyboxPipelineLayout;		// @NOTE: EFFING DON'T FORGET THIS LINE BC THAT'S WHAT CAUSED ME A BUTT TON OF GRIEF!!!!!

	auto _skyboxPipeline = pipelineBuilder.buildPipeline(_device, _mainRenderPass);
	createMaterial(_skyboxPipeline, _skyboxPipelineLayout, "skyboxMaterial");

	for (auto shaderStage : pipelineBuilder._shaderStages)
		vkDestroyShaderModule(_device, shaderStage.module, nullptr);

	//
	// Picking pipeline
	//
	VkPipelineLayoutCreateInfo pickingPipelineLayoutInfo = skyboxPipelineLayoutInfo;
	pickingPipelineLayoutInfo.pPushConstantRanges = nullptr;
	pickingPipelineLayoutInfo.pushConstantRangeCount = 0;

	VkDescriptorSetLayout setLayouts3[] = { _globalSetLayout, _objectSetLayout, _skeletalAnimationSetLayout, _pickingReturnValueSetLayout };
	pickingPipelineLayoutInfo.pSetLayouts = setLayouts3;
	pickingPipelineLayoutInfo.setLayoutCount = 4;

	VkPipelineLayout _pickingPipelineLayout;
	VK_CHECK(vkCreatePipelineLayout(_device, &pickingPipelineLayoutInfo, nullptr, &_pickingPipelineLayout));

	pipelineBuilder._shaderStages.clear();
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, pickingVertShader));
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, pickingFragShader));

	pipelineBuilder._rasterizer = vkinit::rasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT);    // Bc we're rendering a box inside-out
	pipelineBuilder._depthStencil = vkinit::depthStencilCreateInfo(true, true, VK_COMPARE_OP_LESS_OR_EQUAL);
	pipelineBuilder._pipelineLayout = _pickingPipelineLayout;

	std::array<VkDynamicState, 1> states = { VK_DYNAMIC_STATE_SCISSOR };		// We're using a dynamic scissor here so that we can just render a 1x1 pixel and read from it using an ssbo for picking
	VkPipelineDynamicStateCreateInfo dynamicState = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = static_cast<uint32_t>(states.size()),
		.pDynamicStates = states.data(),
	};
	pipelineBuilder._dynamicState = dynamicState;

	auto _pickingPipeline = pipelineBuilder.buildPipeline(_device, _pickingRenderPass);    // @NOTE: the changed renderpass bc this is for picking
	createMaterial(_pickingPipeline, _pickingPipelineLayout, "pickingMaterial");

	for (auto shaderStage : pipelineBuilder._shaderStages)
		vkDestroyShaderModule(_device, shaderStage.module, nullptr);

	//
	// Wireframe color pipeline
	//
	VkPipelineLayoutCreateInfo wireframeColorPipelineLayoutInfo = pickingPipelineLayoutInfo;

	pushConstant = {
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		.offset = 0,
		.size = sizeof(ColorPushConstBlock)
	};
	wireframeColorPipelineLayoutInfo.pPushConstantRanges = &pushConstant;
	wireframeColorPipelineLayoutInfo.pushConstantRangeCount = 1;

	VkDescriptorSetLayout setLayouts4[] = { _globalSetLayout, _objectSetLayout, _skeletalAnimationSetLayout };
	wireframeColorPipelineLayoutInfo.pSetLayouts = setLayouts4;
	wireframeColorPipelineLayoutInfo.setLayoutCount = 3;

	VkPipelineLayout _wireframeColorPipelineLayout;
	VK_CHECK(vkCreatePipelineLayout(_device, &wireframeColorPipelineLayoutInfo, nullptr, &_wireframeColorPipelineLayout));

	pipelineBuilder._shaderStages.clear();
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, wireframeColorVertShader));
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, wireframeColorFragShader));

	pipelineBuilder._rasterizer = vkinit::rasterizationStateCreateInfo(VK_POLYGON_MODE_LINE, VK_CULL_MODE_BACK_BIT);    // Bc we're rendering a box inside-out
	pipelineBuilder._depthStencil = vkinit::depthStencilCreateInfo(true, true, VK_COMPARE_OP_LESS_OR_EQUAL);
	pipelineBuilder._pipelineLayout = _wireframeColorPipelineLayout;

	VkPipelineDynamicStateCreateInfo noDynamicState = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = 0,
		.pDynamicStates = nullptr,
	};
	pipelineBuilder._dynamicState = noDynamicState;    // Turn off dynamic states

	auto _wireframeColorPipeline = pipelineBuilder.buildPipeline(_device, _mainRenderPass);
	createMaterial(_wireframeColorPipeline, _wireframeColorPipelineLayout, "wireframeColorMaterial");

	pipelineBuilder._depthStencil = vkinit::depthStencilCreateInfo(true, false, VK_COMPARE_OP_GREATER);
	auto _wireframeColorBehindPipeline = pipelineBuilder.buildPipeline(_device, _mainRenderPass);
	createMaterial(_wireframeColorBehindPipeline, _wireframeColorPipelineLayout, "wireframeColorBehindMaterial");

	for (auto shaderStage : pipelineBuilder._shaderStages)
		vkDestroyShaderModule(_device, shaderStage.module, nullptr);

	//
	// Debug Physics Object pipeline
	//
	VkPipelineLayoutCreateInfo debugPhysicsObjectPipelineLayoutInfo = wireframeColorPipelineLayoutInfo;

	debugPhysicsObjectPipelineLayoutInfo.pPushConstantRanges = nullptr;
	debugPhysicsObjectPipelineLayoutInfo.pushConstantRangeCount = 0;

	VkDescriptorSetLayout setLayouts5[] = { _globalSetLayout, _objectSetLayout };
	debugPhysicsObjectPipelineLayoutInfo.pSetLayouts = setLayouts5;
	debugPhysicsObjectPipelineLayoutInfo.setLayoutCount = 2;

	VkPipelineLayout _debugPhysicsObjectPipelineLayout;
	VK_CHECK(vkCreatePipelineLayout(_device, &debugPhysicsObjectPipelineLayoutInfo, nullptr, &_debugPhysicsObjectPipelineLayout));

	auto attributes = PhysicsEngine::getInstance().getVertexAttributeDescriptions();
	auto bindings = PhysicsEngine::getInstance().getVertexBindingDescriptions();

	pipelineBuilder._shaderStages.clear();
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, debugPhysicsObjectVertShader));
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, debugPhysicsObjectFragShader));

	pipelineBuilder._vertexInputInfo = vkinit::vertexInputStateCreateInfo();
	pipelineBuilder._vertexInputInfo.pVertexAttributeDescriptions = attributes.data();
	pipelineBuilder._vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
	pipelineBuilder._vertexInputInfo.pVertexBindingDescriptions = bindings.data();
	pipelineBuilder._vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());

	pipelineBuilder._inputAssembly = vkinit::inputAssemblyCreateInfo(VK_PRIMITIVE_TOPOLOGY_LINE_LIST);

	pipelineBuilder._rasterizer = vkinit::rasterizationStateCreateInfo(VK_POLYGON_MODE_LINE, VK_CULL_MODE_NONE);
	pipelineBuilder._depthStencil = vkinit::depthStencilCreateInfo(false, false, VK_COMPARE_OP_NEVER);
	pipelineBuilder._pipelineLayout = _debugPhysicsObjectPipelineLayout;

	auto _debugPhysicsObjectPipeline = pipelineBuilder.buildPipeline(_device, _mainRenderPass);
	createMaterial(_debugPhysicsObjectPipeline, _debugPhysicsObjectPipelineLayout, "debugPhysicsObjectMaterial");

	for (auto shaderStage : pipelineBuilder._shaderStages)
		vkDestroyShaderModule(_device, shaderStage.module, nullptr);

	//
	// Shadow Depth Pass pipeline
	//
	VkPipelineLayoutCreateInfo shadowDepthPassPipelineLayoutInfo = debugPhysicsObjectPipelineLayoutInfo;

	pushConstant = {
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
		.offset = 0,
		.size = sizeof(CascadeIndexPushConstBlock)
	};
	shadowDepthPassPipelineLayoutInfo.pPushConstantRanges = &pushConstant;
	shadowDepthPassPipelineLayoutInfo.pushConstantRangeCount = 1;

	VkDescriptorSetLayout setLayouts6[] = { _cascadeViewProjsSetLayout, _objectSetLayout, _skeletalAnimationSetLayout, _pbrTexturesSetLayout };
	shadowDepthPassPipelineLayoutInfo.pSetLayouts = setLayouts6;
	shadowDepthPassPipelineLayoutInfo.setLayoutCount = 4;

	VkPipelineLayout _shadowDepthPassPipelineLayout;
	VK_CHECK(vkCreatePipelineLayout(_device, &shadowDepthPassPipelineLayoutInfo, nullptr, &_shadowDepthPassPipelineLayout));

	pipelineBuilder._shaderStages.clear();
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, shadowDepthPassVertShader));
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, shadowDepthPassFragShader));

	// Revert back to the vkglTF traditional tri-mesh models for the vertex input
	pipelineBuilder._vertexInputInfo = vkinit::vertexInputStateCreateInfo();
	pipelineBuilder._vertexInputInfo.pVertexAttributeDescriptions = vertexDescription.attributes.data();
	pipelineBuilder._vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexDescription.attributes.size());
	pipelineBuilder._vertexInputInfo.pVertexBindingDescriptions = vertexDescription.bindings.data();
	pipelineBuilder._vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexDescription.bindings.size());

	pipelineBuilder._inputAssembly = vkinit::inputAssemblyCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

	pipelineBuilder._viewport.x = 0.0f;
	pipelineBuilder._viewport.y = 0.0f;
	pipelineBuilder._viewport.width = (float_t)SHADOWMAP_DIMENSION;
	pipelineBuilder._viewport.height = (float_t)SHADOWMAP_DIMENSION;
	pipelineBuilder._viewport.minDepth = 0.0f;
	pipelineBuilder._viewport.maxDepth = 1.0f;

	pipelineBuilder._scissor.offset = { 0, 0 };
	pipelineBuilder._scissor.extent = VkExtent2D{ SHADOWMAP_DIMENSION, SHADOWMAP_DIMENSION };

	pipelineBuilder._rasterizer = vkinit::rasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE);
	pipelineBuilder._rasterizer.depthClampEnable = VK_TRUE;
	pipelineBuilder._colorBlendAttachment.clear();  // No color attachment for this pipeline
	pipelineBuilder._depthStencil = vkinit::depthStencilCreateInfo(true, true, VK_COMPARE_OP_LESS_OR_EQUAL);
	pipelineBuilder._pipelineLayout = _shadowDepthPassPipelineLayout;

	auto _shadowDepthPassPipeline = pipelineBuilder.buildPipeline(_device, _shadowRenderPass);
	createMaterial(_shadowDepthPassPipeline, _shadowDepthPassPipelineLayout, "shadowDepthPassMaterial");

	for (auto shaderStage : pipelineBuilder._shaderStages)
		vkDestroyShaderModule(_device, shaderStage.module, nullptr);

	// Add destroy command for cleanup
	_swapchainDependentDeletionQueue.pushFunction([=]() {
		vkDestroyPipeline(_device, _meshPipeline, nullptr);
		vkDestroyPipelineLayout(_device, _meshPipelineLayout, nullptr);    // @NOTE: pipelinelayouts don't have to get destroyed... but since we're not saving them anywhere, we're just destroying and recreating them anyway
		vkDestroyPipeline(_device, _skyboxPipeline, nullptr);
		vkDestroyPipelineLayout(_device, _skyboxPipelineLayout, nullptr);
		vkDestroyPipeline(_device, _pickingPipeline, nullptr);
		vkDestroyPipelineLayout(_device, _pickingPipelineLayout, nullptr);
		vkDestroyPipeline(_device, _wireframeColorPipeline, nullptr);
		vkDestroyPipeline(_device, _wireframeColorBehindPipeline, nullptr);
		vkDestroyPipelineLayout(_device, _wireframeColorPipelineLayout, nullptr);
		vkDestroyPipeline(_device, _debugPhysicsObjectPipeline, nullptr);
		vkDestroyPipelineLayout(_device, _debugPhysicsObjectPipelineLayout, nullptr);
		vkDestroyPipeline(_device, _shadowDepthPassPipeline, nullptr);
		vkDestroyPipelineLayout(_device, _shadowDepthPassPipelineLayout, nullptr);
		});
}

void VulkanEngine::initScene()    // @TODO: rename this to something better, bc all it's doing is allocating image descriptorsets... or even better is including this in maybe loadImages()?  -Timo 2022/10/24
{
	//
	// Update defaultMaterial		@TODO: @FIXME: likely we should just have the material get updated with the textures on pipeline creation, not here... plus pipelines are recreated when the screen resizes too so it should be done then.
	//
	Material* texturedMaterial = getMaterial("pbrMaterial");
	VkDescriptorSetAllocateInfo allocInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.pNext = nullptr,
		.descriptorPool = _descriptorPool,
		.descriptorSetCount = 1,
		.pSetLayouts = &_singleTextureSetLayout,
	};
	vkAllocateDescriptorSets(_device, &allocInfo, &texturedMaterial->textureSet);

	VkDescriptorImageInfo imageBufferInfo = {
		.sampler = _loadedTextures["WoodFloor057"].sampler,
		.imageView = _loadedTextures["WoodFloor057"].imageView,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
	VkWriteDescriptorSet texture1 =
		vkinit::writeDescriptorImage(
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			texturedMaterial->textureSet,
			&imageBufferInfo,
			0
		);
	vkUpdateDescriptorSets(_device, 1, &texture1, 0, nullptr);

	//
	// Update cubemapskybox material
	//
	Material& skyboxMaterial = *getMaterial("skyboxMaterial");
	vkAllocateDescriptorSets(_device, &allocInfo, &skyboxMaterial.textureSet);

	imageBufferInfo.sampler = _loadedTextures["CubemapSkybox"].sampler;
	imageBufferInfo.imageView = _loadedTextures["CubemapSkybox"].imageView;
	texture1 =
		vkinit::writeDescriptorImage(
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			skyboxMaterial.textureSet,
			&imageBufferInfo,
			0
		);
	vkUpdateDescriptorSets(_device, 1, &texture1, 0, nullptr);

	//
	// Materials for ImGui
	//
	_imguiData.textureLayerVisible   = ImGui_ImplVulkan_AddTexture(_loadedTextures["imguiTextureLayerVisible"].sampler, _loadedTextures["imguiTextureLayerVisible"].imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	_imguiData.textureLayerInvisible = ImGui_ImplVulkan_AddTexture(_loadedTextures["imguiTextureLayerInvisible"].sampler, _loadedTextures["imguiTextureLayerInvisible"].imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	_imguiData.textureLayerBuilder   = ImGui_ImplVulkan_AddTexture(_loadedTextures["imguiTextureLayerBuilder"].sampler, _loadedTextures["imguiTextureLayerBuilder"].imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	_imguiData.textureLayerCollision = ImGui_ImplVulkan_AddTexture(_loadedTextures["imguiTextureLayerCollision"].sampler, _loadedTextures["imguiTextureLayerCollision"].imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void VulkanEngine::generatePBRCubemaps()
{
	//
	// @NOTE: this function was copied and very slightly modified from Sascha Willem's Vulkan-glTF-PBR example.
	//

	//
	// Offline generation for the cubemaps used for PBR lighting
	// - Irradiance cube map
	// - Pre-filterd environment cubemap
	//
	enum Target
	{
		IRRADIANCE = 0,
		PREFILTEREDENV = 1,
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
		case IRRADIANCE:
			format = VK_FORMAT_R32G32B32A32_SFLOAT;
			dim = 64;
			break;
		case PREFILTEREDENV:
			format = VK_FORMAT_R16G16B16A16_SFLOAT;
			dim = 512;
			break;
		};

		const uint32_t numMips = static_cast<uint32_t>(floor(log2(dim))) + 1;

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

		// @HACK: Get the environment cubemap! (NOTE: not as much of a hack as before, however)
		VkDescriptorImageInfo environmentCubemapBufferInfo = {
			.sampler = _loadedTextures["CubemapSkybox"].sampler,
			.imageView = _loadedTextures["CubemapSkybox"].imageView,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};

		// Descriptor sets
		VkDescriptorSet descriptorset;
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

		struct PushBlockIrradiance
		{
			glm::mat4 mvp;
			float_t deltaPhi = (2.0f * float_t(M_PI)) / 180.0f;
			float_t deltaTheta = (0.5f * float_t(M_PI)) / 64.0f;
		} pushBlockIrradiance;

		struct PushBlockPrefilterEnv
		{
			glm::mat4 mvp;
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
		loadShaderModule("shader/filtercube.vert.spv", &filtercubeVertShader);
		switch (target)
		{
		case IRRADIANCE:
			loadShaderModule("shader/irradiancecube.frag.spv", &filtercubeFragShader);
			break;
		case PREFILTEREDENV:
			loadShaderModule("shader/prefilterenvmap.frag.spv", &filtercubeFragShader);
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

		std::vector<glm::mat4> matrices = {
			glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
			glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
			glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
			glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
			glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
			glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
		};

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
					switch (target) {
					case IRRADIANCE:
						pushBlockIrradiance.mvp = glm::perspective((float_t)(M_PI / 2.0), 1.0f, 0.1f, 512.0f) * matrices[f];
						vkCmdPushConstants(cmd, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushBlockIrradiance), &pushBlockIrradiance);
						break;
					case PREFILTEREDENV:
						pushBlockPrefilterEnv.mvp = glm::perspective((float_t)(M_PI / 2.0), 1.0f, 0.1f, 512.0f) * matrices[f];
						pushBlockPrefilterEnv.roughness = (float_t)m / (float_t)(numMips - 1);
						vkCmdPushConstants(cmd, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushBlockPrefilterEnv), &pushBlockPrefilterEnv);
						break;
					};

					vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
					vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelinelayout, 0, 1, &descriptorset, 0, NULL);

					VkDeviceSize offsets[1] = { 0 };

					auto skybox = _roManager->getModel("cube");
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
	loadShaderModule("shader/genbrdflut.vert.spv", &genBrdfLUTVertShader);
	loadShaderModule("shader/genbrdflut.frag.spv", &genBrdfLUTFragShader);

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

void VulkanEngine::attachPBRDescriptors()
{
	//
	// @NOTE: the descriptor set for the _globalDescriptor (which holds the PBR descriptors)
	// is already allocated, at this point you just need to write the imagedescriptors to
	// the combined sampled texture bind point
	//
	VkDescriptorImageInfo irradianceDescriptor = {
		.sampler = _pbrSceneTextureSet.irradianceCubemap.sampler,
		.imageView = _pbrSceneTextureSet.irradianceCubemap.imageView,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
	VkDescriptorImageInfo prefilteredDescriptor = {
		.sampler = _pbrSceneTextureSet.prefilteredCubemap.sampler,
		.imageView = _pbrSceneTextureSet.prefilteredCubemap.imageView,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
	VkDescriptorImageInfo brdfLUTDescriptor = {
		.sampler = _pbrSceneTextureSet.brdfLUTTexture.sampler,
		.imageView = _pbrSceneTextureSet.brdfLUTTexture.imageView,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
	VkDescriptorImageInfo shadowMapDescriptor = {
		.sampler = _pbrSceneTextureSet.shadowMap.sampler,
		.imageView = _pbrSceneTextureSet.shadowMap.imageView,
		.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
	};

	// Write the descriptors for the images to the globaldescriptor
	for (size_t i = 0; i < FRAME_OVERLAP; i++)
	{
		std::array<VkWriteDescriptorSet, 4> writeDescriptorSets = {
			vkinit::writeDescriptorImage(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, _frames[i].globalDescriptor, &irradianceDescriptor, 2),
			vkinit::writeDescriptorImage(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, _frames[i].globalDescriptor, &prefilteredDescriptor, 3),
			vkinit::writeDescriptorImage(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, _frames[i].globalDescriptor, &brdfLUTDescriptor, 4),
			vkinit::writeDescriptorImage(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, _frames[i].globalDescriptor, &shadowMapDescriptor, 5),
		};
		vkUpdateDescriptorSets(_device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
	}
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
	ImGui_ImplVulkan_Init(&initInfo, _mainRenderPass);

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

	_swapchainDependentDeletionQueue.flush();

	initSwapchain();
	initShadowRenderpass();
	initDefaultRenderpass();
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

bool VulkanEngine::loadShaderModule(const char* filePath, VkShaderModule* outShaderModule)
{
	std::cout << "[LOAD SHADER MODULE]" << std::endl;

	// Open SPIRV file
	std::ifstream file(filePath, std::ios::ate | std::ios::binary);
	if (!file.is_open())
	{
		std::cerr << "ERROR: could not open file " << filePath << std::endl;
		return false;
	}

	//
	// Get the filesize and copy the whole thing into the correct sized buffer
	//
	size_t filesize = (size_t)file.tellg();
	std::vector<uint32_t> buffer(filesize / sizeof(uint32_t));
	file.seekg(0);
	file.read((char*)buffer.data(), filesize);
	file.close();

	//
	// Load the shader into Vulkan
	//
	VkShaderModuleCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.pNext = nullptr;
	createInfo.codeSize = buffer.size() * sizeof(uint32_t);
	createInfo.pCode = buffer.data();

	// Error check
	VkShaderModule shaderModule;
	if (vkCreateShaderModule(_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
	{
		std::cerr << "ERROR: could not create shader module for shader file " << filePath << std::endl;
		return false;
	}

	// Successful shader creation!
	*outShaderModule = shaderModule;

	std::cout << "Successfully created shader module for shader file " << filePath << std::endl;
	return true;
}

void VulkanEngine::loadMeshes()
{
	tf::Executor e;
	tf::Taskflow taskflow;

	vkglTF::Model
		*cube,
		*slimeGirl;
	taskflow.emplace(
		[&]() { cube = new vkglTF::Model(); cube->loadFromFile(this, "res/models/Box.gltf"); },
		[&]() { slimeGirl = new vkglTF::Model(); slimeGirl->loadFromFile(this, "res/models/SlimeGirl.glb"); }
	);
	e.run(taskflow).wait();

	_roManager->createModel(cube, "cube");
	_roManager->createModel(slimeGirl, "slimeGirl");
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
	// Fill in object data
	//
	void* objectData;
	vmaMapMemory(_allocator, currentFrame.objectBuffer._allocation, &objectData);
	GPUObjectData* objectSSBO = (GPUObjectData*)objectData;    // @IMPROVE: perhaps multithread this? Or only update when the object moves?

	for (size_t i = 0; i < _roManager->_renderObjects.size(); i++)
	{
		RenderObject& object = _roManager->_renderObjects[i];
		objectSSBO[i].modelMatrix = object.transformMatrix;		// Another evil pointer trick I love... call me Dmitri the Evil
	}

	vmaUnmapMemory(_allocator, currentFrame.objectBuffer._allocation);
}

void VulkanEngine::renderRenderObjects(VkCommandBuffer cmd, const FrameData& currentFrame, size_t offset, size_t count, bool renderSkybox, bool materialOverride, VkPipelineLayout* overrideLayout, bool injectColorMapIntoMaterialOverride)
{
	//
	// Render Skybox
	// @TODO: fix this weird organization!!!
	//
	if (renderSkybox)
	{
		Material& skyboxMaterial = *getMaterial("skyboxMaterial");
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxMaterial.pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxMaterial.pipelineLayout, 0, 1, &currentFrame.globalDescriptor, 0, nullptr);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxMaterial.pipelineLayout, 1, 1, &skyboxMaterial.textureSet, 0, nullptr);

		auto skybox = _roManager->getModel("cube");
		skybox->bind(cmd);
		skybox->draw(cmd);
	}

	//
	// Render all the renderobjects
	//
	Material& defaultMaterial = *getMaterial("pbrMaterial");    // @HACK: @TODO: currently, the way that the pipeline is getting used is by just hardcode using it in the draw commands for models... however, each model should get its pipeline set to this material instead (or whatever material its using... that's why we can't hardcode stuff!!!)   @TODO: create some kind of way to propagate the newly created pipeline to the primMat (calculated material in the gltf model) instead of using defaultMaterial directly.  -Timo
	vkglTF::Model* lastModel = nullptr;
	Material* lastMaterial = nullptr;
	VkDescriptorSet* lastJointDescriptor = nullptr;

	for (size_t i = offset; i < offset + count; i++)
	{
		RenderObject& object = _roManager->_renderObjects[i];

		if (!_roManager->_renderObjectLayersEnabled[(size_t)object.renderLayer])
			continue;    // Ignore layers that are disabled

		if (!object.model)	// @NOTE: Subdue a warning that a possible nullptr could be dereferenced
		{
			std::cerr << "ERROR: object model are NULL" << std::endl;
			continue;
		}

		if (object.model != lastModel)
		{
			// Bind the new mesh
			object.model->bind(cmd);
			lastModel = object.model;
		}

		//
		// Render it out
		//
		object.model->draw(cmd, (uint32_t)i,
			[&](vkglTF::Primitive* primitive, vkglTF::Node* node) {
				//
				// Apply all of the material properties
				//
				if (!materialOverride)
				{
					vkglTF::PBRMaterial& pbr = primitive->material;
					Material& primMat = pbr.calculatedMaterial;
			
					if (lastMaterial != &primMat)
					{
						// Bind new material
						vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, defaultMaterial.pipeline);
						lastMaterial = &primMat;
			
						// Global data descriptor (set = 0)
						vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, defaultMaterial.pipelineLayout, 0, 1, &currentFrame.globalDescriptor, 0, nullptr);
			
						// Object data descriptor (set = 1)
						vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, defaultMaterial.pipelineLayout, 1, 1, &currentFrame.objectDescriptor, 0, nullptr);
			
						// PBR data descriptor    (set = 3)
						vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, defaultMaterial.pipelineLayout, 3, 1, &primMat.textureSet, 0, nullptr);
			
						// Undo flag for joint descriptor to force rebinding
						lastJointDescriptor = nullptr;
					}
			
					//
					// PBR Material push constant data
					//
					PBRMaterialPushConstBlock pc = {};
					pc.emissiveFactor = pbr.emissiveFactor;
					// To save push constant space, availabilty and texture coordinates set are combined
					// -1 = texture not used for this material, >= 0 texture used and index of texture coordinate set
					pc.colorTextureSet = pbr.baseColorTexture != nullptr ? pbr.texCoordSets.baseColor : -1;
					pc.normalTextureSet = pbr.normalTexture != nullptr ? pbr.texCoordSets.normal : -1;
					pc.occlusionTextureSet = pbr.occlusionTexture != nullptr ? pbr.texCoordSets.occlusion : -1;
					pc.emissiveTextureSet = pbr.emissiveTexture != nullptr ? pbr.texCoordSets.emissive : -1;
					pc.alphaMask = static_cast<float>(pbr.alphaMode == vkglTF::PBRMaterial::ALPHAMODE_MASK);
					pc.alphaMaskCutoff = pbr.alphaCutoff;
			
					// TODO: glTF specs states that metallic roughness should be preferred, even if specular glossiness is present
			
					if (pbr.pbrWorkflows.metallicRoughness)
					{
						// Metallic roughness workflow
						pc.workflow = static_cast<float>(PBR_WORKFLOW_METALLIC_ROUGHNESS);
						pc.baseColorFactor = pbr.baseColorFactor;
						pc.metallicFactor = pbr.metallicFactor;
						pc.roughnessFactor = pbr.roughnessFactor;
						pc.PhysicalDescriptorTextureSet = pbr.metallicRoughnessTexture != nullptr ? pbr.texCoordSets.metallicRoughness : -1;
						pc.colorTextureSet = pbr.baseColorTexture != nullptr ? pbr.texCoordSets.baseColor : -1;
					}
			
					if (pbr.pbrWorkflows.specularGlossiness)
					{
						// Specular glossiness workflow
						pc.workflow = static_cast<float>(PBR_WORKFLOW_SPECULAR_GLOSSINESS);
						pc.PhysicalDescriptorTextureSet = pbr.extension.specularGlossinessTexture != nullptr ? pbr.texCoordSets.specularGlossiness : -1;
						pc.colorTextureSet = pbr.extension.diffuseTexture != nullptr ? pbr.texCoordSets.baseColor : -1;
						pc.diffuseFactor = pbr.extension.diffuseFactor;
						pc.specularFactor = glm::vec4(pbr.extension.specularFactor, 1.0f);
					}
			
					vkCmdPushConstants(cmd, defaultMaterial.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PBRMaterialPushConstBlock), &pc);
				}
				else if (injectColorMapIntoMaterialOverride)
				{
					// PBR data descriptor    (set = 3)
					vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *overrideLayout, 3, 1, &primitive->material.calculatedMaterial.textureSet, 0, nullptr);
				}

				//
				// Apply joint properties
				//
				VkDescriptorSet* jointDescriptor = &node->mesh->uniformBuffer.descriptorSet;
				if (lastJointDescriptor != jointDescriptor)
				{
					// Joint Descriptor (set = 2) (i.e. skeletal animations)
					// 
					// @NOTE: this doesn't have to be bound every primitive. Every mesh will
					// have a single joint descriptor, hence having its own binding flag.
					vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, (materialOverride) ? *overrideLayout : defaultMaterial.pipelineLayout, 2, 1, jointDescriptor, 0, nullptr);
					lastJointDescriptor = jointDescriptor;
				}
			}
		);
	}
}

void VulkanEngine::renderPickedObject(VkCommandBuffer cmd, const FrameData& currentFrame)
{
	//
	// Try to find the picked object
	//
	bool found = false;
	size_t pickedROIndex = 0;

	for (size_t i = 0; i < _roManager->_renderObjects.size(); i++)
	{
		if (_movingMatrix.matrixToMove == &_roManager->_renderObjects[i].transformMatrix)
		{
			found = true;
			pickedROIndex = i;
			break;
		}
	}

	if (!found)
		return;

	//
	// Render it with the wireframe color pipeline
	//
	constexpr size_t numRenders = 2;
	std::string materialNames[numRenders] = {
		"wireframeColorMaterial",
		"wireframeColorBehindMaterial"
	};
	glm::vec4 materialColors[numRenders] = {
		glm::vec4(1, 0.25, 1, 1),
		glm::vec4(0.535, 0.13, 0.535, 1),
	};

	for (size_t i = 0; i < numRenders; i++)
	{
		Material& material = *getMaterial(materialNames[i]);

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, material.pipeline);

		// Global data descriptor             (set = 0)
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, material.pipelineLayout, 0, 1, &currentFrame.globalDescriptor, 0, nullptr);

		// Object data descriptor             (set = 1)
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, material.pipelineLayout, 1, 1, &currentFrame.objectDescriptor, 0, nullptr);

		// Push constants
		ColorPushConstBlock pc = {
			.color = materialColors[i],
		};	
		vkCmdPushConstants(cmd, material.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ColorPushConstBlock), &pc);

		renderRenderObjects(cmd, currentFrame, pickedROIndex, 1, false, true, &material.pipelineLayout, false);
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

void VulkanEngine::buildResourceList()
{
	std::vector<std::string> directories = {
		"res",
		"shader",
	};
	for (auto directory : directories)
		for (const auto& entry : std::filesystem::recursive_directory_iterator(directory))
		{
			// Add the resource if it should be watched
			const auto& path = entry.path();
			if (std::filesystem::is_directory(path))
				continue;		// Ignore directories
			if (!path.has_extension())
				continue;		// @NOTE: only allow resource files if they have an extension!  -Timo

			if (path.extension().compare(".spv") == 0 ||
				path.extension().compare(".log") == 0)
				continue;		// @NOTE: ignore compiled SPIRV shader files, logs

			ResourceToWatch resource = {
				.path = path,
				.lastWriteTime = std::filesystem::last_write_time(path),
			};
			resourcesToWatch.push_back(resource);

			// Compile glsl shader if corresponding .spv file isn't up to date
			const auto& ext = path.extension();
			if (ext.compare(".vert") == 0 ||
				ext.compare(".frag") == 0)		// @COPYPASTA
			{
				auto spvPath = path;
				spvPath += ".spv";

				if (!std::filesystem::exists(spvPath) ||
					std::filesystem::last_write_time(spvPath) <= std::filesystem::last_write_time(path))
				{
					glslToSPIRVHelper::compileGLSLShaderToSPIRV(path);
				}
			}
		}
}

void VulkanEngine::checkIfResourceUpdatedThenHotswapRoutine()
{
	for (auto& resource : resourcesToWatch)
	{
		try
		{
			const std::filesystem::file_time_type lastWriteTime = std::filesystem::last_write_time(resource.path);
			if (resource.lastWriteTime == lastWriteTime)
				continue;

			//
			// Reload the resource
			//
			std::cout << "[RELOAD HOTSWAPPABLE RESOURCE]" << std::endl
				<< "Name: " << resource.path << std::endl;
			resource.lastWriteTime = lastWriteTime;

			if (!resource.path.has_extension())
			{
				std::cerr << "ERROR: file " << resource.path << " has no extension!" << std::endl;
				continue;
			}

			//
			// Find the extension and execute appropriate routine
			//
			const auto& ext = resource.path.extension();
			if (ext.compare(".vert") == 0 ||
				ext.compare(".frag") == 0)
			{
				// Compile the shader (GLSL -> SPIRV)
				glslToSPIRVHelper::compileGLSLShaderToSPIRV(resource.path);

				// Trip reloading the shaders (recreate swapchain flag)
				_recreateSwapchain = true;
				std::cout << "Recompile shader to SPIRV and trigger swapchain recreation SUCCESS" << std::endl;
				continue;
			}

			// Nothing to do to the resource!
			// That means there's no routine for this certain resource
			std::cout << "WARNING: No routine for " << ext << " files!" << std::endl;
		}
		catch (...) { }   // Just continue on if you get the filesystem error
	}
}

void VulkanEngine::teardownResourceList()
{
	// @NOTE: nothing is around to tear down!
	//        There are just filesystem entries, so they only go until the lifetime
	//        of the VulkanEngine object, so we don't need to tear that down!
}
#endif

void VulkanEngine::submitSelectedRenderObjectId(int32_t id)
{
	if (id < 0)
	{
		// Nullify the matrixToMove pointer
		_movingMatrix.matrixToMove = nullptr;
		std::cout << "[PICKING]" << std::endl
			<< "Selected object nullified" << std::endl;
		return;
	}

	// Set a new matrixToMove
	_movingMatrix.matrixToMove = &_roManager->_renderObjects[id].transformMatrix;
	std::cout << "[PICKING]" << std::endl
		<< "Selected object " << id << std::endl;
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

	ImGui::ShowDemoWindow();

	ImPlot::ShowDemoWindow();

	//
	// Debug Messages window
	//
	debug::renderImguiDebugMessages(_windowExtent.width, deltaTime);

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
	ImGui::Begin((scene::currentLoadedScene + " Properties").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
	{
		ImGui::Text(scene::currentLoadedScene.c_str());

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
					for (auto& ent : _entityManager->_entities)
						_entityManager->destroyEntity(ent);
					_flagNextStepLoadThisPathAsAScene = path;  // @HACK: mireba wakaru... but it's needed bc it works when delaying the load by a step...  -Timo 2022/10/30
					ImGui::CloseCurrentPopup();
				}
			ImGui::EndPopup();
		}

		ImGui::SameLine();
		if (ImGui::Button("Save Scene"))
			scene::saveScene(scene::currentLoadedScene, _entityManager->_entities, this);

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
				scene::currentLoadedScene = saveSceneAsFname + ".ssdat";
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
	float_t accumulatedWindowHeight = 0.0f;
	constexpr float_t windowPadding = 8.0f;

	ImGui::SetNextWindowPos(ImVec2(0, accumulatedWindowHeight), ImGuiCond_Always);		// @NOTE: the ImGuiCond_Always means that this line will execute always, when set to once, this line will be ignored after the first time it's called
	ImGui::Begin("##Debug Statistics", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);
	{
		ImGui::Text((std::to_string(_debugStats.currentFPS) + " FPS").c_str());
		ImGui::Text((std::format("{:.2f}", _debugStats.renderTimesMS[_debugStats.renderTimesMSHeadIndex]) + "ms").c_str());
		ImGui::Text(("Frame : " + std::to_string(_frameNumber)).c_str());

		ImGui::Text(("Render Times :     [0, " + std::format("{:.2f}", _debugStats.highestRenderTime) + "]").c_str());
		ImGui::PlotHistogram("##Render Times Histogram", _debugStats.renderTimesMS, (int32_t)_debugStats.renderTimesMSCount, (int32_t)_debugStats.renderTimesMSHeadIndex, "", 0.0f, _debugStats.highestRenderTime, ImVec2(256, 24.0f));

		accumulatedWindowHeight += ImGui::GetWindowHeight() + windowPadding;
	}
	ImGui::End();

	//
	// PBR Shading Properties
	//
	ImGui::SetNextWindowPos(ImVec2(0, accumulatedWindowHeight), ImGuiCond_Always);
	ImGui::Begin("PBR Shading Properties", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
	{
		if (ImGui::DragFloat3("Light Direction", glm::value_ptr(_pbrRendering.gpuSceneShadingProps.lightDir)))
			_pbrRendering.gpuSceneShadingProps.lightDir = glm::normalize(_pbrRendering.gpuSceneShadingProps.lightDir);

		ImGui::DragFloat("Exposure", &_pbrRendering.gpuSceneShadingProps.exposure, 0.1f, 0.1f, 10.0f);
		ImGui::DragFloat("Gamma", &_pbrRendering.gpuSceneShadingProps.gamma, 0.1f, 0.1f, 4.0f);
		ImGui::DragFloat("IBL Strength", &_pbrRendering.gpuSceneShadingProps.scaleIBLAmbient, 0.1f, 0.0f, 2.0f);

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
				isLayerActive = _showCollisionDebugDraw;
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
						for (auto& ro : _roManager->_renderObjects)
						{
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
					_showCollisionDebugDraw = !_showCollisionDebugDraw;
					break;
				}

				// Assume toggle occurred (so if layer wasn't active)
				if (!isLayerActive)
					AudioEngine::getInstance().playSound(buttonTurnOnSfx[i]);
			}

			if ((int32_t)fmodf(i + 1, 3) != 0)
				ImGui::SameLine();
		}

		accumulatedWindowHeight += ImGui::GetWindowHeight() + windowPadding;
	}
	ImGui::End();

	//
	// Global Properties
	//
	ImGui::SetNextWindowPos(ImVec2(0, accumulatedWindowHeight), ImGuiCond_Always);
	ImGui::Begin("Global Properties", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
	{
		if (ImGui::CollapsingHeader("Camera Properties", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Text("NOTE: press F10 to change camera types");

			ImGui::SliderFloat("lookDistance", &_camera->mainCamMode.lookDistance, 1.0f, 100.0f);
			ImGui::DragFloat("focusRadius", &_camera->mainCamMode.focusRadius, 1.0f, 0.0f);
			ImGui::SliderFloat("focusCentering", &_camera->mainCamMode.focusCentering, 0.0f, 1.0f);
			ImGui::DragFloat3("focusPositionOffset", &_camera->mainCamMode.focusPositionOffset.x);
		}

		accumulatedWindowHeight += ImGui::GetWindowHeight() + windowPadding;
	}
	ImGui::End();

	//
	// Create Entity
	//
	ImGui::SetNextWindowPos(ImVec2(0, accumulatedWindowHeight), ImGuiCond_Always);
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
			for (auto& ro : _roManager->_renderObjects)
				if (ro.attachedEntityGuid == _flagAttachToThisEntity->getGUID())
					_movingMatrix.matrixToMove = &ro.transformMatrix;
			_flagAttachToThisEntity = nullptr;
		}

		if (ImGui::Button("Create!"))
		{
			auto newEnt = scene::spinupNewObject(listEntityTypes[(size_t)entityToCreateIndex], this, nullptr);
			_flagAttachToThisEntity = newEnt;  // @HACK: ... but if it works?
		}

		// Duplicate the selected entity
		Entity* selectedEntity = nullptr;
		for (auto& ro : _roManager->_renderObjects)
		{
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
			if (ImGui::Button("Duplicate Selected Entity"))
			{
				DataSerializer ds;
				selectedEntity->dump(ds);
				auto dsd = ds.getSerializedData();
				auto newEnt = scene::spinupNewObject(selectedEntity->getTypeName(), this, &dsd);
				_flagAttachToThisEntity = newEnt;
			}


		accumulatedWindowHeight += ImGui::GetWindowHeight() + windowPadding;
	}
	ImGui::End();

	//
	// Moving stuff around window (using ImGuizmo)
	//
	if (_movingMatrix.matrixToMove != nullptr)
	{
		if (input::keyDelPressed)
		{
			_movingMatrix.matrixToMove = nullptr;    // @NOTE: this is based off the assumption that likely if you're pressing delete while selecting an object, you're about to delete the object, so we need to dereference this instead of crashing!
		}
		else
		{
			//
			// Move the matrix via ImGuizmo
			//
			glm::mat4 projection = _camera->sceneCamera.gpuCameraData.projection;
			projection[1][1] *= -1.0f;

			static ImGuizmo::OPERATION manipulateOperation = ImGuizmo::OPERATION::TRANSLATE;
			static ImGuizmo::MODE manipulateMode           = ImGuizmo::MODE::WORLD;
			ImGuizmo::Manipulate(
				glm::value_ptr(_camera->sceneCamera.gpuCameraData.view),
				glm::value_ptr(projection),
				manipulateOperation,
				manipulateMode,
				glm::value_ptr(*_movingMatrix.matrixToMove)
			);

			//
			// Move the matrix via the cached matrix components
			//
			ImGui::SetNextWindowPos(ImVec2(0, accumulatedWindowHeight), ImGuiCond_Always);
			ImGui::Begin("Edit Selected", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
			{
				if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
				{
					glm::vec3 position, eulerAngles, scale;
					ImGuizmo::DecomposeMatrixToComponents(
						glm::value_ptr(*_movingMatrix.matrixToMove),
						glm::value_ptr(position),
						glm::value_ptr(eulerAngles),
						glm::value_ptr(scale)
					);

					bool changed = false;
					changed |= ImGui::DragFloat3("Pos##ASDFASDFASDFJAKSDFKASDHF", glm::value_ptr(position));
					changed |= ImGui::DragFloat3("Rot##ASDFASDFASDFJAKSDFKASDHF", glm::value_ptr(eulerAngles));
					changed |= ImGui::DragFloat3("Sca##ASDFASDFASDFJAKSDFKASDHF", glm::value_ptr(scale));

					if (changed)
					{
						// Recompose the matrix
						// @TODO: Figure out when to invalidate the cache bc the euler angles will reset!
						//        Or... maybe invalidating the cache isn't necessary for this window????
						ImGuizmo::RecomposeMatrixFromComponents(
							glm::value_ptr(position),
							glm::value_ptr(eulerAngles),
							glm::value_ptr(scale),
							glm::value_ptr(*_movingMatrix.matrixToMove)
						);
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
				if (!hasMouseButtonDown)
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
				for (auto& ro : _roManager->_renderObjects)
				{
					if (_movingMatrix.matrixToMove == &ro.transformMatrix)
					{
						foundRO = &ro;
						break;
					}
				}

				if (foundRO != nullptr)
				{
					if (ImGui::CollapsingHeader("Render Object", ImGuiTreeNodeFlags_DefaultOpen))
					{
						int32_t temp = (int32_t)foundRO->renderLayer;
						if (ImGui::Combo("Render Layer##asdfasdfasgasgcombo", &temp, "VISIBLE\0INVISIBLE\0BUILDER"))
							foundRO->renderLayer = RenderLayer(temp);
					}

					//
					// @TODO: see if you can't implement one for physics objects
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

						if (ImGui::CollapsingHeader(("Entity " + foundEnt->getGUID()).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
						{
							foundEnt->renderImGui();
						}
					}
				}

				accumulatedWindowHeight += ImGui::GetWindowHeight() + windowPadding;
			}
			ImGui::End();
		}
	}

	ImGui::Render();
}

VkPipeline PipelineBuilder::buildPipeline(VkDevice device, VkRenderPass pass)
{
	VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.pNext = nullptr;

	viewportState.viewportCount = 1;
	viewportState.pViewports = &_viewport;
	viewportState.scissorCount = 1;
	viewportState.pScissors = &_scissor;

	VkPipelineColorBlendStateCreateInfo colorBlending = {};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.pNext = nullptr;

	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.logicOp = VK_LOGIC_OP_COPY;
	colorBlending.attachmentCount = static_cast<uint32_t>(_colorBlendAttachment.size());
	colorBlending.pAttachments = _colorBlendAttachment.data();

	//
	// Build the actual pipeline
	//
	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.pNext = nullptr;

	pipelineInfo.stageCount = static_cast<uint32_t>(_shaderStages.size());
	pipelineInfo.pStages = _shaderStages.data();
	pipelineInfo.pVertexInputState = &_vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &_inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &_rasterizer;
	pipelineInfo.pMultisampleState = &_multisampling;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.layout = _pipelineLayout;
	pipelineInfo.renderPass = pass;
	pipelineInfo.subpass = 0;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
	pipelineInfo.pDepthStencilState = &_depthStencil;
	pipelineInfo.pDynamicState = &_dynamicState;

	//
	// Check for errors while creating gfx pipelines
	//
	VkPipeline newPipeline;
	if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline) != VK_SUCCESS)
	{
		std::cout << "FAILED: creating pipeline" << std::endl;
		return VK_NULL_HANDLE;
	}
	return newPipeline;
}
