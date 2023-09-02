#pragma once

#include "Imports.h"
#include "Settings.h"
#include "VkDataStructures.h"
#include "EntityManager.h"
#include "SceneManagement.h"


struct RenderObject;
class RenderObjectManager;
class Entity;
class EntityManager;
struct Camera;
namespace vkglTF { struct Model; }
struct ImGuiIO;

struct CascadeIndexPushConstBlock
{
	uint32_t cascadeIndex;
};

struct GPUPBRShadingProps
{
	vec4 lightDir;
	float_t exposure = 4.5f;
	float_t gamma = 2.2f;
	float_t prefilteredCubemapMipLevels;
	float_t scaleIBLAmbient = 1.0f;
	vec4 cascadeSplits;
	mat4 cascadeViewProjMats[SHADOWMAP_CASCADES];
	float_t shadowMapScale        = 1.0f / SHADOWMAP_DIMENSION;
	float_t shadowJitterMapXScale = 1.0f / SHADOWMAP_JITTERMAP_DIMENSION_X;
	float_t shadowJitterMapYScale = 1.0f / SHADOWMAP_JITTERMAP_DIMENSION_Y;
	float_t shadowJitterMapOffsetScale = 1.0f;
	float_t debugViewInputs = 0;
	float_t debugViewEquation = 0;
};

struct GPUObjectData
{
	mat4 modelMatrix;
};

struct GPUPickingSelectedIdData
{
	uint32_t selectedId[RENDER_OBJECTS_MAX_CAPACITY];
	float_t selectedDepth[RENDER_OBJECTS_MAX_CAPACITY];
};

struct ColorPushConstBlock
{
	vec4 color;
};

struct FrameData
{
	VkSemaphore presentSemaphore, renderSemaphore;
	VkFence renderFence, pickingRenderFence;

	VkCommandPool commandPool;
	VkCommandBuffer mainCommandBuffer;
	VkCommandBuffer pickingCommandBuffer;
	AllocatedBuffer indirectDrawCommandBuffer;

	AllocatedBuffer cameraBuffer;
	AllocatedBuffer pbrShadingPropsBuffer;
	VkDescriptorSet globalDescriptor;

	AllocatedBuffer cascadeViewProjsBuffer;  // For CSM shadow rendering
	VkDescriptorSet cascadeViewProjsDescriptor;

	AllocatedBuffer objectBuffer;
	VkDescriptorSet objectDescriptor;

	AllocatedBuffer instancePtrBuffer;
	VkDescriptorSet instancePtrDescriptor;

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


class VulkanEngine
{
public:
	void init();
	void run();
	void cleanup();

	bool _isInitialized{ false };
	uint32_t _frameNumber{ 0 };

	//VkExtent2D _windowExtent{ 1920, 1080 };
	VkExtent2D _windowExtent{ 1280, 720 };
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

	VkSwapchainKHR             _swapchain;				// The swapchain
	VkFormat                   _swapchainImageFormat;	// Image format expected by SDL2
	std::vector<VkImage>       _swapchainImages;		// Images from the swapchain
	std::vector<VkImageView>   _swapchainImageViews;	// Image views from the swapchain
	std::vector<VkFramebuffer> _swapchainFramebuffers;

	VkQueue  _graphicsQueue;
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
	VkRenderPass  _mainRenderPass;
	VkFramebuffer _mainFramebuffer;
	Texture       _mainImage;

	// Main Depth Buffer
	AllocatedImage _depthImage;
	VkImageView    _depthImageView;
	VkFormat       _depthFormat;

	//
	// UI Renderpass
	//
	VkRenderPass  _uiRenderPass;
	VkFramebuffer _uiFramebuffer;
	Texture       _uiImage;

	//
	// Postprocessing Renderpass
	//
	VkRenderPass _postprocessRenderPass;  // @NOTE: no framebuffers defined here, bc this will write to the swapchain framebuffers
	Texture      _bloomPostprocessImage;
	VkExtent2D   _bloomPostprocessImageExtent;

	//
	// Picking Renderpass
	//
	VkRenderPass   _pickingRenderPass;
	VkFramebuffer  _pickingFramebuffer;
	VkImageView    _pickingImageView;
	AllocatedImage _pickingImage;

	// Picking Depth Buffer
	VkImageView    _pickingDepthImageView;
	AllocatedImage _pickingDepthImage;

	// VMA Lib Allocator
	VmaAllocator _allocator;

	void render();		// @TODO: Why is this public?

	//
	// Textures
	//
	std::unordered_map<std::string, Texture> _loadedTextures;
	void loadImages();

	//
	// Voxel lightgrids
	//
	struct VoxelFieldLightingGridTextureSet
	{
		struct GPUTransform
		{
			mat4 transform;
		};
		std::vector<GPUTransform> transforms;
		AllocatedBuffer           transformsBuffer;

		bool                  flagRecreateTextureSet = false;
		std::vector<Texture>  textures;  // @TODO: make way to add and remove lighting grids or edit lighting grids.
		VkDescriptorSet       descriptor;
		VkDescriptorSetLayout layout;
	} _voxelFieldLightingGridTextureSet;
	void initVoxelLightingDescriptor();
	void recreateVoxelLightingDescriptor();

