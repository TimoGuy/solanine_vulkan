#include "VulkanEngine.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include "VkBootstrap.h"
#include "VkInitializers.h"
#include "VkTextures.h"
#include "GLSLToSPIRVHelper.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl.h"
#include "imgui/imgui_impl_vulkan.h"

// @NOTE: this is for creation of the VMA function definitions
#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>


#define VK_CHECK(x)                                                    \
	do                                                                 \
	{                                                                  \
		VkResult err = x;                                              \
		if (err)                                                       \
		{                                                              \
			std::cout <<"Detected Vulkan error: " << err << std::endl; \
			abort();                                                   \
		}                                                              \
	} while (0)


constexpr uint64_t TIMEOUT_1_SEC = 1000000000;

void VulkanEngine::init()
{
	SDL_Init(SDL_INIT_VIDEO);
	SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

	_window = SDL_CreateWindow(
		"Vulkan Engine",
		SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED,
		_windowExtent.width,
		_windowExtent.height,
		window_flags
	);

#ifdef _DEVELOP
	buildResourceList();
#endif

	initVulkan();
	initSwapchain();
	initCommands();
	initDefaultRenderpass();
	initFramebuffers();
	initSyncStructures();
	initDescriptors();
	initPipelines();
	loadMeshes();
	loadImages();
	initScene();
	initImgui();

	_isInitialized = true;
}

void VulkanEngine::run()
{
	SDL_Event e;
	bool isRunning = true;

	//
	// Main Loop
	//
	while (isRunning)
	{
#ifdef _DEVELOP
		checkIfResourceUpdatedThenHotswapRoutine();
#endif

		//
		// Poll events from the window
		//
		while (SDL_PollEvent(&e) != 0)
		{
			ImGui_ImplSDL2_ProcessEvent(&e);
			switch (e.type)
			{
			case SDL_QUIT:
			{
				isRunning = false;
				break;
			}

			case SDL_WINDOWEVENT:
			{
				if (e.window.event == SDL_WINDOWEVENT_RESIZED)
				{
					_windowExtentQueueup.width = (uint32_t)e.window.data1;
					_windowExtentQueueup.height = (uint32_t)e.window.data2;
				}
				break;
			}
			}
		}

		//
		// Render
		//
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplSDL2_NewFrame(_window);
		ImGui::NewFrame();

		ImGui::ShowDemoWindow();

		ImGui::Begin("mywindow");
		ImGui::Button("Hello");
		if (ImGui::TreeNode("Jojo me up"))
		{
			ImGui::Text("Hi there");
			ImGui::TreePop();
		}
		ImGui::End();
		
		ImGui::Render();

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

	// Wait until GPU finishes rendering the previous frame
	VK_CHECK(vkWaitForFences(_device, 1, &currentFrame.renderFence, true, TIMEOUT_1_SEC));
	VK_CHECK(vkResetFences(_device, 1, &currentFrame.renderFence));

	// Request image from swapchain
	uint32_t swapchainImageIndex;
	VkResult result = vkAcquireNextImageKHR(_device, _swapchain, TIMEOUT_1_SEC, currentFrame.presentSemaphore, nullptr, &swapchainImageIndex);
	if (result == VK_ERROR_OUT_OF_DATE_KHR)
	{
		recreateSwapchain();
		return;
	}
	else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
	{
		throw std::runtime_error("ERROR: failed to acquire swap chain image!");
	}

	// After commands finished executing, we can safely resume recording commands
	VK_CHECK(vkResetCommandBuffer(currentFrame.mainCommandBuffer, 0));

	//
	// Record commands into command buffer
	//
	VkCommandBuffer cmd = currentFrame.mainCommandBuffer;

	VkCommandBufferBeginInfo cmdBeginInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.pNext = nullptr,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		.pInheritanceInfo = nullptr,
	};

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	//
	// Execute Renderpass
	//
	VkClearValue clearValue;
	clearValue.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

	VkClearValue depthClear;
	depthClear.depthStencil.depth = 1.0f;

	VkClearValue clearValues[] = { clearValue, depthClear };

	VkRenderPassBeginInfo renderpassInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.pNext = nullptr,

		.renderPass = _renderPass,
		.framebuffer = _framebuffers[swapchainImageIndex],		// @NOTE: Framebuffer of the index the swapchain gave
		.renderArea = {
			.offset = {
				.x = 0,
				.y = 0,
			},
			.extent = _windowExtent,
		},

		.clearValueCount = 2,
		.pClearValues = &clearValues[0],
	};

	vkCmdBeginRenderPass(cmd, &renderpassInfo, VK_SUBPASS_CONTENTS_INLINE);

	renderRenderObjects(cmd, _renderObjects.data(), _renderObjects.size());

	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

	vkCmdEndRenderPass(cmd);
	VK_CHECK(vkEndCommandBuffer(cmd));

	//
	// Submit command buffer to gpu for execution
	//
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

	VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submit, currentFrame.renderFence));		// Submit work to gpu

	//
	// Submit the rendered frame to the screen
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
		recreateSwapchain();
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
	Texture woodFloor057;
	vkutil::loadImageFromFile(*this, "res/textures/WoodFloor057_1K-JPG/WoodFloor057_1K_Color.jpg", woodFloor057.image);

	VkImageViewCreateInfo imageInfo = vkinit::imageviewCreateInfo(VK_FORMAT_R8G8B8A8_SRGB, woodFloor057.image._image, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCreateImageView(_device, &imageInfo, nullptr, &woodFloor057.imageView);

	_mainDeletionQueue.pushFunction([=]() {
		vkDestroyImageView(_device, woodFloor057.imageView, nullptr);
		});

	_loadedTextures["WoodFloor057"] = woodFloor057;
}

