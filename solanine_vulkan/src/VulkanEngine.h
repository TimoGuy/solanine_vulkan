#pragma once

#include "Settings.h"
#include "VkDataStructures.h"
#include "EntityManager.h"
#include "SceneManagement.h"


struct SDL_Window;
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
	vec4 boundingSphere;
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

struct UIQuadSettingsConstBlock
{
	vec4 tint;
	float_t useNineSlicing;
	float_t nineSlicingBoundX1;
	float_t nineSlicingBoundX2;
	float_t nineSlicingBoundY1;
	float_t nineSlicingBoundY2;
};

struct GPUCoCParams
{
	float_t cameraZNear;
	float_t cameraZFar;
	float_t focusDepth;
	float_t focusExtent;
	float_t blurExtent;
};

struct GPUBlurParams
{
	vec2 oneOverImageExtent;
};

struct GPUGatherDOFParams
{
	float_t sampleRadiusMultiplier;
	float_t oneOverArbitraryResExtentX;
	float_t oneOverArbitraryResExtentY;
};

struct GPUPostProcessParams
{
	bool applyTonemap;
	bool pad0;
	bool pad1;
	bool pad2;  // Vulkan spec requires multiple of 4 bytes for push constants.
};

struct GPUCullingParams
{
	mat4     view;
	float_t  zNear;
	float_t  zFar;
	float_t  frustumX_x;
	float_t  frustumX_z;
	float_t  frustumY_y;
	float_t  frustumY_z;
	uint32_t cullingEnabled;
	uint32_t numInstances;
};

struct GPUIndirectDrawCommandOffsetsData
{
	uint32_t batchFirstIndex;
	uint32_t countIndex;
	uint32_t pad0;
	uint32_t pad1;
};

struct GPUInputSkinningMeshPrefixData
{
	uint32_t numVertices;
    uint32_t pad0;
    uint32_t pad1;
    uint32_t pad2;
};

struct GPUInputSkinningMeshData
{
	vec3 pos;
    uint32_t animatorNodeID;  // @NOTE: insert offset here so that 16 byte padding can be complete and normal doesn't get garbage values.  -Timo 2023/12/16
    vec3 normal;
    uint32_t baseInstanceID;  // Here too.
    vec2 UV0;
    vec2 UV1;
    vec4 joint0;
    vec4 weight0;
    vec4 color0;
};

struct GPUOutputSkinningMeshData
{
	vec3 pos;
    uint32_t instanceIDOffset;  // @NOTE: insert offset here so that 16 byte padding can be complete and normal doesn't get garbage values.  -Timo 2023/12/16
    vec3 normal;
	uint32_t pad0;  // Here too.
    vec2 UV0;
    vec2 UV1;
    vec4 color0;
};

struct FrameData
{
	VkSemaphore presentSemaphore, renderSemaphore;
	VkFence renderFence, pickingRenderFence;

	VkCommandPool commandPool;
	VkCommandBuffer mainCommandBuffer;
	VkCommandBuffer pickingCommandBuffer;

	struct IndirectDrawCommands
	{
		AllocatedBuffer indirectDrawCommandsBuffer;
		AllocatedBuffer indirectDrawCommandOffsetsBuffer;
		AllocatedBuffer indirectDrawCommandCountsBuffer;
		VkDescriptorSet indirectDrawCommandDescriptor;
	};
	AllocatedBuffer      indirectDrawCommandRawBuffer;
	IndirectDrawCommands indirectDrawCommandsShadowPass;
	IndirectDrawCommands indirectDrawCommandsMainPass;
	uint32_t             numInstances;

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

	struct ComputeSkinning
	{
		uint64_t        numVertices;
		uint64_t        numIndices;
		AllocatedBuffer inputVerticesBuffer;
		AllocatedBuffer outputVerticesBuffer;
		uint64_t        outputBufferSize;
		AllocatedBuffer indicesBuffer;
		VkDescriptorSet inoutVerticesDescriptor;
		bool created                    = false;
		bool recalculateSkinningBuffers = true;
	} skinning;
};

struct UploadContext
{
	VkFence uploadFence;
	VkCommandPool commandPool;
	VkCommandBuffer commandBuffer;
};

class VulkanEngine
{
public:
	void init();
	void run();
	void cleanup();

	bool _isInitialized{ false };
	uint32_t _frameNumber{ 0 };

	VkExtent2D _windowExtent{ 1920, 1080 };
	// VkExtent2D _windowExtent{ 1280, 720 };
	SDL_Window* _window = nullptr;
	bool _windowFullscreen = false;
	bool _isWindowMinimized = false;    // @NOTE: if we don't handle window minimization correctly, we can get the VK_ERROR_DEVICE_LOST(-4) error
	bool _recreateSwapchain = false;

	void setWindowFullscreen(bool isFullscreen);

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
	Texture        _depthImage;
	VkFormat       _depthFormat;

