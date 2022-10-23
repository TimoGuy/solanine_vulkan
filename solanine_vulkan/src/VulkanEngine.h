#pragma once

#include "Imports.h"
#include "VkglTFModel.h"


class Entity;

struct GPUCameraData
{
	glm::mat4 view;
	glm::mat4 projection;
	glm::mat4 projectionView;
	glm::vec3 cameraPosition;
};

struct GPUPBRShadingProps
{
	glm::vec4 lightDir;
	float_t exposure = 4.5f;
	float_t gamma = 2.2f;
	float_t prefilteredCubemapMipLevels;
	float_t scaleIBLAmbient = 1.0f;
	float_t debugViewInputs = 0;
	float_t debugViewEquation = 0;
};

struct GPUObjectData
{
	glm::mat4 modelMatrix;
};

struct GPUPickingSelectedIdData
{
	uint32_t selectedId;
};

struct ColorPushConstBlock
{
	glm::vec4 color;
};

struct FrameData
{
	VkSemaphore presentSemaphore, renderSemaphore;
	VkFence renderFence, pickingRenderFence;

	VkCommandPool commandPool;
	VkCommandBuffer mainCommandBuffer;
	VkCommandBuffer pickingCommandBuffer;

	AllocatedBuffer cameraBuffer;
	AllocatedBuffer pbrShadingPropsBuffer;
	VkDescriptorSet globalDescriptor;

	AllocatedBuffer objectBuffer;
	VkDescriptorSet objectDescriptor;

	AllocatedBuffer pickingSelectedIdBuffer;
	VkDescriptorSet pickingReturnValueDescriptor;
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

enum class RenderLayer
{
	VISIBLE, INVISIBLE, BUILDER
};

struct RenderObject
{
	vkglTF::Model* model;
	glm::mat4 transformMatrix;
	RenderLayer renderLayer;
};

constexpr unsigned int FRAME_OVERLAP = 2;
class VulkanEngine
{
public:
	bool _isInitialized{ false };
	uint32_t _frameNumber{ 0 };

	VkExtent2D _windowExtent{ 1920, 1080 };
	struct SDL_Window* _window{ nullptr };
	bool _isWindowMinimized = false;    // @NOTE: if we don't handle window minimization correctly, we can get the VK_ERROR_DEVICE_LOST(-4) error
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

	// Main Renderpass
	VkRenderPass _renderPass;
	std::vector<VkFramebuffer> _framebuffers;

	// Main Depth Buffer
	VkImageView _depthImageView;
	AllocatedImage _depthImage;
	VkFormat _depthFormat;

	// Picking Renderpass
	VkRenderPass _pickingRenderPass;
	VkFramebuffer _pickingFramebuffer;
	VkImageView _pickingImageView;
	AllocatedImage _pickingImage;

	// Picking Depth Buffer
	VkImageView _pickingDepthImageView;
	AllocatedImage _pickingDepthImage;

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
	std::vector<bool> _renderObjectLayersEnabled = { true, true, true };

	struct RenderObjectModels
	{
		vkglTF::Model skybox;
		vkglTF::Model slimeGirl;
	} _renderObjectModels;

	struct PBRSceneTextureSet    // @NOTE: these are the textures that are needed for any type of pbr scene (i.e. the irradiance, prefilter, and brdf maps)
	{
		Texture irradianceCubemap;
		Texture prefilteredCubemap;
		Texture brdfLUTTexture;
	} _pbrSceneTextureSet;

	Material* createMaterial(VkPipeline pipeline, VkPipelineLayout layout, const std::string& name);
	Material* getMaterial(const std::string& name);

	//
	// Descriptor Sets
	//
	VkDescriptorSetLayout _globalSetLayout;
	VkDescriptorSetLayout _objectSetLayout;
	VkDescriptorSetLayout _singleTextureSetLayout;
	VkDescriptorSetLayout _pbrTexturesSetLayout;
	VkDescriptorSetLayout _pickingReturnValueSetLayout;
	VkDescriptorSetLayout _skeletalAnimationSetLayout;    // @NOTE: for this one, descriptor sets are created inside of the vkglTFModels themselves, they're not global
	VkDescriptorPool _descriptorPool;

	AllocatedBuffer createBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
	size_t padUniformBufferSize(size_t originalSize);    // @NOTE: this is unused, but it's useful for dynamic uniform buffers

	// Upload context
	UploadContext _uploadContext;
	void immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function);

private:
	void initVulkan();
	void initSwapchain();
	void initCommands();
	void initDefaultRenderpass();
	void initPickingRenderpass();
	void initFramebuffers();
	void initSyncStructures();
	void initDescriptors();
	void initPipelines();
	void initScene();
	void generatePBRCubemaps();
	void generateBRDFLUT();
	void attachPBRDescriptors();
	void initImgui();

	void recreateSwapchain();

	FrameData _frames[FRAME_OVERLAP];
	FrameData& getCurrentFrame();

	bool loadShaderModule(const char* filePath, VkShaderModule* outShaderModule);

	void loadMeshes();