Material* VulkanEngine::createMaterial(VkPipeline pipeline, VkPipelineLayout layout, const std::string& name)
{
	Material material = {
		.pipeline = pipeline,
		.pipelineLayout = layout,
	};
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

Mesh* VulkanEngine::getMesh(const std::string& name)
{
	auto it = _meshes.find(name);
	if (it == _meshes.end())
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
	std::cout << "DEVICE_NAME\t\t\t" << _gpuProperties.deviceName << std::endl;
	std::cout << "MINIMUM_BUFFER_ALIGNMENT\t" << _gpuProperties.limits.minUniformBufferOffsetAlignment << std::endl;
	std::cout << std::endl;
}

void VulkanEngine::initSwapchain()
{
	vkb::SwapchainBuilder swapchainBuilder{ _chosenGPU, _device, _surface };
	vkb::Swapchain vkbSwapchain = swapchainBuilder
		.use_default_format_selection()
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)		// @NOTE: this is "soft" v-sync, where it won't go above the monitor hertz, but it won't immediately go down to 1/2 the framerate if dips below.
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
	VkImageCreateInfo depthImgInfo = vkinit::imageCreateInfo(_depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthImgExtent);
	VmaAllocationCreateInfo depthImgAllocInfo = {
		.usage = VMA_MEMORY_USAGE_GPU_ONLY,
		.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
	};
	vmaCreateImage(_allocator, &depthImgInfo, &depthImgAllocInfo, &_depthImage._image, &_depthImage._allocation, nullptr);

	VkImageViewCreateInfo depthViewInfo = vkinit::imageviewCreateInfo(_depthFormat, _depthImage._image, VK_IMAGE_ASPECT_DEPTH_BIT);
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

		// Create command buffer
		VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::commandBufferAllocateInfo(_frames[i].commandPool, 1);
		VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_frames[i].mainCommandBuffer));

		// Add destroy command for cleanup
		_mainDeletionQueue.pushFunction([=]() {
			vkDestroyCommandPool(_device, _frames[i].commandPool, nullptr);
			});
	}

	//
	// Create Pool dfasdf;lkj
	//
	VkCommandPoolCreateInfo uploadCommandPoolInfo = vkinit::commandPoolCreateInfo(_graphicsQueueFamily);
	VK_CHECK(vkCreateCommandPool(_device, &uploadCommandPoolInfo, nullptr, &_uploadContext.commandPool));

	// Add destroy command for cleanup
	_mainDeletionQueue.pushFunction([=]() {
		vkDestroyCommandPool(_device, _uploadContext.commandPool, nullptr);
		});

	VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::commandBufferAllocateInfo(_uploadContext.commandPool, 1);
	VkCommandBuffer cmd;		// ?????????? @NOTE
	VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_uploadContext.commandBuffer));
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
	VkSubpassDependency dependencies[] = {colorDependency, depthDependency};
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

	VK_CHECK(vkCreateRenderPass(_device, &renderPassInfo, nullptr, &_renderPass));

	// Add destroy command for cleanup
	_mainDeletionQueue.pushFunction([=]() {
		vkDestroyRenderPass(_device, _renderPass, nullptr);
		});
}

