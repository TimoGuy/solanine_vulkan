#pragma once

#include "Imports.h"
#include "VkDataStructures.h"

namespace vkglTF { class Model; }
class Entity;

struct GPUCameraData
{
	glm::mat4 view;
	glm::mat4 projection;
	glm::mat4 projectionView;
	glm::vec3 cameraPosition;
};

constexpr uint32_t SHADOWMAP_DIMENSION = 4096;
constexpr uint32_t SHADOWMAP_CASCADES  = 4;

struct GPUCascadeViewProjsData
{
	glm::mat4 cascadeViewProjs[SHADOWMAP_CASCADES];
};

struct CascadeIndexPushConstBlock
{
	uint32_t cascadeIndex;
};

struct GPUPBRShadingProps
{
	glm::vec4 lightDir;
	float_t exposure = 4.5f;
	float_t gamma = 2.2f;
	float_t prefilteredCubemapMipLevels;
	float_t scaleIBLAmbient = 1.0f;
	glm::vec4 cascadeSplits;
	glm::mat4 cascadeViewProjMats[SHADOWMAP_CASCADES];
	float_t zFarShadowZFarRatio;
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

	AllocatedBuffer cascadeViewProjsBuffer;  // For CSM shadow rendering
	VkDescriptorSet cascadeViewProjsDescriptor;

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
	std::string attachedEntityGuid;  // @NOTE: this is just for @DEBUG purposes for the imgui property panel
};

constexpr unsigned int FRAME_OVERLAP = 2;
constexpr size_t RENDER_OBJECTS_MAX_CAPACITY = 10000;

class VulkanEngine
{
public:
	bool _isInitialized{ false };
	uint32_t _frameNumber{ 0 };

	VkExtent2D _windowExtent{ 1920, 1080 };
	struct SDL_Window* _window{ nullptr };
	bool _isWindowMinimized = false;    // @NOTE: if we don't handle window minimization correctly, we can get the VK_ERROR_DEVICE_LOST(-4) error
	bool _recreateSwapchain = false;

	//
	// Vulkan Base
	//
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

	//
	// Shadow Renderpass
	//
	VkRenderPass _shadowRenderPass;

	struct ShadowRenderPassCascade
	{
		VkFramebuffer  framebuffer;
		VkImageView    imageView;  // @NOTE: this will just contain a single layer of the _shadowImage so that it can connect to the framebuffer
	};
	std::array<ShadowRenderPassCascade, SHADOWMAP_CASCADES> _shadowCascades;

	//
	// Main Renderpass
	//
	VkRenderPass _mainRenderPass;
	std::vector<VkFramebuffer> _framebuffers;

	// Main Depth Buffer
	VkImageView _depthImageView;
	AllocatedImage _depthImage;
	VkFormat _depthFormat;

	//
	// Picking Renderpass
	//
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
	std::vector<RenderObject> _renderObjects;    // @TODO: consider this to be private!
	std::vector<bool> _renderObjectLayersEnabled = { true, false, false };
	RenderObject* registerRenderObject(RenderObject renderObjectData);
	void unregisterRenderObject(RenderObject* objRegistration);

	std::unordered_map<std::string, Material> _materials;
	Material* createMaterial(VkPipeline pipeline, VkPipelineLayout layout, const std::string& name);
	Material* getMaterial(const std::string& name);

	std::unordered_map<std::string, vkglTF::Model*> _renderObjectModels;
	vkglTF::Model* createModel(vkglTF::Model* model, const std::string& name);
	vkglTF::Model* getModel(const std::string& name);

	struct PBRSceneTextureSet    // @NOTE: these are the textures that are needed for any type of pbr scene (i.e. the irradiance, prefilter, and brdf maps)
	{
		Texture irradianceCubemap;
		Texture prefilteredCubemap;
		Texture brdfLUTTexture;
		Texture shadowMap;
	} _pbrSceneTextureSet;

	//
	// Descriptor Sets
	//
	VkDescriptorSetLayout _globalSetLayout;
	VkDescriptorSetLayout _cascadeViewProjsSetLayout;
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
	void initShadowRenderpass();
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
	void renderRenderObjects(VkCommandBuffer cmd, const FrameData& currentFrame, size_t offset, size_t count, bool renderSkybox, bool materialOverride, VkPipelineLayout* overrideLayout, bool injectColorMapIntoMaterialOverride);
	void renderPickedObject(VkCommandBuffer cmd, const FrameData& currentFrame);