	//
	// Texture for taking a snapshot of the rendered game screen
	// (to warp for screen transitions, pause menus, etc.)
	//
	Texture       _snapshotImage;
	bool          _blitToSnapshotImageFlag = false;
	bool          _skyboxIsSnapshotImage = false;

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

	// Depth of Field
	float_t        _DOFSampleRadiusMultiplier = 0.75f;

	VkRenderPass   _CoCRenderPass;
	VkFramebuffer  _CoCFramebuffer;
	Texture        _CoCImage;
	VkSampler      _CoCImageMaxSampler;

	VkRenderPass   _halveCoCRenderPass;
	VkFramebuffer  _halveCoCFramebuffer;
	Texture        _nearFieldImage;
	Texture        _farFieldImage;
	VkExtent2D     _halfResImageExtent;

// 1: half, 2: quarter, 3: eighth, 4: sixteenth
#define NUM_INCREMENTAL_COC_REDUCTIONS 4
	VkRenderPass   _incrementalReductionHalveCoCRenderPass;
	VkFramebuffer  _incrementalReductionHalveCoCFramebuffers[NUM_INCREMENTAL_COC_REDUCTIONS];
	Texture        _nearFieldIncrementalReductionHalveResCoCImages[NUM_INCREMENTAL_COC_REDUCTIONS];
	VkExtent2D     _incrementalReductionHalveResImageExtents[NUM_INCREMENTAL_COC_REDUCTIONS];

	VkRenderPass   _blurXNearsideCoCRenderPass;
	VkFramebuffer  _blurXNearsideCoCFramebuffer;
	VkRenderPass   _blurYNearsideCoCRenderPass;
	VkFramebuffer  _blurYNearsideCoCFramebuffer;
	Texture        _nearFieldIncrementalReductionHalveResCoCImagePongImage;  // This is what I'm calling the separate image for ping-pong buffers (i.e. gaussian blurring).

	VkRenderPass   _gatherDOFRenderPass;
	VkFramebuffer  _gatherDOFFramebuffer;
	Texture        _nearFieldImagePongImage;
	Texture        _farFieldImagePongImage;

	VkRenderPass   _dofFloodFillRenderPass;
	VkFramebuffer  _dofFloodFillFramebuffer;

	VkSampler      _nearFieldImageLinearSampler;
	VkSampler      _farFieldImageLinearSampler;

	VkDescriptorSetLayout _dofSingleTextureLayout;
	VkDescriptorSetLayout _dofDoubleTextureLayout;
	VkDescriptorSetLayout _dofTripleTextureLayout;

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
	VkDescriptorSetLayout _computeCullingIndirectDrawCommandSetLayout;
	VkDescriptorSetLayout _computeSkinningInoutVerticesSetLayout;

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
	void initPostprocessImages();
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

	void loadMaterials();
	void loadMeshes();

	void uploadCurrentFrameToGPU(const FrameData& currentFrame);
	void createSkinningBuffers(FrameData& currentFrame);
	void destroySkinningBuffersIfCreated(FrameData& currentFrame);
	
	std::vector<IndirectBatch> indirectBatches;

	struct ModelWithIndirectDrawId
	{
		vkglTF::Model* model;
		uint32_t indirectDrawId;
	};

	void compactRenderObjectsIntoDraws(
		FrameData& currentFrame,
#ifdef _DEVELOP
		std::vector<size_t> onlyPoolIndices,
		std::vector<ModelWithIndirectDrawId>& outIndirectDrawCommandIdsForPoolIndex
#endif
		);
	void renderRenderObjects(VkCommandBuffer cmd, const FrameData& currentFrame, bool materialOverride);

	bool searchForPickedObjectPoolIndex(size_t& outPoolIndex);
	void renderPickedObject(VkCommandBuffer cmd, const FrameData& currentFrame, const std::vector<ModelWithIndirectDrawId>& indirectDrawCommandIds);

	void computeCulling(const FrameData& currentFrame, VkCommandBuffer cmd);
	void computeSkinnedMeshes(const FrameData& currentFrame, VkCommandBuffer cmd);
	void renderPickingRenderpass(const FrameData& currentFrame);
	void renderShadowRenderpass(const FrameData& currentFrame, VkCommandBuffer cmd);
	void renderMainRenderpass(const FrameData& currentFrame, VkCommandBuffer cmd, const std::vector<ModelWithIndirectDrawId>& pickingIndirectDrawCommandIds);
	void renderUIRenderpass(VkCommandBuffer cmd);
	void renderPostprocessRenderpass(const FrameData& currentFrame, VkCommandBuffer cmd, uint32_t swapchainImageIndex);

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
	void updateDebugStats(float_t deltaTime);

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
	// Editor Modes
	// @TODO: move this to a place that's not here!
	//
	enum class EditorModes
	{
		LEVEL_EDITOR,
		MATERIAL_EDITOR,
	};
	EditorModes _currentEditorMode = EditorModes::MATERIAL_EDITOR;
	void changeEditorMode(EditorModes newEditorMode);

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
	friend void scene::tick();
	friend Entity* scene::spinupNewObject(const std::string& objectName, DataSerialized* ds);
};