void VulkanEngine::initFramebuffers()
{
	VkFramebufferCreateInfo fbInfo = {};
	fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fbInfo.pNext = nullptr;

	fbInfo.renderPass = _renderPass;
	fbInfo.attachmentCount = 2;
	fbInfo.width = _windowExtent.width;
	fbInfo.height = _windowExtent.height;
	fbInfo.layers = 1;

	const uint32_t swapchainImagecount = _swapchainImages.size();
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

		// Add destroy command for cleanup
		_mainDeletionQueue.pushFunction([=]() {
			vkDestroyFence(_device, _frames[i].renderFence, nullptr);
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

void VulkanEngine::initDescriptors()
{
	//
	// Create Descriptor Pool
	//
	std::vector<VkDescriptorPoolSize> sizes = {
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 10 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10 },
	};
	VkDescriptorPoolCreateInfo poolInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = 0,
		.maxSets = 10,
		.poolSizeCount = (uint32_t)sizes.size(),
		.pPoolSizes = sizes.data(),
	};
	vkCreateDescriptorPool(_device, &poolInfo, nullptr, &_descriptorPool);

	//
	// Create Descriptor Set Layout for camera buffers
	//
	VkDescriptorSetLayoutBinding cameraBufferBinding = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0);
	VkDescriptorSetLayoutBinding sceneBufferBinding = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 1);
	VkDescriptorSetLayoutBinding bindings[] = { cameraBufferBinding, sceneBufferBinding };
	VkDescriptorSetLayoutCreateInfo setInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.bindingCount = 2,
		.pBindings = bindings,
	};
	vkCreateDescriptorSetLayout(_device, &setInfo, nullptr, &_globalSetLayout);

	const size_t sceneParamBufferSize = FRAME_OVERLAP * padUniformBufferSize(sizeof(GPUSceneData));
	_sceneParameterBuffer = createBuffer(sceneParamBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

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
	// Create buffers
	//
	for (size_t i = 0; i < FRAME_OVERLAP; i++)
	{
		// Create buffers
		_frames[i].cameraBuffer = createBuffer(sizeof(GPUCameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
		
		const int MAX_OBJECTS = 10000;
		_frames[i].objectBuffer = createBuffer(sizeof(GPUObjectData) * MAX_OBJECTS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

		// Allocate descriptor sets
		VkDescriptorSetAllocateInfo allocInfo = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.pNext = nullptr,
			.descriptorPool = _descriptorPool,
			.descriptorSetCount = 1,
			.pSetLayouts = &_globalSetLayout,
		};
		vkAllocateDescriptorSets(_device, &allocInfo, &_frames[i].globalDescriptor);

		VkDescriptorSetAllocateInfo objectSetAllocInfo = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.pNext = nullptr,
			.descriptorPool = _descriptorPool,
			.descriptorSetCount = 1,
			.pSetLayouts = &_objectSetLayout,
		};
		vkAllocateDescriptorSets(_device, &objectSetAllocInfo, &_frames[i].objectDescriptor);

		// Point descriptor set to camera buffer
		VkDescriptorBufferInfo cameraInfo = {
			.buffer = _frames[i].cameraBuffer._buffer,
			.offset = 0,
			.range = sizeof(GPUCameraData),
		};
		VkDescriptorBufferInfo sceneInfo = {
			.buffer = _sceneParameterBuffer._buffer,
			.offset = 0,
			.range = sizeof(GPUSceneData),
		};
		VkDescriptorBufferInfo objectBufferInfo = {
			.buffer = _frames[i].objectBuffer._buffer,
			.offset = 0,
			.range = sizeof(GPUObjectData) * MAX_OBJECTS,
		};
		VkWriteDescriptorSet cameraWrite = vkinit::writeDescriptorBuffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, _frames[i].globalDescriptor, &cameraInfo, 0);
		VkWriteDescriptorSet sceneWrite = vkinit::writeDescriptorBuffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, _frames[i].globalDescriptor, &sceneInfo, 1);
		VkWriteDescriptorSet objectWrite = vkinit::writeDescriptorBuffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _frames[i].objectDescriptor, &objectBufferInfo, 0);
		VkWriteDescriptorSet setWrites[] = { cameraWrite, sceneWrite, objectWrite };
		vkUpdateDescriptorSets(_device, 3, setWrites, 0, nullptr);
	}

	// Add destroy command for cleanup
	_mainDeletionQueue.pushFunction([=]() {
		vmaDestroyBuffer(_allocator, _sceneParameterBuffer._buffer, _sceneParameterBuffer._allocation);
		vkDestroyDescriptorSetLayout(_device, _globalSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(_device, _objectSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(_device, _singleTextureSetLayout, nullptr);
		vkDestroyDescriptorPool(_device, _descriptorPool, nullptr);
		for (uint32_t i = 0; i < FRAME_OVERLAP; i++)
		{
			vmaDestroyBuffer(_allocator, _frames[i].cameraBuffer._buffer, _frames[i].cameraBuffer._allocation);
			vmaDestroyBuffer(_allocator, _frames[i].objectBuffer._buffer, _frames[i].objectBuffer._allocation);
		}
		});
}

void VulkanEngine::initPipelines()
{
	//
	// Load shader modules
	//
	VkShaderModule defaultLitVertShader;
	if (!loadShaderModule("shader/default_lit.vert.spv", &defaultLitVertShader))
	{
		std::cout << "ERROR: building default lit vert shader" << std::endl;
	}
	else
	{
		std::cout << "Default lit vert shader SUCCESS" << std::endl;
	}

	VkShaderModule defaultLitFragShader;
	if (!loadShaderModule("shader/default_lit.frag.spv", &defaultLitFragShader))
	{
		std::cout << "ERROR: building default lit frag shader" << std::endl;
	}
	else
	{
		std::cout << "Default lit frag shader SUCCESS" << std::endl;
	}

	//
	// Mesh Pipeline
	//
	VkPipelineLayoutCreateInfo meshPipelineLayoutInfo = vkinit::pipelineLayoutCreateInfo();

	VkPushConstantRange pushConstant = {
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
		.offset = 0,
		.size = sizeof(MeshPushConstants)
	};
	meshPipelineLayoutInfo.pPushConstantRanges = &pushConstant;
	meshPipelineLayoutInfo.pushConstantRangeCount = 1;

	VkDescriptorSetLayout setLayouts[] = { _globalSetLayout, _objectSetLayout, _singleTextureSetLayout };
	meshPipelineLayoutInfo.pSetLayouts = setLayouts;
	meshPipelineLayoutInfo.setLayoutCount = 3;

	VkPipelineLayout _meshPipelineLayout;
	VK_CHECK(vkCreatePipelineLayout(_device, &meshPipelineLayoutInfo, nullptr, &_meshPipelineLayout));

	VertexInputDescription vertexDescription = Vertex::getVertexDescription();

	PipelineBuilder pipelineBuilder;
	pipelineBuilder._shaderStages.clear();
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, defaultLitVertShader));
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, defaultLitFragShader));

	pipelineBuilder._vertexInputInfo = vkinit::vertexInputStateCreateInfo();
	pipelineBuilder._vertexInputInfo.pVertexAttributeDescriptions = vertexDescription.attributes.data();
	pipelineBuilder._vertexInputInfo.vertexAttributeDescriptionCount = vertexDescription.attributes.size();
	pipelineBuilder._vertexInputInfo.pVertexBindingDescriptions = vertexDescription.bindings.data();
	pipelineBuilder._vertexInputInfo.vertexBindingDescriptionCount= vertexDescription.bindings.size();

	pipelineBuilder._inputAssembly = vkinit::inputAssemblyCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

	pipelineBuilder._viewport.x = 0.0f;
	pipelineBuilder._viewport.y = 0.0f;
	pipelineBuilder._viewport.width = (float_t)_windowExtent.width;
	pipelineBuilder._viewport.height = (float_t)_windowExtent.height;
	pipelineBuilder._viewport.minDepth = 0.0f;
	pipelineBuilder._viewport.maxDepth = 1.0f;

	pipelineBuilder._scissor.offset = { 0, 0 };
	pipelineBuilder._scissor.extent = _windowExtent;

	pipelineBuilder._rasterizer = vkinit::rasterizationStateCreateInfo(VK_POLYGON_MODE_FILL);
	pipelineBuilder._colorBlendAttachment = vkinit::colorBlendAttachmentState();
	pipelineBuilder._multisampling = vkinit::multisamplingStateCreateInfo();
	pipelineBuilder._pipelineLayout = _meshPipelineLayout;
	pipelineBuilder._depthStencil = vkinit::depthStencilCreateInfo(true, true, VK_COMPARE_OP_LESS_OR_EQUAL);

	auto _meshPipeline = pipelineBuilder.buildPipeline(_device, _renderPass);
	createMaterial(_meshPipeline, _meshPipelineLayout, "defaultMaterial");

	//
	// Cleanup
	//
	vkDestroyShaderModule(_device, defaultLitVertShader, nullptr);
	vkDestroyShaderModule(_device, defaultLitFragShader, nullptr);

	// Add destroy command for cleanup
	_swapchainDependentDeletionQueue.pushFunction([=]() {
		vkDestroyPipeline(_device, _meshPipeline, nullptr);
		vkDestroyPipelineLayout(_device, _meshPipelineLayout, nullptr);
		});
	
}

