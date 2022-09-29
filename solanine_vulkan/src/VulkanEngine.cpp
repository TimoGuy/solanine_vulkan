#include "VulkanEngine.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include "VkBootstrap.h"
#include "VkInitializers.h"
#include "GLSLToSPIRVHelper.h"

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
	initPipelines();
	loadMeshes();
	initScene();

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
			if (e.type == SDL_QUIT)
			{
				isRunning = false;
			}
		}

		render();
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
	VK_CHECK(vkAcquireNextImageKHR(_device, _swapchain, TIMEOUT_1_SEC, currentFrame.presentSemaphore, nullptr, &swapchainImageIndex));

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
	// Begin Renderpass
	//
	VkClearValue clearValue;
	float flash = abs(sin(_frameNumber / 120.f));
	clearValue.color = { { 0.0f, 0.0f, flash, 1.0f } };

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

	////
	//// Renderpass		@TODO: take out the hardcoded stuff like this!!!
	////
	//vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _meshPipeline);
	//
	//VkDeviceSize offset = 0;
	//vkCmdBindVertexBuffers(cmd, 0, 1, &_triangleMesh._vertexBuffer._buffer, &offset);
	//
	//// Create modelview matrix for rendering the triangle
	//glm::vec3 camPos = { 0.0f, 0.0f, -2.0f };
	//glm::mat4 view = glm::translate(glm::mat4(1.0f), camPos);
	//glm::mat4 projection = glm::perspective(glm::radians(70.0f), (float)_windowExtent.width / (float)_windowExtent.height, 0.1f, 200.0f);
	////projection[1][1] *= -1;
	//glm::mat4 model = glm::rotate(glm::mat4(1.0f), glm::radians(_frameNumber * 0.4f), glm::vec3(0, 1, 0));
	//
	//glm::mat4 transform = projection * view * model;
	//MeshPushConstants constants = {
	//	.renderMatrix = transform
	//};
	//vkCmdPushConstants(cmd, _meshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants), &constants);
	//
	//vkCmdDraw(cmd, _triangleMesh._vertices.size(), 1, 0, 0);

	//
	// End Renderpass
	//
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

	VK_CHECK(vkQueuePresentKHR(_graphicsQueue, &presentInfo));

	//
	// End of frame!
	//
	_frameNumber++;
}

