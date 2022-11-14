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
		glm::vec3 min;
		glm::vec3 max;
		bool valid = false;

		BoundingBox();
		BoundingBox(glm::vec3 min, glm::vec3 max);
		BoundingBox getAABB(glm::mat4 m);
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

	struct PBRMaterial    // @NOTE: this uses the vulkan engine's _pbrTexturesSetLayout as the base pipeline
	{
		enum AlphaMode { ALPHAMODE_OPAQUE, ALPHAMODE_MASK, ALPHAMODE_BLEND };
		AlphaMode alphaMode = ALPHAMODE_OPAQUE;
		float alphaCutoff = 1.0f;
		float metallicFactor = 1.0f;
		float roughnessFactor = 1.0f;
		glm::vec4 baseColorFactor = glm::vec4(1.0f);
		glm::vec4 emissiveFactor = glm::vec4(1.0f);
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
			glm::vec4 diffuseFactor = glm::vec4(1.0f);
			glm::vec3 specularFactor = glm::vec3(0.0f);
		} extension;
	
		struct PbrWorkflows
		{
			bool metallicRoughness = true;
			bool specularGlossiness = false;
		} pbrWorkflows;
	
		Material calculatedMaterial;    // @NOTE: this will contain the pbr pipeline, the pbr pipeline layout, and the texture descriptorset for this pbrmaterial instance
	};

	struct Primitive
	{
		uint32_t firstIndex;
		uint32_t indexCount;
		uint32_t vertexCount;
		PBRMaterial& material;
		bool hasIndices;
		BoundingBox bb;
		Primitive(uint32_t firstIndex, uint32_t indexCount, uint32_t vertexCount, PBRMaterial& material);
		void setBoundingBox(glm::vec3 min, glm::vec3 max);
	};

	struct Mesh
	{
		std::vector<Primitive*> primitives;
		BoundingBox bb;
		BoundingBox aabb;

		uint32_t animatorMeshId;

		Mesh();
		~Mesh();
		void setBoundingBox(glm::vec3 min, glm::vec3 max);
	};

	struct Skin
	{
		std::string name;
		Node* skeletonRoot = nullptr;
		std::vector<glm::mat4> inverseBindMatrices;
		std::vector<Node*> joints;
	};

	struct Animator;

	struct Node
	{
		Node* parent;
		uint32_t index;
		std::vector<Node*> children;
		glm::mat4 matrix;
		std::string name;
		Mesh* mesh;
		Skin* skin;
		int32_t skinIndex = -1;
		glm::vec3 translation{};
		glm::vec3 scale{ 1.0f };
		glm::quat rotation{};
		BoundingBox bvh;
		BoundingBox aabb;

		void generateCalculateJointMatrixTaskflow(Animator* animator, tf::Taskflow& taskflow, tf::Task* taskPrerequisite);
		glm::mat4 localMatrix();
		glm::mat4 getMatrix();
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
		std::vector<glm::vec4> outputsVec4;
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

		struct Transition
		{
			std::string triggerName;  // @NOTE: just for setup
			size_t      triggerIndex;
			std::string toStateName;  // @NOTE: just for setup
			size_t      toStateIndex;
		};

		struct Trigger
		{
			std::string triggerName;  // @NOTE: this is just for setup referencing. Indices should be used once this setup finishes (i.e. don't do any string comps after setup)
			bool        activated = false;
		};

		struct State
		{
			std::string             stateName = "";  // @NOTE: just for referencing for debugging etc. after setup
			std::string             animationName;
			uint32_t                animationIndex;
			bool                    loop;
			OnFinish                onFinish;
			std::vector<Transition> transitions;
		};

		bool                          loaded = false;
		std::vector<Trigger>          triggers;
		std::vector<State>            states;
		std::map<std::string, size_t> triggerNameToIndex;  // @NOTE: at the very most, the entity owning the animator should be using this, not the internal animator code!
	};

	struct Model
	{
		struct Vertex
		{
			glm::vec3 pos;
			glm::vec3 normal;
			glm::vec2 uv0;
			glm::vec2 uv1;
			glm::vec4 joint0;
			glm::vec4 weight0;
			glm::vec4 color;
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

		glm::mat4 aabb;

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
			glm::vec3 min = glm::vec3(FLT_MAX);
			glm::vec3 max = glm::vec3(-FLT_MAX);
		} dimensions;

		struct LoaderInfo
		{
			uint32_t* indexBuffer;
			Vertex* vertexBuffer;
			size_t indexPos = 0;
			size_t vertexPos = 0;
		};

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
		void draw(VkCommandBuffer commandBuffer, uint32_t transformID, std::function<void(Primitive* primitive, Node* node)>&& perPrimitiveFunction);    // You know, I wonder about the overhead of including a lambda per primitive
	private:
		void drawNode(Node* node, VkCommandBuffer commandBuffer, uint32_t transformID, std::function<void(Primitive* primitive, Node* node)>&& perPrimitiveFunction);
		void calculateBoundingBox(Node* node, Node* parent);
	public:
		void getSceneDimensions();
	private:
		Node* findNode(Node* parent, uint32_t index);
		Node* nodeFromIndex(uint32_t index);

	private:
		VulkanEngine* engine;
		StateMachine  animStateMachine;

		friend struct Animator;
	};

	struct Animator
	{
		Animator(vkglTF::Model* model);
		~Animator();

		static void initializeEmpty(VulkanEngine* engine);
		static void destroyEmpty(VulkanEngine* engine);
		static VkDescriptorSet* getEmptyJointDescriptorSet();  // For binding to represent a non-skinned mesh

		void playAnimation(uint32_t animationIndex, bool loop, float_t time = 0.0f);  // This is for direct control of the animation index
		void update(const float_t& deltaTime);

		void setTrigger(const std::string& triggerName);

	private:
		vkglTF::Model* model;
		VulkanEngine* engine;
		StateMachine  animStateMachineCopy;
		size_t asmStateIndex;

		void updateAnimation();
		void updateJointMatrices(uint32_t animatorMeshId, vkglTF::Skin* skin, glm::mat4& m);
	public:
		glm::mat4 getJointMatrix(const std::string& jointName);
	private:

		uint32_t animationIndex;
		float_t  time;
		bool     loop;

		struct UniformBuffer
		{
			AllocatedBuffer descriptorBuffer;
			VkDescriptorSet descriptorSet;
			void* mapped;
		};
		std::vector<UniformBuffer> uniformBuffers;

		struct UniformBlock
		{
			glm::mat4 matrix;
			glm::mat4 jointMatrix[MAX_NUM_JOINTS]{};
			float_t jointcount{ 0 };
		};
		std::vector<UniformBlock> uniformBlocks;

		tf::Taskflow calculateJointMatricesTaskflow;
		tf::Executor taskflowExecutor;

		// @SPECIAL: empty animator for empty joint descriptor sets
		static UniformBuffer emptyUBuffer;
		static UniformBlock  emptyUBlock;
	public:
		UniformBuffer& getUniformBuffer(size_t index);

		friend struct Node;
	};
}