void VulkanEngine::initScene()
{
	_renderObjects.clear();
	for (int x = -20; x <= 20; x++)
		for (int z = -20; z <= 20; z++)
		{
			glm::mat4 translation = glm::translate(glm::mat4(1.0f), glm::vec3(x, 0, z));
			glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(0.2f));

			RenderObject triangle = {
				.mesh = getMesh("triangle"),
				.material = getMaterial("defaultMaterial"),
				.transformMatrix = translation * scale,
			};
			_renderObjects.push_back(triangle);
		}

	//
	// Load in sampler for the texture
	//
	VkSamplerCreateInfo samplerInfo = vkinit::samplerCreateInfo(VK_FILTER_NEAREST);
	VkSampler wood057Sampler;
	vkCreateSampler(_device, &samplerInfo, nullptr, &wood057Sampler);

	_swapchainDependentDeletionQueue.pushFunction([=]() {
		vkDestroySampler(_device, wood057Sampler, nullptr);
		});

	Material* texturedMaterial = getMaterial("defaultMaterial");
	VkDescriptorSetAllocateInfo allocInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.pNext = nullptr,
		.descriptorPool = _descriptorPool,
		.descriptorSetCount = 1,
		.pSetLayouts = &_singleTextureSetLayout,
	};
	vkAllocateDescriptorSets(_device, &allocInfo, &texturedMaterial->textureSet);

	VkDescriptorImageInfo imageBufferInfo = {
		.sampler = wood057Sampler,
		.imageView = _loadedTextures["WoodFloor057"].imageView,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
	VkWriteDescriptorSet texture1 = vkinit::writeDescriptorImage(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, texturedMaterial->textureSet, &imageBufferInfo, 0);
	vkUpdateDescriptorSets(_device, 1, &texture1, 0, nullptr);
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
		.poolSizeCount = std::size(poolSizes),
		.pPoolSizes = poolSizes,
	};
	VkDescriptorPool imguiPool;
	VK_CHECK(vkCreateDescriptorPool(_device, &poolInfo, nullptr, &imguiPool));

	//
	// Init dear imgui
	//
	ImGui::CreateContext();
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
	ImGui_ImplVulkan_Init(&initInfo, _renderPass);

	// Load in imgui font textures
	immediateSubmit([&](VkCommandBuffer cmd) {
		ImGui_ImplVulkan_CreateFontsTexture(cmd);
		});
	ImGui_ImplVulkan_DestroyFontUploadObjects();

	// Add destroy command for cleanup
	_mainDeletionQueue.pushFunction([=]() {
		vkDestroyDescriptorPool(_device, imguiPool, nullptr);
		ImGui_ImplVulkan_Shutdown();
		});
}