	void uploadCurrentFrameToGPU(const FrameData& currentFrame);
	void renderRenderObjects(VkCommandBuffer cmd, const FrameData& currentFrame, size_t offset, size_t count, bool renderSkybox, bool materialOverride, VkPipelineLayout* overrideLayout);
	void renderPickedObject(VkCommandBuffer cmd, const FrameData& currentFrame);

	//
	// Scene Camera
	//
	struct SceneCamera
	{
		glm::vec3 facingDirection = { 0.0f, 0.0f, 1.0f };
		float_t fov = glm::radians(70.0f);
		float_t aspect;
		float_t zNear = 0.1f;
		float_t zFar = 200.0f;
		GPUCameraData gpuCameraData;
	} _sceneCamera;
	void recalculateSceneCamera();

	//
	// PBR rendering
	//
	struct PBRRendering
	{
		// Pipelines
		VkPipeline skybox;
		VkPipeline pbr;
		VkPipeline pbrDoubleSided;
		VkPipeline pbrAlphaBlend;

		VkPipeline currentBoundPipeline = VK_NULL_HANDLE;

		GPUPBRShadingProps gpuSceneShadingProps;
	} _pbrRendering;

public:
	enum PBRWorkflows { PBR_WORKFLOW_METALLIC_ROUGHNESS = 0, PBR_WORKFLOW_SPECULAR_GLOSSINESS = 1 };

	struct PBRMaterialPushConstBlock
	{
		glm::vec4 baseColorFactor;
		glm::vec4 emissiveFactor;
		glm::vec4 diffuseFactor;
		glm::vec4 specularFactor;
		float workflow;
		int colorTextureSet;
		int PhysicalDescriptorTextureSet;
		int normalTextureSet;
		int occlusionTextureSet;
		int emissiveTextureSet;
		float metallicFactor;
		float roughnessFactor;
		float alphaMask;
		float alphaMaskCutoff;
	};

private:

	//
	// Entities
	//
	void INTERNALaddEntity(Entity* entity);
	void INTERNALdestroyEntity(Entity* entity);
	void INTERNALaddRemoveRequestedEntities();
public:
	void destroyEntity(Entity* entity);    // Do not use the destructor or INTERNALdestroyEntity(), use this function!
private:
	std::vector<Entity*> _entities;
	std::deque<Entity*> _entitiesToAddQueue;
	std::deque<Entity*> _entitiesToDestroyQueue;
	bool _flushEntitiesToDestroyRoutine = false;

#ifdef _DEVELOP
	//
	// Moving Free cam
	//
	struct FreeCamMode
	{
		bool enabled = false;
		glm::ivec2 savedMousePosition;
		float_t sensitivity = 100.0f;
	} _freeCamMode;
	void updateFreeCam(const float_t& deltaTime);
	
	//
	// Debug Statistics
	//
	struct DebugStats
	{
		uint32_t currentFPS;

		size_t renderTimesMSHeadIndex = 0;
		size_t renderTimesMSCount = 256;
		float_t renderTimesMS[256 * 2];
		float_t highestRenderTime = -1.0f;
	} _debugStats;
	void updateDebugStats(const float_t& deltaTime);

	//
	// Hot-swappable resources system
	//
	struct ResourceToWatch
	{
		std::filesystem::path path;
		std::filesystem::file_time_type lastWriteTime;
	};
	std::vector<ResourceToWatch> resourcesToWatch;
	void buildResourceList();
	void checkIfResourceUpdatedThenHotswapRoutine();
	void teardownResourceList();

	//
	// Moving matrices around
	//
	struct MovingMatrix
	{
		glm::mat4* matrixToMove = nullptr;
		glm::mat4* prevMatrixToMove = nullptr;    // @NOTE: this is for invalidating the cache
		bool invalidateCache = true;
		glm::vec3 cachedPosition;
		glm::vec3 cachedEulerAngles;
		glm::vec3 cachedScale;
	} _movingMatrix;
	void submitSelectedRenderObjectId(int32_t id);

	//
	// ImGui Stuff
	//
	struct ImGuiStuff
	{
		VkDescriptorSet textureLayerVisible;
		VkDescriptorSet textureLayerInvisible;
		VkDescriptorSet textureLayerBuilder;
	} _imguiData;
	void renderImGui();
#endif

    friend class Entity;
};


class PipelineBuilder
{
public:
	std::vector<VkPipelineShaderStageCreateInfo> _shaderStages;
	VkPipelineVertexInputStateCreateInfo         _vertexInputInfo;
	VkPipelineInputAssemblyStateCreateInfo       _inputAssembly;
	VkViewport                                   _viewport;
	VkRect2D                                     _scissor;
	VkPipelineRasterizationStateCreateInfo       _rasterizer;
	VkPipelineColorBlendAttachmentState          _colorBlendAttachment;
	VkPipelineMultisampleStateCreateInfo         _multisampling;
	VkPipelineLayout                             _pipelineLayout;
	VkPipelineDepthStencilStateCreateInfo        _depthStencil;
	VkPipelineDynamicStateCreateInfo             _dynamicState;

	VkPipeline buildPipeline(VkDevice device, VkRenderPass pass);
};
