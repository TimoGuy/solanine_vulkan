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

	initVulkan();
	initSwapchain();
	initCommands();
	initDefaultRenderpass();
	initFramebuffers();
	initSyncStructures();
#ifdef _DEVELOP
	buildResourceList();
#endif
	initPipelines();
	loadMeshes();

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
	// Wait until GPU finishes rendering the previous frame
	VK_CHECK(vkWaitForFences(_device, 1, &_renderFence, true, TIMEOUT_1_SEC));
	VK_CHECK(vkResetFences(_device, 1, &_renderFence));

	// Request image from swapchain
	uint32_t swapchainImageIndex;
	VK_CHECK(vkAcquireNextImageKHR(_device, _swapchain, TIMEOUT_1_SEC, _presentSemaphore, nullptr, &swapchainImageIndex));

	// After commands finished executing, we can safely resume recording commands
	VK_CHECK(vkResetCommandBuffer(_mainCommandBuffer, 0));

	//
	// Record commands into command buffer
	//
	VkCommandBuffer cmd = _mainCommandBuffer;

	VkCommandBufferBeginInfo cmdBeginInfo = {};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.pNext = nullptr;
	cmdBeginInfo.pInheritanceInfo = nullptr;
	cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	//
	// Begin Renderpass
	//
	VkClearValue clearValue;
	float flash = abs(sin(_frameNumber / 120.f));
	clearValue.color = { { 0.0f, 0.0f, flash, 1.0f } };

	VkRenderPassBeginInfo renderpassInfo = {};
	renderpassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderpassInfo.pNext = nullptr;

	renderpassInfo.renderPass = _renderPass;
	renderpassInfo.renderArea.offset.x = 0;
	renderpassInfo.renderArea.offset.y = 0;
	renderpassInfo.renderArea.extent = _windowExtent;
	renderpassInfo.framebuffer = _framebuffers[swapchainImageIndex];		// @NOTE: Framebuffer of the index the swapchain gave

	renderpassInfo.clearValueCount = 1;
	renderpassInfo.pClearValues = &clearValue;

	vkCmdBeginRenderPass(cmd, &renderpassInfo, VK_SUBPASS_CONTENTS_INLINE);

	//
	// Renderpass
	//
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _trianglePipeline);
	vkCmdDraw(cmd, 3, 1, 0, 0);

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
	submit.pWaitSemaphores = &_presentSemaphore;

	submit.signalSemaphoreCount = 1;
	submit.pSignalSemaphores = &_renderSemaphore;

	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &cmd;

	VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submit, _renderFence));		// Submit work to gpu

	//
	// Submit the rendered frame to the screen
	//
	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;

	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &_swapchain;

	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &_renderSemaphore;

	presentInfo.pImageIndices = &swapchainImageIndex;

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
		// Wait until the GPU is finished before executing cleanup
		vkWaitForFences(_device, 1, &_renderFence, true, TIMEOUT_1_SEC);

		_mainDeletionQueue.flush();

		vmaDestroyAllocator(_allocator);
		vkDestroySurfaceKHR(_instance, _surface, nullptr);
		vkb::destroy_debug_utils_messenger(_instance, _debugMessenger);
		vkDestroyDevice(_device, nullptr);
		vkDestroyInstance(_instance, nullptr);

		SDL_DestroyWindow(_window);
	}
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
}

void VulkanEngine::initCommands()
{
	// Create command pool
	VkCommandPoolCreateInfo commandPoolInfo = vkinit::commandPoolCreateInfo(_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
	VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_commandPool));

	// Create command buffer
	VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::commandBufferAllocateInfo(_commandPool, 1);
	VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_mainCommandBuffer));

	// Add destroy command for cleanup
	_mainDeletionQueue.pushFunction([=]() {
		vkDestroyCommandPool(_device, _commandPool, nullptr);
		});
}

void VulkanEngine::initDefaultRenderpass()
{
	VkAttachmentDescription colorAttachment = {};
	colorAttachment.format = _swapchainImageFormat;
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;		// @NOTE: After this renderpass, the image needs to be ready for the display

	//
	// Define the subpass to render to the default renderpass
	//
	VkAttachmentReference colorAttachmentRef = {};
	colorAttachmentRef.attachment = 0;
	colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorAttachmentRef;

	//
	// Create the renderpass for the subpass
	//
	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = 1;
	renderPassInfo.pAttachments = &colorAttachment;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;

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
	fbInfo.attachmentCount = 1;
	fbInfo.width = _windowExtent.width;
	fbInfo.height = _windowExtent.height;
	fbInfo.layers = 1;

	const uint32_t swapchainImagecount = _swapchainImages.size();
	_framebuffers = std::vector<VkFramebuffer>(swapchainImagecount);

	for (size_t i = 0; i < swapchainImagecount; i++)
	{
		fbInfo.pAttachments = &_swapchainImageViews[i];
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
	//
	// Create Fence
	//
	VkFenceCreateInfo fenceCreateInfo = {};
	fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceCreateInfo.pNext = nullptr;
	fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_renderFence));

	// Add destroy command for cleanup
	_mainDeletionQueue.pushFunction([=]() {
		vkDestroyFence(_device, _renderFence, nullptr);
		});

	//
	// Create Semaphores
	//
	VkSemaphoreCreateInfo semaphoreCreateInfo = {};
	semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	semaphoreCreateInfo.pNext = nullptr;
	semaphoreCreateInfo.flags = 0;

	VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_presentSemaphore));
	VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_renderSemaphore));

	// Add destroy command for cleanup
	_mainDeletionQueue.pushFunction([=]() {
		vkDestroySemaphore(_device, _presentSemaphore, nullptr);
		vkDestroySemaphore(_device, _renderSemaphore, nullptr);
		});
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

	_trianglePipeline = pipelineBuilder.buildPipeline(_device, _renderPass);

	// Destroy all shader modules
	vkDestroyShaderModule(_device, triangleVertShader, nullptr);
	vkDestroyShaderModule(_device, triangleFragShader, nullptr);

	// Add destroy command for cleanup
	_mainDeletionQueue.pushFunction([=]() {
		vkDestroyPipeline(_device, _trianglePipeline, nullptr);
		vkDestroyPipelineLayout(_device, _trianglePipelineLayout, nullptr);
		});
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