void VulkanEngine::recreateSwapchain()
{
	vkDeviceWaitIdle(_device);

	_swapchainDependentDeletionQueue.flush();

	_windowExtent = _windowExtentQueueup;

	initSwapchain();
	initFramebuffers();
	initPipelines();
	initScene();
}

FrameData& VulkanEngine::getCurrentFrame()
{
	return _frames[_frameNumber % FRAME_OVERLAP];
}

bool VulkanEngine::loadShaderModule(const char* filePath, VkShaderModule* outShaderModule)
{
	// Open SPIRV file
	std::ifstream file(filePath, std::ios::ate | std::ios::binary);
	if (!file.is_open())
		return false;

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
		return false;
	}

	// Successful shader creation!
	*outShaderModule = shaderModule;
	return true;
}

void VulkanEngine::loadMeshes()
{
	Mesh _triangleMesh;

	//make the array 3 vertices long
	_triangleMesh._vertices.resize(3);

	//vertex positions
	_triangleMesh._vertices[0].position = { 1.f, 1.f, 0.0f };
	_triangleMesh._vertices[1].position = { -1.f, 1.f, 0.0f };
	_triangleMesh._vertices[2].position = { 0.f,-1.f, 0.0f };

	//vertex colors, all green
	_triangleMesh._vertices[0].color = { 0.f, 1.f, 0.0f }; //pure green
	_triangleMesh._vertices[1].color = { 0.f, 1.f, 0.0f }; //pure green
	_triangleMesh._vertices[2].color = { 0.f, 1.f, 0.0f }; //pure green

	//we don't care about the vertex normals

	// vertex uv
	_triangleMesh._vertices[0].uv = { 1.f, 0.f };
	_triangleMesh._vertices[1].uv = { 0.f, 0.f };
	_triangleMesh._vertices[2].uv = { 0.5f, 1.f };

	// Register mesh
	uploadMeshToGPU(_triangleMesh);
	_meshes["triangle"] = _triangleMesh;
}