	//
	// Scene Camera
	// @NOTE: I think this is a bad name for it. The Scene Camera holds all the data for the GPU,
	//        however, the Main cam and the Moving Free cam are both virtual cameras that get switched
	//        to back and forth, and just simply change the scene camera's information
	//
public:
	struct SceneCamera
	{
		glm::vec3 facingDirection = { 0.0f, 0.0f, 1.0f };
		float_t fov               = glm::radians(70.0f);
		float_t aspect;
		float_t zNear             = 0.1f;
		float_t zFar              = 1500.0f;
		float_t zFarShadow        = 200.0f;
		GPUCameraData gpuCameraData;
		GPUCascadeViewProjsData gpuCascadeViewProjsData;  // This will get calculated from the scene camera since this is a CSM viewprojs
	} _sceneCamera;
private:
	void recalculateSceneCamera();
	void recalculateCascadeViewProjs();

	const uint32_t
		_cameraMode_mainCamMode = 0,
		_cameraMode_freeCamMode = 1;
	uint32_t _cameraMode = _cameraMode_freeCamMode;
	uint32_t _prevCameraMode = (uint32_t)-1;
	static const uint32_t _numCameraModes = 2;
	enum class CameraModeChangeEvent { NONE, ENTER, EXIT };
	CameraModeChangeEvent _changeEvents[_numCameraModes];
	bool _flagNextStepSetEnterChangeEvent = true;  // For default camera mode event to get triggered with ::ENTER

	//
	// Main cam mode
	// (Orbiting camera using just mouse inputs)
	//
	struct MainCamMode
	{
		RenderObject* targetObject = nullptr;
		glm::vec3 focusPosition = glm::vec3(0);
		glm::vec2 sensitivity = glm::vec2(0.1f, 0.1f);
		glm::vec2 orbitAngles = glm::vec2(glm::radians(45.0f), 0.0f);
		glm::vec3 calculatedCameraPosition = glm::vec3(0);
		glm::vec3 calculatedLookDirection = glm::vec3(0, -0.707106781, 0.707106781);

		// Tweak variables
		float_t   lookDistance = 15.0f;
		float_t   focusRadius = 3.0f;
		float_t   focusCentering = 0.75f;
		glm::vec3 focusPositionOffset = glm::vec3(0, 7, 0);
	} _mainCamMode;
	void updateMainCam(const float_t& deltaTime, CameraModeChangeEvent changeEvent);
public:
	void setMainCamTargetObject(RenderObject* targetObject);
private:

#ifdef _DEVELOP
public:
	//
	// Moving Free cam mode
	// (First person camera using wasd and q and e and RMB+mouse to look and move)
	//
	struct FreeCamMode
	{
		bool enabled = false;
		glm::ivec2 savedMousePosition;
		float_t sensitivity = 0.1f;
	} _freeCamMode;
private:
	void updateFreeCam(const float_t& deltaTime, CameraModeChangeEvent changeEvent);
#endif


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
public:
	//
	// Debug Messages
	//
	struct DebugMessage
	{
		std::string message;
		uint32_t type = 0;  // 0: info | 1: warning | 2: error
		float_t timeUntilDeletion = 5.0f;  // @NOTE: use this to lengthen certain messages, like error ones
	};
	static void pushDebugMessage(const DebugMessage& message);
private:
	static std::vector<DebugMessage> _debugMessages;
	
	//
	// Debug
	//
	struct DebugStatistics
	{
		uint32_t currentFPS;

		size_t renderTimesMSHeadIndex = 0;
		size_t renderTimesMSCount = 256;
		float_t renderTimesMS[256 * 2];
		float_t highestRenderTime = -1.0f;
	} _debugStats;
	bool _showCollisionDebugDraw = true;
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
		VkDescriptorSet textureLayerCollision;
	} _imguiData;
	void renderImGui(float_t deltaTime);
#endif

    friend class Entity;
};


class PipelineBuilder
{
public:
	std::vector<VkPipelineShaderStageCreateInfo>       _shaderStages;
	VkPipelineVertexInputStateCreateInfo               _vertexInputInfo;
	VkPipelineInputAssemblyStateCreateInfo             _inputAssembly;
	VkViewport                                         _viewport;
	VkRect2D                                           _scissor;
	VkPipelineRasterizationStateCreateInfo             _rasterizer;
	std::vector<VkPipelineColorBlendAttachmentState>   _colorBlendAttachment;
	VkPipelineMultisampleStateCreateInfo               _multisampling;
	VkPipelineLayout                                   _pipelineLayout;
	VkPipelineDepthStencilStateCreateInfo              _depthStencil;
	VkPipelineDynamicStateCreateInfo                   _dynamicState;

	VkPipeline buildPipeline(VkDevice device, VkRenderPass pass);
};
