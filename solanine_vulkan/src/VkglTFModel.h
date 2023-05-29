/**
 * Vulkan glTF model and texture loading class based on tinyglTF (https://github.com/syoyo/tinygltf)
 *
 * Copyright (C) 2018-2022 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#pragma once

#include "ImportGLM.h"
#include <taskflow/taskflow.hpp>
#include "VkDataStructures.h"

class VulkanEngine;

// ERROR is already defined in wingdi.h and collides with a define in the Draco headers
#if defined(_WIN32) && defined(ERROR) && defined(TINYGLTF_ENABLE_DRACO) 
#undef ERROR
#pragma message ("ERROR constant already defined, undefining")
#endif

#include "tiny_gltf.h"

#if defined(__ANDROID__)
#include <android/asset_manager.h>
#endif

// Changing this value here also requires changing it in the vertex shader
#define MAX_NUM_JOINTS 128u

namespace vkglTF
{
	struct Node;

	struct BoundingBox
	{
		vec3 min;
		vec3 max;
		bool valid = false;

		BoundingBox();
		BoundingBox(vec3 min, vec3 max);
		BoundingBox getAABB(mat4 m);
	};

	struct TextureSampler
	{
		VkFilter magFilter;
		VkFilter minFilter;
		VkSamplerMipmapMode mipmapMode;
		VkSamplerAddressMode addressModeU;
		VkSamplerAddressMode addressModeV;
		VkSamplerAddressMode addressModeW;
	};

	struct PBRMaterial
	{
		enum AlphaMode { ALPHAMODE_OPAQUE, ALPHAMODE_MASK, ALPHAMODE_BLEND };
		AlphaMode alphaMode = ALPHAMODE_OPAQUE;
		float alphaCutoff = 1.0f;
		float metallicFactor = 1.0f;
		float roughnessFactor = 1.0f;
		vec4 baseColorFactor = GLM_VEC4_ONE_INIT;
		vec4 emissiveFactor = GLM_VEC4_ONE_INIT;
		Texture* baseColorTexture;
		Texture* metallicRoughnessTexture;
		Texture* normalTexture;
		Texture* occlusionTexture;
		Texture* emissiveTexture;
		bool doubleSided = false;
	
		struct TexCoordSets
		{
			uint8_t baseColor = 0;
			uint8_t metallicRoughness = 0;
			uint8_t specularGlossiness = 0;
			uint8_t normal = 0;
			uint8_t occlusion = 0;
			uint8_t emissive = 0;
		} texCoordSets;
	
		struct Extension
		{
			Texture* specularGlossinessTexture;
			Texture* diffuseTexture;
			vec4 diffuseFactor = GLM_VEC4_ONE_INIT;
			vec3 specularFactor = GLM_VEC3_ZERO_INIT;
		} extension;
	
		struct PbrWorkflows
		{
			bool metallicRoughness = true;
			bool specularGlossiness = false;
		} pbrWorkflows;

		struct TexturePtr
		{
			uint32_t colorMapIndex = 0;
			uint32_t physicalDescriptorMapIndex = 0;
			uint32_t normalMapIndex = 0;
			uint32_t aoMapIndex = 0;
			uint32_t emissiveMapIndex = 0;
		} texturePtr;
	};

	struct Primitive
	{
		uint32_t firstIndex;
		uint32_t indexCount;
		uint32_t vertexCount;
		bool hasIndices;
		BoundingBox bb;
		uint32_t materialID;
		size_t   animatorNodeReservedIndexPropagatedCopy;
		Primitive(uint32_t firstIndex, uint32_t indexCount, uint32_t vertexCount, uint32_t materialID);
		void setBoundingBox(vec3 min, vec3 max);
	};

	struct Mesh
	{
		std::vector<Primitive*> primitives;
		BoundingBox bb;
		BoundingBox aabb;

		size_t animatorNodeReservedIndex;

		Mesh();
		~Mesh();
		void setBoundingBox(vec3 min, vec3 max);
	};

	struct Skin
	{
		std::string name;
		Node* skeletonRoot = nullptr;
		std::vector<mat4s> inverseBindMatrices;  // @NOCHECKIN
		std::vector<Node*> joints;
	};

	struct Animator;

	struct Node
	{
		Node* parent;
		uint32_t index;
		std::vector<Node*> children;
		mat4 matrix;
		std::string name;
		Mesh* mesh;
		Skin* skin;
		int32_t skinIndex = -1;
		vec3 translation = GLM_VEC3_ZERO_INIT;
		vec3 scale = GLM_VEC3_ONE_INIT;
		versor rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
		BoundingBox bvh;
		BoundingBox aabb;

		void generateCalculateJointMatrixTaskflow(Animator* animator, tf::Taskflow& taskflow, tf::Task* taskPrerequisite);
		void localMatrix(mat4& out);
		void getMatrix(mat4& out);
		void update(Animator* animator);
		~Node();
	};

	struct AnimationChannel
	{
		enum PathType { TRANSLATION, ROTATION, SCALE };
		PathType path;
		Node* node;
		uint32_t samplerIndex;
	};

	struct AnimationSampler
	{
		enum InterpolationType { LINEAR, STEP, CUBICSPLINE };
		InterpolationType interpolation;
		std::vector<float> inputs;
		std::vector<vec4s> outputsVec4;  // @NOCHECKIN
	};

	struct Animation
	{
		std::string name;
		std::vector<AnimationSampler> samplers;
		std::vector<AnimationChannel> channels;
		float start = std::numeric_limits<float>::max();
		float end = std::numeric_limits<float>::min();
	};

	struct VertexInputDescription
	{
		std::vector<VkVertexInputBindingDescription> bindings;
		std::vector<VkVertexInputAttributeDescription> attributes;
		VkPipelineVertexInputStateCreateFlags flags = 0;
	};

	// Animator State Machine held by Model for the Animator
	struct StateMachine
	{
		struct OnFinish
		{
			bool useOnFinish = false;
			std::string toStateName;  // @NOTE: just for setup
			size_t      toStateIndex;
		};

		enum class TransitionType
		{
			TRIGGER_ACTIVATED,
			CURRENT_STATE,
			NOT_CURRENT_STATE,
		};

		struct Transition
		{
			TransitionType type;
			std::string    triggerName;  // @NOTE: just for setup
			size_t         triggerIndex;
			std::string    checkingStateName;  // This name compiles down to the mask index and state index
			size_t         checkingMaskIndex;
			size_t         checkingStateIndex;
			std::string    toStateName;  // @NOTE: just for setup
			size_t         toStateIndex;
		};

		struct Trigger
		{
			std::string triggerName;  // @NOTE: this is just for setup referencing. Indices should be used once this setup finishes (i.e. don't do any string comps after setup)
			bool        activated = false;
		};

		struct Event
		{
			float_t     eventCallAt = 0.0f;  // @NOTE: at least for now this is a percentage value
			std::string eventName;  // @NOTE: just for setup
			size_t      eventIndex  = (size_t)-1;  // @NOTE: this will be compiled at the animator-owned copy level
		};

		struct State
		{
			std::string             stateName = "";  // @NOTE: just for referencing for debugging etc. after setup
			std::string             maskName  = "";  // @NOTE: just for setup
			std::string             animationName;
			uint32_t                animationIndex;
			bool                    loop;
			OnFinish                onFinish;
			std::vector<Transition> transitions;
			std::vector<Event>      events;
		};

		struct Mask
		{
			std::string              maskName = "";
			size_t                   asmStateIndex = 0;  // This will represent this mask's current state
			bool                     enabled = false;  // @NOTE: the asm still runs for masks that are disabled, however, `enabled` just sets whether it gets applied to the final joints
			std::vector<State>       states;
			std::vector<std::string> boneNameList;  // @NOTE: just for setup
			std::vector<Node*>       boneRefList;
		};

		struct MaskPlayer
		{
			uint32_t animationIndex;  // @TODO: instead of having this be the end all be all, have each bone capture this number as a pointer, so that this will be the index for the global layer, and have the AnimStateMachine carry the mask animation indices and have the relevant bones point to that animationIndex using a pointer  -Timo 2022/11/19
			float_t  time;
			bool     loop;

			// Temp data holders:
			bool     animEndedThisFrame;
			float_t  animDuration;
			vec2     timeRange;
		};

		bool                          loaded = false;
		std::vector<Trigger>          triggers;
		std::vector<Mask>             masks;  // @NOTE: masks[0] is the global mask
		std::vector<MaskPlayer>       maskPlayers;  // @NOTE: maskPlayers[0] is the global mask player
		std::map<std::string, size_t> triggerNameToIndex;  // @NOTE: at the very most, the entity owning the animator should be using this, not the internal animator code!
	};

	struct Model
	{
		struct PBRTextureCollection
		{
			std::vector<Texture*> textures;
		} static pbrTextureCollection;

		struct PBRMaterialCollection
		{
			std::vector<PBRMaterial*> materials;
		} static pbrMaterialCollection;

		struct Vertex
		{
			vec3 pos;
			vec3 normal;
			vec2 uv0;
			vec2 uv1;
			vec4 joint0;
			vec4 weight0;
			vec4 color;
			static VertexInputDescription getVertexDescription();
		};

		struct Vertices
		{
			VkBuffer buffer = VK_NULL_HANDLE;
			VmaAllocation allocation;
		} vertices;

		struct Indices
		{
			int count;
			VkBuffer buffer = VK_NULL_HANDLE;
			VmaAllocation allocation;
		} indices;

		mat4 aabb;

		std::vector<Node*> nodes;
		std::vector<Node*> linearNodes;

		std::vector<Skin*> skins;

		std::vector<Texture> textures;
		std::vector<TextureSampler> textureSamplers;
		std::vector<PBRMaterial> materials;
		std::vector<Animation> animations;
		std::vector<std::string> extensions;

		struct Dimensions
		{
			vec3 min = { FLT_MAX, FLT_MAX, FLT_MAX };
			vec3 max = { -FLT_MAX, -FLT_MAX , -FLT_MAX };
		} dimensions;

		struct LoaderInfo
		{
			uint32_t* indexBuffer;
			Vertex* vertexBuffer;
			size_t indexCount;
			size_t vertexCount;
			size_t indexPos = 0;
			size_t vertexPos = 0;
		} loaderInfo;

		void destroy(VmaAllocator allocator);
	private:
		void loadNode(vkglTF::Node* parent, const tinygltf::Node& node, uint32_t nodeIndex, const tinygltf::Model& model, LoaderInfo& loaderInfo, float globalscale);
		void getNodeProps(const tinygltf::Node& node, const tinygltf::Model& model, size_t& vertexCount, size_t& indexCount);
		void loadSkins(tinygltf::Model& gltfModel);
		void loadTextures(tinygltf::Model& gltfModel);
		VkSamplerAddressMode getVkWrapMode(int32_t wrapMode);
		VkFilter getVkFilterMode(int32_t filterMode);
		VkSamplerMipmapMode getVkMipmapModeMode(int32_t filterMode);
		void loadTextureSamplers(tinygltf::Model& gltfModel);
		void loadMaterials(tinygltf::Model& gltfModel);    // @NOTE: someday it might be beneficial to have some kind of material override for any model.  -Timo
		void loadAnimations(tinygltf::Model& gltfModel);
		void loadAnimationStateMachine(const std::string& filename, tinygltf::Model& gltfModel);
	public:
		void loadFromFile(VulkanEngine* engine, std::string filename, float scale = 1.0f);
		void bind(VkCommandBuffer commandBuffer);
		void draw(VkCommandBuffer commandBuffer);
		void draw(VkCommandBuffer commandBuffer, uint32_t& inOutInstanceID);
		void appendPrimitiveDraws(std::vector<MeshCapturedInfo>& draws, uint32_t& appendedCount);
	private:
		void drawNode(Node* node, VkCommandBuffer commandBuffer, uint32_t& inOutInstanceID);
		void appendPrimitiveDrawNode(Node* node, std::vector<MeshCapturedInfo>& draws, uint32_t& appendedCount);
		void calculateBoundingBox(Node* node, Node* parent);
	public:
		void getSceneDimensions();
		std::vector<Primitive*> getAllPrimitivesInOrder();
	private:
		Node* findNode(Node* parent, uint32_t index);
		Node* nodeFromIndex(uint32_t index);
	public:
		std::vector<Node*> fetchAllNodesWithAMesh();  // @NOTE: this is probs a computationally expensive thing, hence using "fetch" instead of "get"

	private:
		VulkanEngine* engine;
		StateMachine  animStateMachine;


		friend struct Animator;
	};

	struct Animator
	{
		struct AnimatorCallback
		{
			std::string           eventName;
			std::function<void()> callback;
		};

		Animator(vkglTF::Model* model, std::vector<AnimatorCallback>& eventCallbacks);
		~Animator();

		static void initializeEmpty(VulkanEngine* engine);
		static void destroyEmpty(VulkanEngine* engine);
		static VkDescriptorSet* getGlobalAnimatorNodeCollectionDescriptorSet();  // For binding to represent a non-skinned mesh

		void playAnimation(size_t maskIndex, uint32_t animationIndex, bool loop, float_t time = 0.0f);  // This is for direct control of the animation index
		void update(const float_t& deltaTime);

		void runEvent(const std::string& eventName);  // @NOTE: this is really naive btw
		void setTrigger(const std::string& triggerName);
		void setMask(const std::string& maskName, bool enabled);
		void setTwitchAngle(float_t radians);

	private:
		vkglTF::Model*                model;
		VulkanEngine*                 engine;
		StateMachine                  animStateMachineCopy;
		std::vector<AnimatorCallback> eventCallbacks;
		float_t                       twitchAngle;

		void updateAnimation();
		void updateJointMatrices(size_t animatorNodeReservedIndex, vkglTF::Skin* skin, mat4& m);
	public:
		bool getJointMatrix(const std::string& jointName, mat4& out);
	private:

		struct GPUAnimatorNode
		{
			mat4 matrix;
			mat4 jointMatrix[MAX_NUM_JOINTS]{};
			float_t jointcount{ 0 };
		};
		static GPUAnimatorNode uniformBlocks[];

		struct AnimatorNodeCollectionBuffer
		{
			AllocatedBuffer buffer;
			VkDescriptorSet descriptorSet;
			GPUAnimatorNode* mapped;
		};
		static AnimatorNodeCollectionBuffer nodeCollectionBuffer;
		static std::vector<size_t> reservedNodeCollectionIndices;

		std::vector<size_t> myReservedNodeCollectionIndices;

		tf::Taskflow calculateJointMatricesTaskflow;
		tf::Executor taskflowExecutor;

	public:
		friend struct Node;
	};
}