void VulkanEngine::uploadMeshToGPU(Mesh& mesh)
{
	const size_t bufferSize = mesh._vertices.size() * sizeof(Vertex);
	VkBufferCreateInfo stagingBufferInfo = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.pNext = nullptr,
		.size = bufferSize,
		.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	};
	VmaAllocationCreateInfo vmaAllocInfo = {
		.usage = VMA_MEMORY_USAGE_CPU_ONLY,
	};

	// Create temporary staging buffer
	AllocatedBuffer stagingBuffer;
	VK_CHECK(vmaCreateBuffer(_allocator, &stagingBufferInfo, &vmaAllocInfo, &stagingBuffer._buffer, &stagingBuffer._allocation, nullptr));

	// Copy mesh to staging buffer
	void* data;
	vmaMapMemory(_allocator, stagingBuffer._allocation, &data);
	memcpy(data, mesh._vertices.data(), bufferSize);
	vmaUnmapMemory(_allocator, stagingBuffer._allocation);

	// Create GPU side buffer
	VkBufferCreateInfo vertexBufferInfo = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.pNext = nullptr,
		.size = bufferSize,
		.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	};
	vmaAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	VK_CHECK(vmaCreateBuffer(_allocator, &vertexBufferInfo, &vmaAllocInfo, &mesh._vertexBuffer._buffer, &mesh._vertexBuffer._allocation, nullptr));

	// Launch copy command! (staging to GPU)
	immediateSubmit([=](VkCommandBuffer cmd) {
		VkBufferCopy copy = {
			.srcOffset = 0,
			.dstOffset = 0,
			.size = bufferSize,
		};
		vkCmdCopyBuffer(cmd, stagingBuffer._buffer, mesh._vertexBuffer._buffer, 1, &copy);
		});

	// Destroy staging buffer
	vmaDestroyBuffer(_allocator, stagingBuffer._buffer, stagingBuffer._allocation);

	// Add destroy command for cleanup
	_mainDeletionQueue.pushFunction([=]() {
		vmaDestroyBuffer(_allocator, mesh._vertexBuffer._buffer, mesh._vertexBuffer._allocation);
		});
}