void VulkanEngine::cleanup()
{
#ifdef _DEVELOP
	teardownResourceList();
#endif

	if (_isInitialized)
	{
		// Wait until the GPU is finished before executing cleanup (check both fences)
		for (size_t i = 0; i < FRAME_OVERLAP; i++)
			vkWaitForFences(_device, 1, &_frames[i].renderFence, true, TIMEOUT_1_SEC * 2);

		_mainDeletionQueue.flush();

		vmaDestroyAllocator(_allocator);
		vkDestroySurfaceKHR(_instance, _surface, nullptr);
		vkb::destroy_debug_utils_messenger(_instance, _debugMessenger);
		vkDestroyDevice(_device, nullptr);
		vkDestroyInstance(_instance, nullptr);

		SDL_DestroyWindow(_window);
	}
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

void VulkanEngine::initVulkan()
{
	//
	// Setup vulkan instance and debug messenger
	//
	vkb::InstanceBuilder builder;

	auto instance = builder.set_app_name("Hawsoo_Solanine_Win32")
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
	vkb::Device vkbDevice = deviceBuilder.build().value();

	_device = vkbDevice.device;
	_chosenGPU = physicalDevice.physical_device;

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
	_mainDeletionQueue.pushFunction([=]() {
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
	_mainDeletionQueue.pushFunction([=]() {
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
		_mainDeletionQueue.pushFunction([=]() {
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
}

void VulkanEngine::initPipelines()
{
	VkShaderModule triangleVertShader;
	if (!loadShaderModule("shader/triangle.vert.spv", &triangleVertShader))
	{
		std::cout << "ERROR: building triangle vert shader" << std::endl;
	}
	else
	{
		std::cout << "Triangle vert shader SUCCESS" << std::endl;
	}

	VkShaderModule triangleFragShader;
	if (!loadShaderModule("shader/triangle.frag.spv", &triangleFragShader))
	{
		std::cout << "ERROR: building triangle frag shader" << std::endl;
	}
	else
	{
		std::cout << "Triangle frag shader SUCCESS" << std::endl;
	}

	//
	// Triangle Pipeline
	//
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = vkinit::pipelineLayoutCreateInfo();
	VK_CHECK(vkCreatePipelineLayout(_device, &pipelineLayoutInfo, nullptr, &_trianglePipelineLayout));

	PipelineBuilder pipelineBuilder;
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, triangleVertShader)
	);
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, triangleFragShader)
	);

	pipelineBuilder._vertexInputInfo = vkinit::vertexInputStateCreateInfo();
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
	pipelineBuilder._multisampling = vkinit::multisamplingStateCreateInfo();
	pipelineBuilder._colorBlendAttachment = vkinit::colorBlendAttachmentState();
	pipelineBuilder._pipelineLayout = _trianglePipelineLayout;
	pipelineBuilder._depthStencil = vkinit::depthStencilCreateInfo(true, true, VK_COMPARE_OP_LESS_OR_EQUAL);

	_trianglePipeline = pipelineBuilder.buildPipeline(_device, _renderPass);

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

	VK_CHECK(vkCreatePipelineLayout(_device, &meshPipelineLayoutInfo, nullptr, &_meshPipelineLayout));

	VertexInputDescription vertexDescription = Vertex::getVertexDescription();

	pipelineBuilder._vertexInputInfo.pVertexAttributeDescriptions = vertexDescription.attributes.data();
	pipelineBuilder._vertexInputInfo.vertexAttributeDescriptionCount = vertexDescription.attributes.size();
	pipelineBuilder._vertexInputInfo.pVertexBindingDescriptions = vertexDescription.bindings.data();
	pipelineBuilder._vertexInputInfo.vertexBindingDescriptionCount= vertexDescription.bindings.size();

	pipelineBuilder._shaderStages.clear();

	VkShaderModule meshVertShader;
	if (!loadShaderModule("shader/mesh.vert.spv", &meshVertShader))
	{
		std::cout << "ERROR: building mesh vert shader" << std::endl;
	}
	else
	{
		std::cout << "Mesh vert shader SUCCESS" << std::endl;
	}

	pipelineBuilder._shaderStages.push_back(
		vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, meshVertShader));
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, triangleFragShader));
	pipelineBuilder._pipelineLayout = _meshPipelineLayout;

	_meshPipeline = pipelineBuilder.buildPipeline(_device, _renderPass);
	createMaterial(_meshPipeline, _meshPipelineLayout, "defaultMaterial");

	//
	// Cleanup
	//
	vkDestroyShaderModule(_device, triangleVertShader, nullptr);
	vkDestroyShaderModule(_device, triangleFragShader, nullptr);
	vkDestroyShaderModule(_device, meshVertShader, nullptr);

	// Add destroy command for cleanup
	_mainDeletionQueue.pushFunction([=]() {
		vkDestroyPipeline(_device, _trianglePipeline, nullptr);
		vkDestroyPipeline(_device, _meshPipeline, nullptr);

		vkDestroyPipelineLayout(_device, _trianglePipelineLayout, nullptr);
		vkDestroyPipelineLayout(_device, _meshPipelineLayout, nullptr);
		});
	
}

void VulkanEngine::initScene()
{
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

	uploadMesh(_triangleMesh);
	_meshes["triangle"] = _triangleMesh;
}

void VulkanEngine::uploadMesh(Mesh& mesh)
{
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = mesh._vertices.size() * sizeof(Vertex);
	bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

	VmaAllocationCreateInfo vmaAllocInfo = {};
	vmaAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

	// Allocate the buffer
	VK_CHECK(vmaCreateBuffer(_allocator, &bufferInfo, &vmaAllocInfo, &mesh._vertexBuffer._buffer, &mesh._vertexBuffer._allocation, nullptr));

	// Add destroy command for cleanup
	_mainDeletionQueue.pushFunction([=]() {
		vmaDestroyBuffer(_allocator, mesh._vertexBuffer._buffer, mesh._vertexBuffer._allocation);
		});

	//
	// Copy to GPU
	//
	void* data;
	vmaMapMemory(_allocator, mesh._vertexBuffer._allocation, &data);
	memcpy(data, mesh._vertices.data(), mesh._vertices.size() * sizeof(Vertex));
	vmaUnmapMemory(_allocator, mesh._vertexBuffer._allocation);
}

void VulkanEngine::renderRenderObjects(VkCommandBuffer cmd, RenderObject* first, size_t count)
{
	//
	// Setup scene camera
	//
	glm::vec3 camPos = { 0.0f, -6.0f, -10.0f };
	glm::mat4 view = glm::translate(glm::mat4(1.0f), camPos);
	glm::mat4 projection = glm::perspective(
		glm::radians(70.0f),
		(float_t)_windowExtent.width / (float_t)_windowExtent.height,
		0.1f,
		200.0f
	);
	projection[1][1] *= -1;		// I don't get this  -Timo
	glm::mat4 projectionView = projection * view;

	//
	// Render all the renderobjects
	//
	Mesh* lastMesh = nullptr;
	Material* lastMaterial = nullptr;
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
		}

		glm::mat4 model = object.transformMatrix;

		MeshPushConstants constants = {
			.renderMatrix = projectionView * model,
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
		vkCmdDraw(cmd, object.mesh->_vertices.size(), 1, 0, 0);
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
