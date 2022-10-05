#pragma once

#include "Imports.h"
#include "VkMesh.h"


struct GPUCameraData
{
	glm::mat4 view;
	glm::mat4 projection;
	glm::mat4 projectionView;
	glm::vec3 cameraPosition;
};

struct GPUSceneData
{
	glm::vec4 fogColor;
	glm::vec4 fogDistances;
	glm::vec4 ambientColor;
	glm::vec4 sunlightDirection;
	glm::vec4 sunlightColor;
};

struct GPUObjectData
{
	glm::mat4 modelMatrix;
};

struct FrameData
{
	VkSemaphore presentSemaphore, renderSemaphore;
	VkFence renderFence;

	VkCommandPool commandPool;
	VkCommandBuffer mainCommandBuffer;

	AllocatedBuffer cameraBuffer;
	VkDescriptorSet globalDescriptor;

	AllocatedBuffer objectBuffer;
	VkDescriptorSet objectDescriptor;
};

struct MeshPushConstants
{
	glm::vec4 data;
	glm::mat4 renderMatrix;
};

struct UploadContext
{
	VkFence uploadFence;
	VkCommandPool commandPool;
	VkCommandBuffer commandBuffer;
};

struct DeletionQueue
{
	std::deque<std::function<void()>> deletors;

	void pushFunction(std::function<void()>&& function)
	{
		deletors.push_back(function);
	}

	void flush()
	{
		// Call deletor lambdas in reverse order
		for (auto it = deletors.rbegin(); it != deletors.rend(); it++)
		{
			(*it)();
		}

		deletors.clear();
	}
};

struct Texture
{
	AllocatedImage image;
	VkImageView imageView;
};

struct Material
{
	VkDescriptorSet textureSet{ VK_NULL_HANDLE };	// Texture is defaulted to NULL
	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;
};

struct RenderObject
{
	Mesh* mesh;
	Material* material;
	glm::mat4 transformMatrix;
};

constexpr unsigned int FRAME_OVERLAP = 2;
class VulkanEngine
{
public:
	bool _isInitialized{ false };
	uint32_t _frameNumber{ 0 };

	VkExtent2D _windowExtent{ 1920, 1080 };
	struct SDL_Window* _window{ nullptr };
	bool _recreateSwapchain = false;

	VkInstance _instance;							// Vulkan library handle
	VkDebugUtilsMessengerEXT _debugMessenger;		// Vulkan debug output handle
	VkPhysicalDevice _chosenGPU;					// GPU chosen as the default device
	VkPhysicalDeviceProperties _gpuProperties;
	VkDevice _device;								// Vulkan device for commands
	VkSurfaceKHR _surface;							// Vulkan window surface

	DeletionQueue
		_swapchainDependentDeletionQueue,
		_mainDeletionQueue;

	VkSwapchainKHR _swapchain;						// The swapchain
	VkFormat _swapchainImageFormat;					// Image format expected by SDL2
	std::vector<VkImage> _swapchainImages;			// Images from the swapchain
	std::vector<VkImageView> _swapchainImageViews;	// Image views from the swapchain

	VkQueue _graphicsQueue;
	uint32_t _graphicsQueueFamily;

	VkRenderPass _renderPass;
	std::vector<VkFramebuffer> _framebuffers;

	// Depth Buffer
	VkImageView _depthImageView;
	AllocatedImage _depthImage;
	VkFormat _depthFormat;

	// VMA Lib Allocator
	VmaAllocator _allocator;


	void init();
	void run();
	void cleanup();
	void render();		// @TODO: Why is this public?

	//
	// Textures
	//
	std::unordered_map<std::string, Texture> _loadedTextures;
	void loadImages();

	//
	// Render Objects
	//
	std::vector<RenderObject> _renderObjects;
	std::unordered_map<std::string, Material> _materials;
	std::unordered_map<std::string, Mesh> _meshes;

	Material* createMaterial(VkPipeline pipeline, VkPipelineLayout layout, const std::string& name);
	Material* getMaterial(const std::string& name);
	Mesh* getMesh(const std::string& name);

	//
	// Descriptor Sets
	//
	VkDescriptorSetLayout _globalSetLayout;
	VkDescriptorSetLayout _objectSetLayout;
	VkDescriptorSetLayout _singleTextureSetLayout;
	VkDescriptorPool _descriptorPool;

	GPUSceneData _sceneParameters;
	AllocatedBuffer _sceneParameterBuffer;

	AllocatedBuffer createBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
	size_t padUniformBufferSize(size_t originalSize);

	// Upload context
	UploadContext _uploadContext;
	void immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function);

private:
	void initVulkan();
	void initSwapchain();
	void initCommands();
	void initDefaultRenderpass();
	void initFramebuffers();
	void initSyncStructures();
	void initDescriptors();
	void initPipelines();
	void initScene();
	void initImgui();

	void recreateSwapchain();

	FrameData _frames[FRAME_OVERLAP];
	FrameData& getCurrentFrame();

	bool loadShaderModule(const char* filePath, VkShaderModule* outShaderModule);

	void loadMeshes();
	void uploadMeshToGPU(Mesh& mesh);

	void renderRenderObjects(VkCommandBuffer cmd, RenderObject* first, size_t count);

#ifdef _DEVELOP
	struct ResourceToWatch
	{
		std::filesystem::path path;
		std::filesystem::file_time_type lastWriteTime;
	};
	std::vector<ResourceToWatch> resourcesToWatch;
	void buildResourceList();
	void checkIfResourceUpdatedThenHotswapRoutine();
	void teardownResourceList();
#endif
};


class PipelineBuilder
{
public:
	std::vector<VkPipelineShaderStageCreateInfo> _shaderStages;
	VkPipelineVertexInputStateCreateInfo _vertexInputInfo;
	VkPipelineInputAssemblyStateCreateInfo _inputAssembly;
	VkViewport _viewport;
	VkRect2D _scissor;
	VkPipelineRasterizationStateCreateInfo _rasterizer;
	VkPipelineColorBlendAttachmentState _colorBlendAttachment;
	VkPipelineMultisampleStateCreateInfo _multisampling;
	VkPipelineLayout _pipelineLayout;
	VkPipelineDepthStencilStateCreateInfo _depthStencil;

	VkPipeline buildPipeline(VkDevice device, VkRenderPass pass);
};