void VulkanEngine::renderRenderObjects(VkCommandBuffer cmd, RenderObject* first, size_t count)
{
	//
	// Setup scene camera
	//
	glm::vec3 camPos = { 0.0f, -3.0f, -5.0f };
	glm::mat4 view = glm::translate(glm::mat4(1.0f), camPos);
	glm::mat4 projection = glm::perspective(
		glm::radians(70.0f),
		(float_t)_windowExtent.width / (float_t)_windowExtent.height,
		0.1f,
		200.0f
	);
	projection[1][1] *= -1;		// I don't get this  -Timo
	glm::mat4 projectionView = projection * view;

	// Create and send off cameraData
	GPUCameraData cameraData = {
		.view = view,
		.projection = projection,
		.projectionView = projectionView,
	};
	const auto& currentFrame = getCurrentFrame();
	void* data;
	vmaMapMemory(_allocator, currentFrame.cameraBuffer._allocation, &data);
	memcpy(data, &cameraData, sizeof(GPUCameraData));
	vmaUnmapMemory(_allocator, currentFrame.cameraBuffer._allocation);

	//
	// Fill in scene data
	//
	float framed = _frameNumber / 120.0f;
	_sceneParameters.ambientColor = { sin(framed), 0, cos(framed), 1 };

	char* sceneData;
	vmaMapMemory(_allocator, _sceneParameterBuffer._allocation, (void**)&sceneData);

	int frameIndex = _frameNumber % FRAME_OVERLAP;
	sceneData += padUniformBufferSize(sizeof(GPUSceneData)) * frameIndex;		// Evil pointer... but I'm friends with it... am I evil?
	memcpy(sceneData, &_sceneParameters, sizeof(GPUSceneData));
	vmaUnmapMemory(_allocator, _sceneParameterBuffer._allocation);

	//
	// Fill in object data
	//
	void* objectData;
	vmaMapMemory(_allocator, currentFrame.objectBuffer._allocation, &objectData);
	GPUObjectData* objectSSBO = (GPUObjectData*)objectData;

	for (size_t i = 0; i < count; i++)
	{
		RenderObject& object = first[i];
		objectSSBO[i].modelMatrix = object.transformMatrix;		// Another evil pointer trick I love... call me Dmitri the Evil
	}

	vmaUnmapMemory(_allocator, currentFrame.objectBuffer._allocation);

	//
	// Render all the renderobjects
	//
	Material* lastMaterial = nullptr;
	Mesh* lastMesh = nullptr;
	for (size_t i = 0; i < count; i++)
	{
		RenderObject& object = first[i];

		if (!object.material || !object.mesh)	// @NOTE: Subdue a warning that a possible nullptr could be dereferenced
		{
			std::cerr << "ERROR: object material and/or mesh are NULL" << std::endl;
			continue;
		}

		if (object.material != lastMaterial)
		{
			// Bind the new material
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipeline);
			lastMaterial = object.material;

			// Camera data descriptor
			uint32_t uniformOffset = padUniformBufferSize(sizeof(GPUSceneData)) * frameIndex;
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipelineLayout, 0, 1, &currentFrame.globalDescriptor, 1, &uniformOffset);

			// Object data descriptor
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipelineLayout, 1, 1, &currentFrame.objectDescriptor, 0, nullptr);

			// Singletexture
			if (object.material->textureSet != VK_NULL_HANDLE)
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipelineLayout, 2, 1, &object.material->textureSet, 0, nullptr);
		}

		// Push constants
		MeshPushConstants constants = {
			.renderMatrix = object.transformMatrix,
		};		
		vkCmdPushConstants(cmd, object.material->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants), &constants);

		if (object.mesh != lastMesh)
		{
			// Bind the new mesh
			VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(cmd, 0, 1, &object.mesh->_vertexBuffer._buffer, &offset);
			lastMesh = object.mesh;
		}

		// Render it out
		vkCmdDraw(cmd, object.mesh->_vertices.size(), 1, 0, i);
	}
}

#ifdef _DEVELOP
void VulkanEngine::buildResourceList()
{
	std::string directory = "shader";
	for (const auto& entry : std::filesystem::directory_iterator(directory))
	{
		// Add the resource if it should be watched
		const auto& path = entry.path();
		if (!path.has_extension())
			continue;		// @NOTE: only allow resource files if they have an extension!  -Timo

		if (path.extension().compare(".spv") == 0 ||
			path.extension().compare(".log") == 0)
			continue;		// @NOTE: ignore compiled SPIRV shader files, logs

		resourcesToWatch.push_back({
			path,
			std::filesystem::last_write_time(path)
			});

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
		const std::filesystem::file_time_type lastWriteTime = std::filesystem::last_write_time(resource.path);
		if (resource.lastWriteTime == lastWriteTime)
			continue;

		//
		// Reload the resource
		//
		resource.lastWriteTime = lastWriteTime;

		if (!resource.path.has_extension())
		{
			std::cerr << "ERROR: file " << resource.path << " has no extension!" << std::endl;
			continue;
		}

		// Find the extension and execute appropriate routine
		const auto& ext = resource.path.extension();
		if (ext.compare(".vert") == 0 ||
			ext.compare(".frag") == 0)
		{
			// Compile the shader (GLSL -> SPIRV)
			glslToSPIRVHelper::compileGLSLShaderToSPIRV(resource.path);
		}
	}
}

void VulkanEngine::teardownResourceList()
{
}
#endif

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
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &_colorBlendAttachment;

	//
	// Build the actual pipeline
	//
	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.pNext = nullptr;

	pipelineInfo.stageCount = _shaderStages.size();
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