	//
	// Render Objects
	//
	RenderObjectManager* _roManager;

	std::unordered_map<std::string, Material> _materials;
	Material* attachPipelineToMaterial(VkPipeline pipeline, VkPipelineLayout layout, const std::string& name);
	Material* attachTextureSetToMaterial(VkDescriptorSet textureSet, const std::string& name);
	Material* getMaterial(const std::string& name);

	struct PBRSceneTextureSet    // @NOTE: these are the textures that are needed for any type of pbr scene (i.e. the irradiance, prefilter, and brdf maps)
	{
		Texture irradianceCubemap;
		Texture prefilteredCubemap;
		Texture brdfLUTTexture;
		Texture shadowMap;
		Texture shadowJitterMap;
	} _pbrSceneTextureSet;

	//
	// Descriptor Sets
	//
	VkDescriptorSetLayout _globalSetLayout;
	VkDescriptorSetLayout _cascadeViewProjsSetLayout;
	VkDescriptorSetLayout _objectSetLayout;
	VkDescriptorSetLayout _instancePtrSetLayout;
	VkDescriptorSetLayout _singleTextureSetLayout;
	VkDescriptorSetLayout _pbrTexturesSetLayout;
	VkDescriptorSetLayout _pickingReturnValueSetLayout;
	VkDescriptorSetLayout _skeletalAnimationSetLayout;    // @NOTE: for this one, descriptor sets are created inside of the vkglTFModels themselves, they're not global
	VkDescriptorSetLayout _postprocessSetLayout;

	AllocatedBuffer createBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
	size_t padUniformBufferSize(size_t originalSize);    // @NOTE: this is unused, but it's useful for dynamic uniform buffers

#ifdef _DEVELOP
	bool generateCollisionDebugVisualization = false;
#endif

	// Upload context
	UploadContext _uploadContext;
	void immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function);

private:
	void initVulkan();
	void initSwapchain();
	void initCommands();
	void initShadowRenderpass();
	void initShadowImages();
	void initMainRenderpass();
	void initUIRenderpass();
	void initPostprocessRenderpass();
	void initPickingRenderpass();
	void initFramebuffers();
	void initSyncStructures();
	void initDescriptors();
	void initPipelines();
	void generatePBRCubemaps();
	void generateBRDFLUT();
	void initImgui();

	void recreateSwapchain();

	FrameData _frames[FRAME_OVERLAP];
	FrameData& getCurrentFrame();

	void loadMeshes();

	void uploadCurrentFrameToGPU(const FrameData& currentFrame);
	
	std::vector<IndirectBatch> indirectBatches;

	struct ModelWithIndirectDrawId
	{
		vkglTF::Model* model;
		uint32_t indirectDrawId;
	};

	void compactRenderObjectsIntoDraws(
		const FrameData& currentFrame,
#ifdef _DEVELOP
		std::vector<size_t> onlyPoolIndices,
		std::vector<ModelWithIndirectDrawId>& outIndirectDrawCommandIdsForPoolIndex
#endif
		);
	void renderRenderObjects(VkCommandBuffer cmd, const FrameData& currentFrame);

	bool searchForPickedObjectPoolIndex(size_t& outPoolIndex);
	void renderPickedObject(VkCommandBuffer cmd, const FrameData& currentFrame, const std::vector<ModelWithIndirectDrawId>& indirectDrawCommandIds);

public:
	//
	// Camera
	//
	Camera* _camera;

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

	enum PBRWorkflows { PBR_WORKFLOW_METALLIC_ROUGHNESS = 0, PBR_WORKFLOW_SPECULAR_GLOSSINESS = 1 };

	struct PBRMaterialParam
	{
		// Texture map references
		uint32_t colorMapIndex;
		uint32_t physicalDescriptorMapIndex;
		uint32_t normalMapIndex;
		uint32_t aoMapIndex;
		uint32_t emissiveMapIndex;

		// Material properties
		vec4    baseColorFactor;
		vec4    emissiveFactor;
		vec4    diffuseFactor;
		vec4    specularFactor;
		float_t workflow;
		int32_t colorTextureSet;
		int32_t PhysicalDescriptorTextureSet;
		int32_t normalTextureSet;
		int32_t occlusionTextureSet;
		int32_t emissiveTextureSet;
		float_t metallicFactor;
		float_t roughnessFactor;
		float_t alphaMask;
		float_t alphaMaskCutoff;
	};

private:
	//
	// Entities
	//
	EntityManager* _entityManager;

#ifdef _DEVELOP
private:	
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
	void updateDebugStats(const float_t& deltaTime);

	//
	// Moving matrices around
	//
	struct MovingMatrix
	{
		mat4* matrixToMove = nullptr;
	} _movingMatrix;
	void submitSelectedRenderObjectId(int32_t poolIndex);
public:
	mat4* getMatrixToMove() { return _movingMatrix.matrixToMove; }
private:

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
	void renderImGuiContent(float_t deltaTime, ImGuiIO& io);
	void renderImGui(float_t deltaTime);
#endif

    friend class Entity;
	friend Entity* scene::spinupNewObject(const std::string& objectName, VulkanEngine* engine, DataSerialized* ds);
};
