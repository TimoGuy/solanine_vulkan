/**
 * Vulkan glTF model and texture loading class based on tinyglTF (https://github.com/syoyo/tinygltf)
 *
 * Copyright (C) 2018-2022 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#include "Imports.h"
#include "VkglTFModel.h"
#include "VkDescriptorBuilderUtil.h"
#include "VulkanEngine.h"
#include "PhysicsEngine.h"
#include "VkTextures.h"
#include "VkInitializers.h"
#include "StringHelper.h"
#include <chrono>
#include <taskflow/algorithm/for_each.hpp>


namespace vkglTF
{
	//
	// Bounding box
	//
	BoundingBox::BoundingBox() { };
	BoundingBox::BoundingBox(vec3 min, vec3 max)
	{
		glm_vec3_copy(min, this->min);
		glm_vec3_copy(max, this->max);
	}

	BoundingBox BoundingBox::getAABB(mat4 m)
	{
		vec3 min;
		glm_vec3_copy(m[3], min);
		vec3 max;
		glm_vec3_copy(min, max);
		vec3 v0, v1;

		vec3 right;
		glm_vec3_copy(m[0], right);
		glm_vec3_scale(right, this->min[0], v0);
		glm_vec3_scale(right, this->max[0], v1);
		glm_vec3_minadd(v0, v1, min);
		glm_vec3_maxadd(v0, v1, max);

		vec3 up;
		glm_vec3_copy(m[1], up);
		glm_vec3_scale(up, this->min[1], v0);
		glm_vec3_scale(up, this->max[1], v1);
		glm_vec3_minadd(v0, v1, min);
		glm_vec3_maxadd(v0, v1, max);

		vec3 back;
		glm_vec3_copy(m[2], back);
		glm_vec3_scale(back, this->min[2], v0);
		glm_vec3_scale(back, this->max[2], v1);
		glm_vec3_minadd(v0, v1, min);
		glm_vec3_maxadd(v0, v1, max);

		return BoundingBox(min, max);
	}

	//
	// Primitive
	//
	Primitive::Primitive(uint32_t firstIndex, uint32_t indexCount, uint32_t vertexCount, uint32_t materialID) : firstIndex(firstIndex), indexCount(indexCount), vertexCount(vertexCount), materialID(materialID)
	{
		hasIndices = indexCount > 0;
	}

	void Primitive::setBoundingBox(vec3 min, vec3 max)
	{
		glm_vec3_copy(min, bb.min);
		glm_vec3_copy(max, bb.max);
		bb.valid = true;
	}

	//
	// Mesh
	//
	Mesh::Mesh() : animatorSkinIndex(0)
	{
	}

	Mesh::~Mesh()
	{
		for (Primitive* p : primitives)
			delete p;
	}

	void Mesh::setBoundingBox(vec3 min, vec3 max)
	{
		glm_vec3_copy(min, bb.min);
		glm_vec3_copy(max, bb.max);
		bb.valid = true;
	}

	//
	// Node
	//
	void Node::localMatrix(mat4& out)
	{
		glm_mat4_identity(out);
		glm_translate(out, translation);
		glm_quat_rotate(out, rotation, out);
		glm_scale(out, scale);
	}

	void Node::getMatrix(mat4& out)
	{
		localMatrix(out);
		vkglTF::Node* p = parent;
		while (p)
		{
			mat4 lm;
			p->localMatrix(lm);
			glm_mat4_mul(lm, out, out);
			p = p->parent;
		}
	}

	Node::~Node()
	{
		if (mesh)
		{
			delete mesh;
		}
		for (auto& child : children)
		{
			delete child;
		}
	}

	//
	// Vertex
	//
	VertexInputDescription Model::Vertex::getVertexDescription()
	{
		VertexInputDescription description;

		//
		// 1 vertex buffer binding
		//
		VkVertexInputBindingDescription mainBinding = {
			.binding = 0,
			.stride = sizeof(Vertex),
			.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
		};
		description.bindings.push_back(mainBinding);

		//
		// Attributes
		//
		VkVertexInputAttributeDescription posAttribute = {
			.location = 0,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(Vertex, pos),
		};
		VkVertexInputAttributeDescription normalAttribute = {
			.location = 1,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(Vertex, normal),
		};
		VkVertexInputAttributeDescription uv0Attribute = {
			.location = 2,
			.binding = 0,
			.format = VK_FORMAT_R32G32_SFLOAT,
			.offset = offsetof(Vertex, uv0),
		};
		VkVertexInputAttributeDescription uv1Attribute = {
			.location = 3,
			.binding = 0,
			.format = VK_FORMAT_R32G32_SFLOAT,
			.offset = offsetof(Vertex, uv1),
		};
		VkVertexInputAttributeDescription joint0Attribute = {
			.location = 4,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32A32_SFLOAT,
			.offset = offsetof(Vertex, joint0),
		};
		VkVertexInputAttributeDescription weight0Attribute = {
			.location = 5,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32A32_SFLOAT,
			.offset = offsetof(Vertex, weight0),
		};
		VkVertexInputAttributeDescription colorAttribute = {
			.location = 6,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32A32_SFLOAT,
			.offset = offsetof(Vertex, color),
		};

		description.attributes.push_back(posAttribute);
		description.attributes.push_back(normalAttribute);
		description.attributes.push_back(uv0Attribute);
		description.attributes.push_back(uv1Attribute);
		description.attributes.push_back(joint0Attribute);
		description.attributes.push_back(weight0Attribute);
		description.attributes.push_back(colorAttribute);
		return description;
	}

	//
	// Model
	//
	Model::PBRTextureCollection Model::pbrTextureCollection;
	Model::PBRMaterialCollection Model::pbrMaterialCollection;

	void Model::destroy(VmaAllocator allocator)
	{
		if (vertices.buffer != VK_NULL_HANDLE)
		{
			vmaDestroyBuffer(allocator, vertices.buffer, vertices.allocation);
			vertices.buffer = VK_NULL_HANDLE;
		}
		if (indices.buffer != VK_NULL_HANDLE)
		{
			vmaDestroyBuffer(allocator, indices.buffer, indices.allocation);
			indices.buffer = VK_NULL_HANDLE;
		}
		/*for (auto texture : textures)    // @TODO: have some kind of texture deletion routine... maybe similar to what's going on with the _mainDeletionQueue???
			texture.destroy();
		textures.resize(0);
		textureSamplers.resize(0);*/
		for (auto node : nodes)
		{
			delete node;
		}
		//materials.resize(0);
		animations.resize(0);
		nodes.resize(0);
		linearNodes.resize(0);
		extensions.resize(0);
		for (auto skin : skins)
		{
			delete skin;
		}
		skins.resize(0);
	};

	void Model::loadNode(vkglTF::Node* parent, const tinygltf::Node& node, uint32_t nodeIndex, const tinygltf::Model& model, LoaderInfo& loaderInfo, float globalscale)
	{
		vkglTF::Node* newNode = new Node{};
		newNode->index = nodeIndex;
		newNode->parent = parent;
		newNode->name = node.name;
		newNode->skinIndex = node.skin;
		glm_mat4_identity(newNode->matrix);

		// Generate local node matrix
		//vec3 translation = GLM_VEC3_ZERO_INIT;
		if (node.translation.size() == 3)
		{
			const double_t* data = node.translation.data();
			vec3 translation = {
				data[0], data[1], data[2],
			};
			glm_vec3_copy(translation, newNode->translation);
		}
		//versor rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
		if (node.rotation.size() == 4)
		{
			const double_t* data = node.rotation.data();
			versor rotation = {
				data[0], data[1], data[2], data[3],
			};
			glm_vec4_copy(rotation, newNode->rotation);
		}
		//vec3 scale = GLM_VEC3_ONE_INIT;
		if (node.scale.size() == 3)
		{
			const double_t* data = node.scale.data();
			vec3 scale = {
				data[0], data[1], data[2],
			};
			glm_vec3_copy(scale, newNode->scale);
		}
		if (node.matrix.size() == 16)
		{
			const double_t* data = node.matrix.data();
			mat4 matrix = {
				 data[0],  data[1],  data[2],  data[3],
				 data[4],  data[5],  data[6],  data[7],
				 data[8],  data[9], data[10], data[11],
				data[12], data[13], data[14], data[15],
			};
			glm_mat4_copy(matrix, newNode->matrix);
		};

		// Node with children
		if (node.children.size() > 0)
		{
			for (size_t i = 0; i < node.children.size(); i++)
			{
				loadNode(newNode, model.nodes[node.children[i]], node.children[i], model, loaderInfo, globalscale);
			}
		}

		// Node contains mesh data
		if (node.mesh > -1)
		{
			const tinygltf::Mesh mesh = model.meshes[node.mesh];
			Mesh* newMesh = new Mesh();
			for (size_t j = 0; j < mesh.primitives.size(); j++)
			{
				const tinygltf::Primitive& primitive = mesh.primitives[j];
				uint32_t vertexStart = static_cast<uint32_t>(loaderInfo.vertexPos);
				uint32_t indexStart = static_cast<uint32_t>(loaderInfo.indexPos);
				uint32_t indexCount = 0;
				uint32_t vertexCount = 0;
				vec3 posMin{};
				vec3 posMax{};
				bool hasSkin = false;
				bool hasIndices = primitive.indices > -1;
				// Vertices
				{
					const float* bufferPos = nullptr;
					const float* bufferNormals = nullptr;
					const float* bufferTexCoordSet0 = nullptr;
					const float* bufferTexCoordSet1 = nullptr;
					const float* bufferColorSet0 = nullptr;
					const void* bufferJoints = nullptr;
					const float* bufferWeights = nullptr;

					int posByteStride;
					int normByteStride;
					int uv0ByteStride;
					int uv1ByteStride;
					int color0ByteStride;
					int jointByteStride;
					int weightByteStride;

					int jointComponentType;

					// Position attribute is required
					assert(primitive.attributes.find("POSITION") != primitive.attributes.end());

					const tinygltf::Accessor& posAccessor = model.accessors[primitive.attributes.find("POSITION")->second];
					const tinygltf::BufferView& posView = model.bufferViews[posAccessor.bufferView];
					bufferPos = reinterpret_cast<const float*>(&(model.buffers[posView.buffer].data[posAccessor.byteOffset + posView.byteOffset]));
					vec3 posMin_ = { posAccessor.minValues[0], posAccessor.minValues[1], posAccessor.minValues[2] };
					glm_vec3_copy(posMin_, posMin);
					vec3 posMax_ = { posAccessor.maxValues[0], posAccessor.maxValues[1], posAccessor.maxValues[2] };
					glm_vec3_copy(posMax_, posMax);
					vertexCount = static_cast<uint32_t>(posAccessor.count);
					posByteStride = posAccessor.ByteStride(posView) ? (posAccessor.ByteStride(posView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC3);

					if (primitive.attributes.find("NORMAL") != primitive.attributes.end())
					{
						const tinygltf::Accessor& normAccessor = model.accessors[primitive.attributes.find("NORMAL")->second];
						const tinygltf::BufferView& normView = model.bufferViews[normAccessor.bufferView];
						bufferNormals = reinterpret_cast<const float*>(&(model.buffers[normView.buffer].data[normAccessor.byteOffset + normView.byteOffset]));
						normByteStride = normAccessor.ByteStride(normView) ? (normAccessor.ByteStride(normView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC3);
					}

					// UVs
					if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end())
					{
						const tinygltf::Accessor& uvAccessor = model.accessors[primitive.attributes.find("TEXCOORD_0")->second];
						const tinygltf::BufferView& uvView = model.bufferViews[uvAccessor.bufferView];
						bufferTexCoordSet0 = reinterpret_cast<const float*>(&(model.buffers[uvView.buffer].data[uvAccessor.byteOffset + uvView.byteOffset]));
						uv0ByteStride = uvAccessor.ByteStride(uvView) ? (uvAccessor.ByteStride(uvView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC2);
					}
					if (primitive.attributes.find("TEXCOORD_1") != primitive.attributes.end())
					{
						const tinygltf::Accessor& uvAccessor = model.accessors[primitive.attributes.find("TEXCOORD_1")->second];
						const tinygltf::BufferView& uvView = model.bufferViews[uvAccessor.bufferView];
						bufferTexCoordSet1 = reinterpret_cast<const float*>(&(model.buffers[uvView.buffer].data[uvAccessor.byteOffset + uvView.byteOffset]));
						uv1ByteStride = uvAccessor.ByteStride(uvView) ? (uvAccessor.ByteStride(uvView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC2);
					}

					// Vertex colors
					if (primitive.attributes.find("COLOR_0") != primitive.attributes.end())
					{
						const tinygltf::Accessor& accessor = model.accessors[primitive.attributes.find("COLOR_0")->second];
						const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
						bufferColorSet0 = reinterpret_cast<const float*>(&(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
						color0ByteStride = accessor.ByteStride(view) ? (accessor.ByteStride(view) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC3);
					}

					// Skinning
					// Joints
					if (primitive.attributes.find("JOINTS_0") != primitive.attributes.end())
					{
						const tinygltf::Accessor& jointAccessor = model.accessors[primitive.attributes.find("JOINTS_0")->second];
						const tinygltf::BufferView& jointView = model.bufferViews[jointAccessor.bufferView];
						bufferJoints = &(model.buffers[jointView.buffer].data[jointAccessor.byteOffset + jointView.byteOffset]);
						jointComponentType = jointAccessor.componentType;
						jointByteStride = jointAccessor.ByteStride(jointView) ? (jointAccessor.ByteStride(jointView) / tinygltf::GetComponentSizeInBytes(jointComponentType)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC4);
					}

					if (primitive.attributes.find("WEIGHTS_0") != primitive.attributes.end())
					{
						const tinygltf::Accessor& weightAccessor = model.accessors[primitive.attributes.find("WEIGHTS_0")->second];
						const tinygltf::BufferView& weightView = model.bufferViews[weightAccessor.bufferView];
						bufferWeights = reinterpret_cast<const float*>(&(model.buffers[weightView.buffer].data[weightAccessor.byteOffset + weightView.byteOffset]));
						weightByteStride = weightAccessor.ByteStride(weightView) ? (weightAccessor.ByteStride(weightView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC4);
					}

					hasSkin = (bufferJoints && bufferWeights);

					for (size_t v = 0; v < posAccessor.count; v++)
					{
						Vertex& vert = loaderInfo.vertexBuffer[loaderInfo.vertexPos];

						const float_t* bp = &bufferPos[v * posByteStride];
						vec3 pos = { bp[0], bp[1], bp[2] };
						glm_vec3_copy(pos, vert.pos);

						if (bufferNormals)
						{
							const float_t* bn = &bufferNormals[v * normByteStride];
							vec3 normal = { bn[0], bn[1], bn[2] };
							glm_normalize(normal);
							glm_vec3_copy(normal, vert.normal);
						}
						else
							glm_vec3_zero(vert.normal);

						if (bufferTexCoordSet0)
						{
							const float_t* btcs0 = &bufferTexCoordSet0[v * uv0ByteStride];
							vec2 uv0 = { btcs0[0], btcs0[1] };
							glm_vec2_copy(uv0, vert.uv0);
						}
						else
							glm_vec2_zero(vert.uv0);

						if (bufferTexCoordSet1)
						{
							const float_t* btcs1 = &bufferTexCoordSet1[v * uv1ByteStride];
							vec2 uv1 = { btcs1[0], btcs1[1] };
							glm_vec2_copy(uv1, vert.uv1);
						}
						else
							glm_vec2_zero(vert.uv1);

						if (bufferColorSet0)
						{
							const float_t* bcs0 = &bufferColorSet0[v * color0ByteStride];
							vec4 color = { bcs0[0], bcs0[1], bcs0[2], bcs0[3] };
							glm_vec4_copy(color, vert.color);
						}
						else
							glm_vec4_one(vert.color);

						if (hasSkin)
						{
							switch (jointComponentType)
							{
							case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
							{
								const uint16_t* buf = static_cast<const uint16_t*>(bufferJoints);
								const uint16_t* b = &buf[v * jointByteStride];
								vec4 joint0 = {
									b[0], b[1], b[2], b[3],
								};
								glm_vec4_copy(joint0, vert.joint0);
								break;
							}
							case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
							{
								const uint8_t* buf = static_cast<const uint8_t*>(bufferJoints);
								const uint8_t* b = &buf[v * jointByteStride];
								vec4 joint0 = {
									b[0], b[1], b[2], b[3],
								};
								glm_vec4_copy(joint0, vert.joint0);
								break;
							}
							default:
								// Not supported by spec
								std::cerr << "Joint component type " << jointComponentType << " not supported!" << std::endl;
								break;
							}
						}
						else
						{
							glm_vec4_zero(vert.joint0);
						}

						// Add skin weight
						if (hasSkin)
						{
							const float_t* bw = &bufferWeights[v * weightByteStride];
							vec4 bufferWeightsV4 = {
								bw[0],
								bw[1],
								bw[2],
								bw[3],
							};
							glm_vec4_copy(bufferWeightsV4, vert.weight0);
						}
						else
							glm_vec4_zero(vert.weight0);

						// Fix for all zero weights
						if (glm_vec3_norm(vert.weight0) == 0.0f)  // @TODO: may wanna use `_norm2` instead
						{
							vec4 x1 = { 1.0f, 0.0f, 0.0f, 0.0f };
							glm_vec4_copy(x1, vert.weight0);
						}
						loaderInfo.vertexPos++;
					}
				}
				// Indices
				if (hasIndices)
				{
					const tinygltf::Accessor& accessor = model.accessors[primitive.indices > -1 ? primitive.indices : 0];
					const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
					const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];

					indexCount = static_cast<uint32_t>(accessor.count);
					const void* dataPtr = &(buffer.data[accessor.byteOffset + bufferView.byteOffset]);

					switch (accessor.componentType)
					{
					case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT:
					{
						const uint32_t* buf = static_cast<const uint32_t*>(dataPtr);
						for (size_t index = 0; index < accessor.count; index++)
						{
							loaderInfo.indexBuffer[loaderInfo.indexPos] = buf[index] + vertexStart;
							loaderInfo.indexPos++;
						}
						break;
					}
					case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT:
					{
						const uint16_t* buf = static_cast<const uint16_t*>(dataPtr);
						for (size_t index = 0; index < accessor.count; index++)
						{
							loaderInfo.indexBuffer[loaderInfo.indexPos] = buf[index] + vertexStart;
							loaderInfo.indexPos++;
						}
						break;
					}
					case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE:
					{
						const uint8_t* buf = static_cast<const uint8_t*>(dataPtr);
						for (size_t index = 0; index < accessor.count; index++)
						{
							loaderInfo.indexBuffer[loaderInfo.indexPos] = buf[index] + vertexStart;
							loaderInfo.indexPos++;
						}
						break;
					}
					default:
						std::cerr << "Index component type " << accessor.componentType << " not supported!" << std::endl;
						return;
					}
				}
				
				uint32_t materialID = 0;  // Default material
				if (primitive.material >= 0)
				{
					// Find index of the material in the global material collection
					auto& v = pbrMaterialCollection.materials;
					for (size_t i = 0; i < v.size(); i++)
						if (v[i] == &materials[primitive.material])
						{
							materialID = i;
							break;
						}
				}
				Primitive* newPrimitive = new Primitive(indexStart, indexCount, vertexCount, materialID);
				newPrimitive->setBoundingBox(posMin, posMax);
				newMesh->primitives.push_back(newPrimitive);
			}
			// Mesh BB from BBs of primitives
			for (auto p : newMesh->primitives)
			{
				if (p->bb.valid && !newMesh->bb.valid)
				{
					newMesh->bb = p->bb;
					newMesh->bb.valid = true;
				}
				glm_vec3_minv(newMesh->bb.min, p->bb.min, newMesh->bb.min);
				glm_vec3_maxv(newMesh->bb.max, p->bb.max, newMesh->bb.max);
			}
			newNode->mesh = newMesh;
		}
		if (parent)
		{
			parent->children.push_back(newNode);
		}
		else
		{
			nodes.push_back(newNode);
		}
		linearNodes.push_back(newNode);
	}

	void Model::getNodeProps(const tinygltf::Node& node, const tinygltf::Model& model, size_t& vertexCount, size_t& indexCount)
	{
		if (node.children.size() > 0)
		{
			for (size_t i = 0; i < node.children.size(); i++)
			{
				getNodeProps(model.nodes[node.children[i]], model, vertexCount, indexCount);
			}
		}
		if (node.mesh > -1)
		{
			const tinygltf::Mesh mesh = model.meshes[node.mesh];
			for (size_t i = 0; i < mesh.primitives.size(); i++)
			{
				auto primitive = mesh.primitives[i];
				vertexCount += model.accessors[primitive.attributes.find("POSITION")->second].count;
				if (primitive.indices > -1)
				{
					indexCount += model.accessors[primitive.indices].count;
				}
			}
		}
	}

	void Model::loadSkins(tinygltf::Model& gltfModel)
	{
		for (tinygltf::Skin& source : gltfModel.skins)
		{
			Skin* newSkin = new Skin{};
			newSkin->name = source.name;

			// Find skeleton root node
			if (source.skeleton > -1)
			{
				newSkin->skeletonRoot = nodeFromIndex(source.skeleton);
			}

			// Find joint nodes
			for (int jointIndex : source.joints)
			{
				Node* node = nodeFromIndex(jointIndex);
				if (node)
				{
					newSkin->joints.push_back(nodeFromIndex(jointIndex));
				}
			}

			// Get inverse bind matrices from buffer
			if (source.inverseBindMatrices > -1)
			{
				const tinygltf::Accessor& accessor = gltfModel.accessors[source.inverseBindMatrices];
				const tinygltf::BufferView& bufferView = gltfModel.bufferViews[accessor.bufferView];
				const tinygltf::Buffer& buffer = gltfModel.buffers[bufferView.buffer];
				newSkin->inverseBindMatrices.resize(accessor.count);  // @NOCHECKIN
				memcpy(newSkin->inverseBindMatrices.data(), &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(mat4));
			}

			skins.push_back(newSkin);
		}
	}

	void Model::loadTextures(tinygltf::Model& gltfModel)
	{
		for (tinygltf::Texture& tex : gltfModel.textures)
		{
			tinygltf::Image image = gltfModel.images[tex.source];
			vkglTF::TextureSampler textureSampler;
			if (tex.sampler > -1)
			{
				textureSampler = textureSamplers[tex.sampler];
			}
			else
			{
				// No sampler specified, use a default one
				textureSampler.magFilter = VK_FILTER_LINEAR;
				textureSampler.minFilter = VK_FILTER_LINEAR;
				textureSampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
				textureSampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
				textureSampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
				textureSampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			}

			//
			// Load texture
			//
			unsigned char* buffer = nullptr;
			VkDeviceSize bufferSize = 0;
			bool deleteBuffer = false;
			if (image.component == 3)
			{
				// Most devices don't support RGB only on Vulkan so convert
				// TODO: Check actual format support and transform only if required
				bufferSize = image.width * image.height * 4;
				buffer = new unsigned char[bufferSize];
				unsigned char* rgba = buffer;
				unsigned char* rgb = &image.image[0];
				for (int32_t i = 0; i < image.width * image.height; i++)
				{
					for (int32_t j = 0; j < 3; ++j)
					{
						rgba[j] = rgb[j];
					}
					rgba += 4;
					rgb += 3;
				}
				deleteBuffer = true;
			}
			else
			{
				buffer = &image.image[0];
				bufferSize = image.image.size();
			}

			VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;

			Texture texture;
			vkutil::loadImageFromBuffer(*engine, image.width, image.height, bufferSize, format, buffer, 0, texture.image);

			if (deleteBuffer)
				delete[] buffer;

			VkImageViewCreateInfo imageInfo = vkinit::imageviewCreateInfo(format, texture.image._image, VK_IMAGE_ASPECT_COLOR_BIT, texture.image._mipLevels);
			vkCreateImageView(engine->_device, &imageInfo, nullptr, &texture.imageView);

			VkSamplerCreateInfo samplerInfo = {
				.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
				.pNext = nullptr,
				.magFilter = textureSampler.magFilter,
				.minFilter = textureSampler.minFilter,
				.mipmapMode = textureSampler.mipmapMode,
				.addressModeU = textureSampler.addressModeU,
				.addressModeV = textureSampler.addressModeV,
				.addressModeW = textureSampler.addressModeW,
				.mipLodBias = 0.0f,
				.anisotropyEnable = VK_TRUE,
				.maxAnisotropy = engine->_gpuProperties.limits.maxSamplerAnisotropy,
				.compareEnable = VK_FALSE,
				.compareOp = VK_COMPARE_OP_NEVER,
				.minLod = 0.0f,
				.maxLod = static_cast<float_t>(texture.image._mipLevels),
				.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
				.unnormalizedCoordinates = VK_FALSE,
			};
			vkCreateSampler(engine->_device, &samplerInfo, nullptr, &texture.sampler);

			engine->_mainDeletionQueue.pushFunction([=]() {  // @NOTE: images are already destroyed and handled by VkTextures.h/.cpp so only the sampler and imageview get destroyed here.
				vkDestroySampler(engine->_device, texture.sampler, nullptr);
				vkDestroyImageView(engine->_device, texture.imageView, nullptr);
				});

			textures.push_back(texture);
		}
	}

	VkSamplerAddressMode Model::getVkWrapMode(int32_t wrapMode)
	{
		switch (wrapMode)
		{
		case 10497:
			return VK_SAMPLER_ADDRESS_MODE_REPEAT;
		case 33071:
			return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		case 33648:
			return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
		default:
			return VK_SAMPLER_ADDRESS_MODE_REPEAT;
		}
	}

	VkFilter Model::getVkFilterMode(int32_t filterMode)
	{
		switch (filterMode)
		{
		case 9728:
			return VK_FILTER_NEAREST;
		case 9729:
			return VK_FILTER_LINEAR;
		case 9984:
			return VK_FILTER_NEAREST;
		case 9985:
			return VK_FILTER_NEAREST;
		case 9986:
			return VK_FILTER_LINEAR;
		case 9987:
			return VK_FILTER_LINEAR;
		default:
			return VK_FILTER_LINEAR;
		}
	}

	VkSamplerMipmapMode Model::getVkMipmapModeMode(int32_t filterMode)
	{
		VkFilter filterModeEnum = getVkFilterMode(filterMode);
		if (filterModeEnum == VK_FILTER_NEAREST)
			return VK_SAMPLER_MIPMAP_MODE_NEAREST;
		return VK_SAMPLER_MIPMAP_MODE_LINEAR;   // @NOTE: it's only these two options for mipmaps, so the outlier of the VkFilter enum is the NEAREST option.
	}

	void Model::loadTextureSamplers(tinygltf::Model& gltfModel)
	{
		for (tinygltf::Sampler smpl : gltfModel.samplers)
		{
			vkglTF::TextureSampler sampler = {
				.magFilter = getVkFilterMode(smpl.magFilter),
				.minFilter = getVkFilterMode(smpl.minFilter),
				.mipmapMode = getVkMipmapModeMode(smpl.minFilter),
				.addressModeU = getVkWrapMode(smpl.wrapS),
				.addressModeV = getVkWrapMode(smpl.wrapT),
				.addressModeW = sampler.addressModeV,
			};
			textureSamplers.push_back(sampler);
		}
	}

	void Model::loadMaterials(tinygltf::Model& gltfModel)
	{
		//
		// Create PBRMaterials with the properties
		// of the pbr workflow in the gltf model's materials
		//
		for (tinygltf::Material& mat : gltfModel.materials)
		{
			vkglTF::PBRMaterial material = {};

			material.doubleSided = mat.doubleSided;

			if (mat.values.find("baseColorTexture") != mat.values.end())
			{
				material.baseColorTexture = &textures[mat.values["baseColorTexture"].TextureIndex()];
				material.texCoordSets.baseColor = mat.values["baseColorTexture"].TextureTexCoord();
			}

			if (mat.values.find("metallicRoughnessTexture") != mat.values.end())
			{
				material.metallicRoughnessTexture = &textures[mat.values["metallicRoughnessTexture"].TextureIndex()];
				material.texCoordSets.metallicRoughness = mat.values["metallicRoughnessTexture"].TextureTexCoord();
			}

			if (mat.values.find("roughnessFactor") != mat.values.end())
			{
				material.roughnessFactor = static_cast<float_t>(mat.values["roughnessFactor"].Factor());
			}

			if (mat.values.find("metallicFactor") != mat.values.end())
			{
				material.metallicFactor = static_cast<float_t>(mat.values["metallicFactor"].Factor());
			}

			if (mat.values.find("baseColorFactor") != mat.values.end())
			{
				double_t* data = mat.values["baseColorFactor"].ColorFactor().data();
				vec4 baseColorFactor = {
					data[0],
					data[1],
					data[2],
					data[3],
				};
				glm_vec4_copy(baseColorFactor, material.baseColorFactor);
			}

			if (mat.additionalValues.find("normalTexture") != mat.additionalValues.end())
			{
				material.normalTexture = &textures[mat.additionalValues["normalTexture"].TextureIndex()];
				material.texCoordSets.normal = mat.additionalValues["normalTexture"].TextureTexCoord();
			}

			if (mat.additionalValues.find("emissiveTexture") != mat.additionalValues.end())
			{
				material.emissiveTexture = &textures[mat.additionalValues["emissiveTexture"].TextureIndex()];
				material.texCoordSets.emissive = mat.additionalValues["emissiveTexture"].TextureTexCoord();
			}

			if (mat.additionalValues.find("occlusionTexture") != mat.additionalValues.end())
			{
				material.occlusionTexture = &textures[mat.additionalValues["occlusionTexture"].TextureIndex()];
				material.texCoordSets.occlusion = mat.additionalValues["occlusionTexture"].TextureTexCoord();
			}

			if (mat.additionalValues.find("alphaMode") != mat.additionalValues.end())
			{
				tinygltf::Parameter param = mat.additionalValues["alphaMode"];
				if (param.string_value == "BLEND")
				{
					material.alphaMode = PBRMaterial::ALPHAMODE_BLEND;
				}
				if (param.string_value == "MASK")
				{
					material.alphaCutoff = 0.5f;
					material.alphaMode = PBRMaterial::ALPHAMODE_MASK;
				}
			}

			if (mat.additionalValues.find("alphaCutoff") != mat.additionalValues.end())
			{
				material.alphaCutoff = static_cast<float_t>(mat.additionalValues["alphaCutoff"].Factor());
			}

			if (mat.additionalValues.find("emissiveFactor") != mat.additionalValues.end())
			{
				double_t* data = mat.additionalValues["emissiveFactor"].ColorFactor().data();
				vec4 emissiveFactor = {
					data[0],
					data[1],
					data[2],
					1.0f,
				};
				glm_vec4_copy(emissiveFactor, material.emissiveFactor);
			}

			// Extensions
			// @TODO: Find out if there is a nicer way of reading these properties with recent tinygltf headers
			if (mat.extensions.find("KHR_materials_pbrSpecularGlossiness") != mat.extensions.end())
			{
				auto ext = mat.extensions.find("KHR_materials_pbrSpecularGlossiness");
				if (ext->second.Has("specularGlossinessTexture"))
				{
					auto index = ext->second.Get("specularGlossinessTexture").Get("index");
					material.extension.specularGlossinessTexture = &textures[index.Get<int>()];
					auto texCoordSet = ext->second.Get("specularGlossinessTexture").Get("texCoord");
					material.texCoordSets.specularGlossiness = texCoordSet.Get<int>();
					material.pbrWorkflows.specularGlossiness = true;
				}
				if (ext->second.Has("diffuseTexture"))
				{
					auto index = ext->second.Get("diffuseTexture").Get("index");
					material.extension.diffuseTexture = &textures[index.Get<int>()];
				}
				if (ext->second.Has("diffuseFactor"))
				{
					auto factor = ext->second.Get("diffuseFactor");
					for (uint32_t i = 0; i < factor.ArrayLen(); i++)
					{
						auto val = factor.Get(i);
						material.extension.diffuseFactor[i] = val.IsNumber() ? (float_t)val.Get<double>() : (float_t)val.Get<int>();
					}
				}
				if (ext->second.Has("specularFactor"))
				{
					auto factor = ext->second.Get("specularFactor");
					for (uint32_t i = 0; i < factor.ArrayLen(); i++)
					{
						auto val = factor.Get(i);
						material.extension.specularFactor[i] = val.IsNumber() ? (float_t)val.Get<double>() : (float_t)val.Get<int>();
					}
				}
			}

			materials.push_back(material);
		}

		//
		// Load in empty textures for initial index of each texture map
		//
		{
			static std::mutex emptyTextureMapMutex;
			std::lock_guard<std::mutex> lg(emptyTextureMapMutex);
			
			if (pbrTextureCollection.textures.empty())
				pbrTextureCollection.textures.push_back(&engine->_loadedTextures["empty"]);
		}

		//
		// Create default material for meshes with no material assigned
		//
		{
			static std::mutex emptyMaterialCollectionMutex;
			std::lock_guard<std::mutex> lg(emptyMaterialCollectionMutex);
			
			if (pbrMaterialCollection.materials.empty())
				pbrMaterialCollection.materials.push_back(new PBRMaterial());  // @TODO: @NOCHECKIN: delete the heap object somewhere upon shutdown!
		}

		//
		// Create descriptorsets per material
		// 
		// @TODO: This is a @FEATURE for the future, however, it'd be really
		//        nice for there to be a way to create and override materials
		//        for a model.  -Timo
		//
		for (PBRMaterial& material : materials)
		{
			static std::mutex colorMapMutex;
			static std::mutex physicalDescriptorMapMutex;
			static std::mutex normalMapMutex;
			static std::mutex aoMapMutex;
			static std::mutex emissiveMapMutex;

			// TODO: glTF specs states that metallic roughness should be preferred, even if specular glossiness is present
			if (material.pbrWorkflows.metallicRoughness)
			{
				if (material.baseColorTexture)
				{
					std::lock_guard<std::mutex> lg(colorMapMutex);
					material.texturePtr.colorMapIndex = pbrTextureCollection.textures.size();
					pbrTextureCollection.textures.push_back(material.baseColorTexture);
				}
				if (material.metallicRoughnessTexture)
				{
					std::lock_guard<std::mutex> lg(physicalDescriptorMapMutex);
					material.texturePtr.physicalDescriptorMapIndex = pbrTextureCollection.textures.size();
					pbrTextureCollection.textures.push_back(material.metallicRoughnessTexture);
				}
			}

			if (material.pbrWorkflows.specularGlossiness)
			{
				if (material.extension.diffuseTexture)
				{
					std::lock_guard<std::mutex> lg(colorMapMutex);
					material.texturePtr.colorMapIndex = pbrTextureCollection.textures.size();
					pbrTextureCollection.textures.push_back(material.extension.diffuseTexture);
				}
				if (material.extension.specularGlossinessTexture)
				{
					std::lock_guard<std::mutex> lg(physicalDescriptorMapMutex);
					material.texturePtr.physicalDescriptorMapIndex = pbrTextureCollection.textures.size();
					pbrTextureCollection.textures.push_back(material.extension.specularGlossinessTexture);
				}
			}

			if (material.normalTexture)
			{
				std::lock_guard<std::mutex> lg(normalMapMutex);
				material.texturePtr.normalMapIndex = pbrTextureCollection.textures.size();
				pbrTextureCollection.textures.push_back(material.normalTexture);
			}

			if (material.occlusionTexture)
			{
				std::lock_guard<std::mutex> lg(aoMapMutex);
				material.texturePtr.aoMapIndex = pbrTextureCollection.textures.size();
				pbrTextureCollection.textures.push_back(material.occlusionTexture);
			}

			if (material.emissiveTexture)
			{
				std::lock_guard<std::mutex> lg(emissiveMapMutex);
				material.texturePtr.emissiveMapIndex = pbrTextureCollection.textures.size();
				pbrTextureCollection.textures.push_back(material.emissiveTexture);
			}

			// Load in the material into the material collection
			static std::mutex materialCollectionMutex;
			std::lock_guard<std::mutex> lg(materialCollectionMutex);
			pbrMaterialCollection.materials.push_back(&material);
		}
	}

	void Model::loadAnimations(tinygltf::Model& gltfModel)
	{
		for (tinygltf::Animation& anim : gltfModel.animations)
		{
			vkglTF::Animation animation{};
			animation.name = anim.name;
			if (anim.name.empty())
			{
				animation.name = std::to_string(animations.size());
			}

			// Samplers
			for (auto& samp : anim.samplers)
			{
				vkglTF::AnimationSampler sampler{};

				if (samp.interpolation == "LINEAR")
				{
					sampler.interpolation = AnimationSampler::InterpolationType::LINEAR;
				}
				if (samp.interpolation == "STEP")
				{
					sampler.interpolation = AnimationSampler::InterpolationType::STEP;
				}
				if (samp.interpolation == "CUBICSPLINE")
				{
					sampler.interpolation = AnimationSampler::InterpolationType::CUBICSPLINE;
				}

				// Read sampler input time values
				{
					const tinygltf::Accessor& accessor = gltfModel.accessors[samp.input];
					const tinygltf::BufferView& bufferView = gltfModel.bufferViews[accessor.bufferView];
					const tinygltf::Buffer& buffer = gltfModel.buffers[bufferView.buffer];

					assert(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);

					const void* dataPtr = &buffer.data[accessor.byteOffset + bufferView.byteOffset];
					const float* buf = static_cast<const float*>(dataPtr);
					for (size_t index = 0; index < accessor.count; index++)
					{
						sampler.inputs.push_back(buf[index]);
					}

					for (auto input : sampler.inputs)
					{
						if (input < animation.start)
						{
							animation.start = input;
						};
						if (input > animation.end)
						{
							animation.end = input;
						}
					}
				}

				// Read sampler output T/R/S values 
				{
					const tinygltf::Accessor& accessor = gltfModel.accessors[samp.output];
					const tinygltf::BufferView& bufferView = gltfModel.bufferViews[accessor.bufferView];
					const tinygltf::Buffer& buffer = gltfModel.buffers[bufferView.buffer];

					assert(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);

					const void* dataPtr = &buffer.data[accessor.byteOffset + bufferView.byteOffset];

					switch (accessor.type)
					{
					case TINYGLTF_TYPE_VEC3:
					{
						const vec3* buf = static_cast<const vec3*>(dataPtr);
						for (size_t index = 0; index < accessor.count; index++)
						{
							vec4s bufAsV4 = {
								buf[index][0],
								buf[index][1],
								buf[index][2],
								0.0f,
							};
							sampler.outputsVec4.push_back(bufAsV4);  // @NOCHECKIN
						}
						break;
					}
					case TINYGLTF_TYPE_VEC4:
					{
						const vec4* buf = static_cast<const vec4*>(dataPtr);
						for (size_t index = 0; index < accessor.count; index++)
						{
							vec4s bufCopy = {
								buf[index][0],
								buf[index][1],
								buf[index][2],
								buf[index][3],
							};
							sampler.outputsVec4.push_back(bufCopy);  // @NOCHECKIN
						}
						break;
					}
					default:
					{
						std::cout << "unknown type" << std::endl;
						break;
					}
					}
				}

				animation.samplers.push_back(sampler);
			}

			// Channels
			for (auto& source : anim.channels)
			{
				vkglTF::AnimationChannel channel{};

				if (source.target_path == "rotation")
				{
					channel.path = AnimationChannel::PathType::ROTATION;
				}
				if (source.target_path == "translation")
				{
					channel.path = AnimationChannel::PathType::TRANSLATION;
				}
				if (source.target_path == "scale")
				{
					channel.path = AnimationChannel::PathType::SCALE;
				}
				if (source.target_path == "weights")
				{
					std::cout << "weights not yet supported, skipping channel" << std::endl;
					continue;
				}
				channel.samplerIndex = source.sampler;
				channel.node = nodeFromIndex(source.target_node);
				if (!channel.node)
				{
					continue;
				}

				animation.channels.push_back(channel);
			}

			animations.push_back(animation);
		}
	}

	void Model::loadAnimationStateMachine(const std::string& filename, tinygltf::Model& gltfModel)
	{
		std::string fnameCooked = (filename + ".hasm");
		std::ifstream inFile(fnameCooked);  // .hasm: Hawsoo Animation State Machine
		if (!inFile.is_open())
		{
			std::cerr << "[ASM LOADING]" << std::endl
				<< "WARNING: file \"" << fnameCooked << "\" not found, thus could not load the animation state machine." << std::endl;
			return;
		}

		{
			animStateMachine.masks.push_back(StateMachine::Mask());  // Global mask
			animStateMachine.masks[0].enabled = true;  // Global mask should always be enabled... unless if you're weird.

			std::vector<StateMachine::State> tempNewStates;
			StateMachine::State newState = {};
			StateMachine::Mask  newMask  = {};

			std::string line;
			for (size_t lineNum = 1; std::getline(inFile, line); lineNum++)  // @COPYPASTA with SceneManagement.cpp
			{
				// Prep line data
				std::string originalLine = line;

				size_t found = line.find('#');
				if (found != std::string::npos)
				{
					line = line.substr(0, found);
				}

				trim(line);
				if (line.empty())
					continue;

				// Package finished state||mask
				if (line[0] == ':' || line[0] == '~')
				{
					if (!newState.stateName.empty())
					{
						tempNewStates.push_back(newState);  // @TODO: that'd be good if there were a check on the data to make sure it has an animation assigned at the very least
						newState = {};
					}

					if (!newMask.maskName.empty())
					{
						animStateMachine.masks.push_back(newMask);
						newMask = {};
					}
				}

				// Process line
				if (line[0] == ':')
				{
					line = line.substr(1);  // Cut out the colon
					trim(line);

					// New state
					newState.stateName = line;
				}
				else if (line[0] == '~')
				{
					line = line.substr(1);  // Cut out tilde
					trim(line);

					// New mask
					newMask.maskName = line;
				}
				else if (!newState.stateName.empty())
				{
					// Find type of information to attach
					if (line.rfind("mask ", 0) == 0)
					{
						line = line.substr(sizeof("mask ") - 1);
						trim(line);

						// Assign mask
						newState.maskName = line;
					}
					else if (line.rfind("animation ", 0) == 0)
					{
						line = line.substr(sizeof("animation ") - 1);
						trim(line);

						// Assign animation
						newState.animationName = line;
					}
					else if (line.rfind("loop ", 0) == 0)
					{
						line = line.substr(sizeof("loop ") - 1);
						trim(line);

						// Assign loop
						newState.loop = (bool)std::stoi(line);
					}
					else if (line.rfind("on_finish ", 0) == 0)
					{
						line = line.substr(sizeof("on_finish ") - 1);
						trim(line);

						// Assign OnFinish
						StateMachine::OnFinish newOnFinish = {};
						newOnFinish.useOnFinish = true;
						newOnFinish.toStateName = line;
						newState.onFinish = newOnFinish;
					}
					else if (line.rfind("trans ", 0) == 0)
					{
						line = line.substr(sizeof("trans ") - 1);
						trim(line);

						std::string param0 = line.substr(0, line.find(' '));
						trim(param0);

						// Assign transition
						StateMachine::Transition newTransition = {};
						if (param0 == "current_state")
						{
							// CurrentState transition
							std::string param1 = line.substr(line.find(' '));
							trim(param1);
							std::string param2 = param1.substr(param1.find(' '));
							param1 = param1.substr(0, param1.find(' '));
							trim(param1);
							trim(param2);

							newTransition.type = StateMachine::TransitionType::CURRENT_STATE;
							newTransition.checkingStateName = param1;
							newTransition.toStateName       = param2;
						}
						else if (param0 == "not_current_state")
						{
							// NotCurrentState transition
							std::string param1 = line.substr(line.find(' '));
							trim(param1);
							std::string param2 = param1.substr(param1.find(' '));
							param1 = param1.substr(0, param1.find(' '));
							trim(param1);
							trim(param2);

							newTransition.type = StateMachine::TransitionType::NOT_CURRENT_STATE;
							newTransition.checkingStateName = param1;
							newTransition.toStateName       = param2;
						}
						else
						{
							// Trigger activated transition
							newTransition.type = StateMachine::TransitionType::TRIGGER_ACTIVATED;
							newTransition.triggerName = param0;
							newTransition.toStateName = line.substr(line.find(' '));
							trim(newTransition.triggerName);
							trim(newTransition.toStateName);
						}

						newState.transitions.push_back(newTransition);
					}
					else if (line.rfind("event ", 0) == 0)
					{
						line = line.substr(sizeof("event ") - 1);
						trim(line);

						// Assign event
						StateMachine::Event newEvent = {};
						newEvent.eventCallAt = std::stof(line);
						newEvent.eventName = line.substr(line.find(' '));
						trim(newEvent.eventName);
						newState.events.push_back(newEvent);
					}
					else
					{
						// ERROR
						std::cerr << "[ASM LOADING]" << std::endl
							<< "ERROR (line " << lineNum << ") (file: " << fnameCooked << "): Unknown type of data" << std::endl
							<< "   Trimmed line: " << line << std::endl
							<< "  Original line: " << line << std::endl;
					}
				}
				else if (!newMask.maskName.empty())
				{
					// Find type of information to attach
					if (line.rfind("enabled ", 0) == 0)
					{
						line = line.substr(sizeof("enabled ") - 1);
						trim(line);

						// Assign enabled
						newMask.enabled = (bool)std::stoi(line);
					}
					else if (line.rfind("bone ", 0) == 0)
					{
						line = line.substr(sizeof("bone ") - 1);
						trim(line);

						// Assign bone name
						newMask.boneNameList.push_back(line);
					}
					else
					{
						// ERROR
						std::cerr << "[ASM LOADING]" << std::endl
							<< "ERROR (line " << lineNum << ") (file: " << fnameCooked << "): Unknown type of data" << std::endl
							<< "   Trimmed line: " << line << std::endl
							<< "  Original line: " << line << std::endl;
					}
				}
				else
				{
					// ERROR
					std::cerr << "[ASM LOADING]" << std::endl
						<< "ERROR (line " << lineNum << ") (file: " << fnameCooked << "): Headless data" << std::endl
						<< "   Trimmed line: " << line << std::endl
						<< "  Original line: " << line << std::endl;
				}
			}

			// @COPYPASTA: @COPYPASTA: Wrap up the previous state under creation if there was one
			if (!newState.stateName.empty())
				tempNewStates.push_back(newState);
			if (!newMask.maskName.empty())
				animStateMachine.masks.push_back(newMask);

			// Add the same number of mask players as masks
			for (auto& mask : animStateMachine.masks)
				animStateMachine.maskPlayers.push_back(StateMachine::MaskPlayer());

			//
			// Assign tempNewStates to masks
			//
			for (auto& s : tempNewStates)
			{
				for (auto& m : animStateMachine.masks)
					if (s.maskName == m.maskName)
						m.states.push_back(s);
			}
		}

		//
		// Compile transition trigger names to trigger indices
		//
		for (auto& mask : animStateMachine.masks)
		{
			for (auto& state : mask.states)
			{
				for (auto& transition : state.transitions)
				{
					if (transition.type != StateMachine::TransitionType::TRIGGER_ACTIVATED)
						continue;

					bool foundInTriggerList = false;
					for (size_t i = 0; i < animStateMachine.triggers.size(); i++)
					{
						auto& trigger = animStateMachine.triggers[i];
						if (trigger.triggerName == transition.triggerName)
						{
							transition.triggerIndex = i;
							foundInTriggerList = true;
							break;
						}
					}

					if (!foundInTriggerList)
					{
						// Create new trigger in trigger list
						size_t index = animStateMachine.triggers.size();
						transition.triggerIndex = index;
						animStateMachine.triggerNameToIndex[transition.triggerName] = index;
						animStateMachine.triggers.push_back({ transition.triggerName, false });
					}
				}
			}
		}

		//
		// Compile mask bones to node pointers
		//
		for (auto& mask : animStateMachine.masks)
		{
			for (auto& boneName : mask.boneNameList)
			{
				bool assignedBone = false;

				for (auto& node : linearNodes)
				{
					if (node->name == boneName)
					{
						mask.boneRefList.push_back(node);
						assignedBone = true;
						break;
					}
				}

				if (!assignedBone)
				{
					std::cerr << "[ASM LOADING]" << std::endl
						<< "WARNING: node name \"" << boneName << "\" for mask \"" << mask.maskName << "\" was not found. No node was assigned to mask." << std::endl;
				}
			}
		}

		//
		// @NOTE: the event names are not compiled bc that will be done in the Animator-owned
		//        copy level. There, an input for a list of callbacks paired with the event name
		//        will get added and thus the event index is filled in with references to that
		//        copy's callbacks.  -Timo 2022/11/17
		//

		//
		// Compile state names to state indices
		//
		std::vector<std::map<std::string, size_t>> stateNameToIndexList;
		for (size_t i = 0; i < animStateMachine.masks.size(); i++)
		{
			// Prefill all statename to index maps
			size_t stateIndex = 0;
			std::map<std::string, size_t> stateNameToIndex;
			for (auto& state : animStateMachine.masks[i].states)
				stateNameToIndex[state.stateName] = stateIndex++;
			stateNameToIndexList.push_back(stateNameToIndex);
		}

		for (size_t i = 0; i < animStateMachine.masks.size(); i++)
		{
			auto& mask = animStateMachine.masks[i];

			for (auto& state : mask.states)
			{
				if (state.onFinish.useOnFinish)
				{
					if (stateNameToIndexList[i].find(state.onFinish.toStateName) == stateNameToIndexList[i].end())  // @COPYPASTA
					{
						std::cerr << "[ASM LOADING]" << std::endl
							<< "ERROR: Reference to non existent state" << std::endl
							<< "State: \"" << state.onFinish.toStateName << "\" was not found in animation state machine list of states"<< std::endl;
						return;
					}
					state.onFinish.toStateIndex = stateNameToIndexList[i][state.onFinish.toStateName];
				}
				for (auto& transition : state.transitions)
				{
					if (transition.type != StateMachine::TransitionType::TRIGGER_ACTIVATED)
					{
						bool found = false;

						for (size_t j = 0; j < animStateMachine.masks.size(); j++)  // Search thru all masks
						{
							if (stateNameToIndexList[j].find(transition.checkingStateName) != stateNameToIndexList[j].end())
							{
								transition.checkingMaskIndex  = j;
								transition.checkingStateIndex = stateNameToIndexList[j][transition.checkingStateName];
								found = true;
								break;
							}
						}

						if (!found)
						{
							std::cerr << "[ASM LOADING]" << std::endl
								<< "ERROR: Reference to non existent state (searched all masks)" << std::endl
								<< "State: \"" << transition.checkingStateName << "\" was not found in animation state machine list of states"<< std::endl;
							return;
						}
					}

					if (stateNameToIndexList[i].find(transition.toStateName) == stateNameToIndexList[i].end())  // @COPYPASTA
					{
						std::cerr << "[ASM LOADING]" << std::endl
							<< "ERROR: Reference to non existent state" << std::endl
							<< "State: \"" << transition.toStateName << "\" was not found in animation state machine list of states"<< std::endl;
						return;
					}
					transition.toStateIndex = stateNameToIndexList[i][transition.toStateName];
				}
			}

			//
			// Compile state animation names to animation indices
			//
			for (auto& state : mask.states)
			{
				bool foundIndex = false;
				for (size_t animInd = 0; animInd < gltfModel.animations.size(); animInd++)
				{
					auto& anim = gltfModel.animations[animInd];
					if (anim.name == state.animationName)
					{
						state.animationIndex = (uint32_t)animInd;
						foundIndex = true;
						break;
					}
				}

				if (!foundIndex)
				{
					std::cerr << "[ASM LOADING]" << std::endl
						<< "ERROR: Unknown animation" << std::endl
						<< "Anim: \"" << state.animationName << "\" was not found in model \"" << fnameCooked << "\""<< std::endl;
				}
			}
		}

		animStateMachine.loaded = true;
	}

	void Model::loadFromFile(VulkanEngine* engine, std::string filename, float scale)
	{
		this->engine = engine;

		constexpr size_t numPerfs = 11;
		std::chrono::steady_clock::time_point perfs[numPerfs];
		double_t perfsAsMS[numPerfs];
		#define PERF_TSTART(x) perfs[x] = std::chrono::high_resolution_clock::now()
		#define PERF_TEND(x) perfsAsMS[x] = std::chrono::duration<double_t, std::milli>(std::chrono::high_resolution_clock::now() - perfs[x]).count()
		#define GET_PERF_TDIFF_MS(x) perfsAsMS[x]

		PERF_TSTART(0);

		//
		// Load in data from file
		//
		tinygltf::Model gltfModel;
		tinygltf::TinyGLTF gltfContext;

		std::string error;
		std::string warning;

		bool binary = false;
		size_t extpos = filename.rfind('.', filename.length());
		if (extpos != std::string::npos)
			binary = (filename.substr(extpos + 1, filename.length() - extpos) == "glb");

		PERF_TSTART(8);
		bool fileLoaded = binary ? gltfContext.LoadBinaryFromFile(&gltfModel, &error, &warning, filename.c_str()) : gltfContext.LoadASCIIFromFile(&gltfModel, &error, &warning, filename.c_str());
		PERF_TEND(8);

		// LoaderInfo loaderInfo{ };  @TODO: @IMPROVE: @MEMORY: See below
		loaderInfo = {};

		size_t vertexCount = 0;
		size_t indexCount = 0;

		if (!fileLoaded)
		{
			std::cerr << "Could not load gltf file: " << error << std::endl;
			return;
		}

		//
		// Load gltf data into data structures
		//
		PERF_TSTART(1);
		loadTextureSamplers(gltfModel);		// @TODO: RE-ENABLE THESE. THESE ARE ALREADY IMPLEMENTED BUT THEY ARE SLOW
		PERF_TEND(1);

		PERF_TSTART(9);
		loadTextures(gltfModel);		// @TODO: RE-ENABLE THESE. THESE ARE ALREADY IMPLEMENTED BUT THEY ARE SLOW
		PERF_TEND(9);
		
		PERF_TSTART(10);
		loadMaterials(gltfModel);
		PERF_TEND(10);

		PERF_TSTART(2);
		const tinygltf::Scene& scene = gltfModel.scenes[gltfModel.defaultScene > -1 ? gltfModel.defaultScene : 0];		// TODO: scene handling with no default scene
		PERF_TEND(2);

		// Get vertex and index buffer sizes
		PERF_TSTART(3);
		for (size_t i = 0; i < scene.nodes.size(); i++)
			getNodeProps(gltfModel.nodes[scene.nodes[i]], gltfModel, vertexCount, indexCount);

		loaderInfo.indexBuffer = new uint32_t[indexCount];
		loaderInfo.vertexBuffer = new Vertex[vertexCount];
		loaderInfo.indexCount = indexCount;
		loaderInfo.vertexCount = vertexCount;
		PERF_TEND(3);

		// Load in vertices and indices
		PERF_TSTART(4);
		for (size_t i = 0; i < scene.nodes.size(); i++)
		{
			const tinygltf::Node node = gltfModel.nodes[scene.nodes[i]];
			loadNode(nullptr, node, scene.nodes[i], gltfModel, loaderInfo, scale);
		}
		PERF_TEND(4);

		// Load in animations
		PERF_TSTART(5);
		if (gltfModel.animations.size() > 0)
		{
			loadAnimations(gltfModel);
			loadAnimationStateMachine(filename, gltfModel);
		}
		loadSkins(gltfModel);

		for (auto node : linearNodes)
		{
			// Assign skins
			if (node->skinIndex > -1)
				node->skin = skins[node->skinIndex];
		}
		PERF_TEND(5);

		PERF_TSTART(6);
		extensions = gltfModel.extensionsUsed;

		size_t vertexBufferSize = vertexCount * sizeof(Vertex);
		size_t indexBufferSize = indexCount * sizeof(uint32_t);
		indices.count = static_cast<int32_t>(indexCount);

		assert(vertexBufferSize > 0);

		//
		// Upload vertices and indices to GPU
		//
		AllocatedBuffer vertexStaging, indexStaging;

		// Create staging buffers
		// Vertex data
		vertexStaging =
			engine->createBuffer(
				vertexBufferSize,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VMA_MEMORY_USAGE_CPU_ONLY
			);

		// Copy mesh to vertex staging buffer
		void* data;
		vmaMapMemory(engine->_allocator, vertexStaging._allocation, &data);
		memcpy(data, loaderInfo.vertexBuffer, vertexBufferSize);
		vmaUnmapMemory(engine->_allocator, vertexStaging._allocation);

		// Index data
		if (indexBufferSize > 0)
		{
			indexStaging =
				engine->createBuffer(
					indexBufferSize,
					VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
					VMA_MEMORY_USAGE_CPU_ONLY
				);

			// Copy indices to index staging buffer
			vmaMapMemory(engine->_allocator, indexStaging._allocation, &data);
			memcpy(data, loaderInfo.indexBuffer, indexBufferSize);
			vmaUnmapMemory(engine->_allocator, indexStaging._allocation);
		}

		// Create GPU side buffers
		// Vertex buffer
		AllocatedBuffer vertexGPUSide =
			engine->createBuffer(
				vertexBufferSize,
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VMA_MEMORY_USAGE_GPU_ONLY
			);
		vertices.buffer = vertexGPUSide._buffer;
		vertices.allocation = vertexGPUSide._allocation;

		// Index buffer
		if (indexBufferSize > 0)
		{
			AllocatedBuffer indexGPUSide =
				engine->createBuffer(
					indexBufferSize,
					VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
					VMA_MEMORY_USAGE_GPU_ONLY
				);
			indices.buffer = indexGPUSide._buffer;
			indices.allocation = indexGPUSide._allocation;
		}

		// Copy from staging buffers to GPU
		engine->immediateSubmit([&](VkCommandBuffer cmd)
			{
				VkBufferCopy copyRegion = {
					.srcOffset = 0,
					.dstOffset = 0,
					.size = vertexBufferSize,
				};
				vkCmdCopyBuffer(cmd, vertexStaging._buffer, vertices.buffer, 1, &copyRegion);

				if (indexBufferSize > 0)
				{
					copyRegion.size = indexBufferSize;
					vkCmdCopyBuffer(cmd, indexStaging._buffer, indices.buffer, 1, &copyRegion);
				}
			});

		vmaDestroyBuffer(engine->_allocator, vertexStaging._buffer, vertexStaging._allocation);
		if (indexBufferSize > 0)
			vmaDestroyBuffer(engine->_allocator, indexStaging._buffer, indexStaging._allocation);
		PERF_TEND(6);

		// @TODO: @IMPROVE: @MEMORY: figure out how to fetch the loader information bc it really should get deleted right here... or later once stuff is loaded up
		/*delete[] loaderInfo.vertexBuffer;
		delete[] loaderInfo.indexBuffer;*/

		PERF_TSTART(7);
		getSceneDimensions();
		PERF_TEND(7);

		PERF_TEND(0);

		// Report time it took to load
		static std::mutex reportModelMutex;
		std::lock_guard<std::mutex> lg(reportModelMutex);
		std::cout << "[LOAD glTF MODEL FROM FILE]" << std::endl
			<< "filename:                      " << filename << std::endl
			<< "meshes:                        " << gltfModel.meshes.size() << std::endl
			<< "animations:                    " << gltfModel.animations.size() << std::endl
			<< "materials:                     " << gltfModel.materials.size() << std::endl
			<< "images:                        " << gltfModel.images.size() << std::endl
			<< "total vertices:                " << vertexCount << std::endl
			<< "total indices:                 " << indexCount << std::endl
			<< "load data from file duration:  " << GET_PERF_TDIFF_MS(8) << " ms" << std::endl
			<< "allocate samplers duration:    " << GET_PERF_TDIFF_MS(1) << " ms" << std::endl
			<< "allocate textures duration:    " << GET_PERF_TDIFF_MS(9) << " ms" << std::endl
			<< "allocate materials duration:   " << GET_PERF_TDIFF_MS(10) << " ms" << std::endl
			<< "init scene duration:           " << GET_PERF_TDIFF_MS(2) << " ms" << std::endl
			<< "get node props duration:       " << GET_PERF_TDIFF_MS(3) << " ms" << std::endl
			<< "load nodes duration:           " << GET_PERF_TDIFF_MS(4) << " ms" << std::endl
			<< "load animations duration:      " << GET_PERF_TDIFF_MS(5) << " ms" << std::endl
			<< "load vert/ind buffer duration: " << GET_PERF_TDIFF_MS(6) << " ms" << std::endl
			<< "get scene dimensions duration: " << GET_PERF_TDIFF_MS(7) << " ms" << std::endl
			<< "total execution duration:      " << GET_PERF_TDIFF_MS(0) << " ms" << std::endl
			<< std::endl;
	}

	void Model::bind(VkCommandBuffer commandBuffer)
	{
		const VkDeviceSize offsets[1] = { 0 };
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertices.buffer, offsets);
		vkCmdBindIndexBuffer(commandBuffer, indices.buffer, 0, VK_INDEX_TYPE_UINT32);
	}

	void Model::draw(VkCommandBuffer commandBuffer)
	{
		// Just an overload that fills out all the garbage for you :)
		uint32_t _;
		draw(commandBuffer, _);
	}

	void Model::draw(VkCommandBuffer commandBuffer, uint32_t& inOutInstanceID)
	{
		for (auto& node : nodes)
		{
			drawNode(node, commandBuffer, inOutInstanceID);
		}
	}

	void Model::appendPrimitiveDraws(std::vector<MeshCapturedInfo>& draws, uint32_t& appendedCount)
	{
		for (auto& node : nodes)
		{
			appendPrimitiveDrawNode(node, draws, appendedCount);
		}
	}

	void Model::drawNode(Node* node, VkCommandBuffer commandBuffer, uint32_t& inOutInstanceID)
	{
		if (node->mesh)
		{
			for (Primitive* primitive : node->mesh->primitives)
			{
				vkCmdDrawIndexed(commandBuffer, primitive->indexCount, 1, primitive->firstIndex, 0, inOutInstanceID++);
			}
		}
		for (auto& child : node->children)
		{
			drawNode(child, commandBuffer, inOutInstanceID);
		}
	}

	void Model::appendPrimitiveDrawNode(Node* node, std::vector<MeshCapturedInfo>& draws, uint32_t& appendedCount)
	{
		if (node->mesh)
		{
			for (Primitive* primitive : node->mesh->primitives)
			{
				draws.push_back({
					.model = this,
					.meshIndexCount = primitive->indexCount,
					.meshFirstIndex = primitive->firstIndex,
				});
				appendedCount++;
			}
		}
		for (auto& child : node->children)
		{
			appendPrimitiveDrawNode(child, draws, appendedCount);
		}
	}

	void Model::calculateBoundingBox(Node* node, Node* parent)
	{
		BoundingBox parentBvh = parent ? parent->bvh : BoundingBox(dimensions.min, dimensions.max);

		if (node->mesh)
		{
			if (node->mesh->bb.valid)
			{
				mat4 m;
				node->getMatrix(m);
				node->aabb = node->mesh->bb.getAABB(m);
				if (node->children.size() == 0)
				{
					glm_vec3_copy(node->aabb.min, node->bvh.min);
					glm_vec3_copy(node->aabb.max, node->bvh.max);
					node->bvh.valid = true;
				}
			}
		}

		glm_vec3_minv(parentBvh.min, node->bvh.min, parentBvh.min);
		glm_vec3_minv(parentBvh.max, node->bvh.max, parentBvh.max);

		for (auto& child : node->children)
		{
			calculateBoundingBox(child, node);
		}
	}

	void Model::getSceneDimensions()
	{
		// Calculate binary volume hierarchy for all nodes in the scene
		for (auto node : linearNodes)
		{
			calculateBoundingBox(node, nullptr);
		}

		dimensions.min[0] = dimensions.min[1] = dimensions.min[2] = FLT_MAX;
		dimensions.max[0] = dimensions.max[1] = dimensions.max[2] = -FLT_MAX;

		for (auto node : linearNodes)
		{
			if (node->bvh.valid)
			{
				glm_vec3_minv(dimensions.min, node->bvh.min, dimensions.min);
				glm_vec3_maxv(dimensions.max, node->bvh.max, dimensions.max);
			}
		}

		// Calculate scene aabb
		vec3 scale;
		glm_vec3_sub(dimensions.max, dimensions.min, scale);
		glm_mat4_identity(aabb);
		glm_scale(aabb, scale);
		aabb[3][0] = dimensions.min[0];
		aabb[3][1] = dimensions.min[1];
		aabb[3][2] = dimensions.min[2];
	}

	void recurseFetchAllPrimitivesInOrderInNode(std::vector<Primitive*>& collection, Node* node)
	{
		if (node->mesh)
			for (Primitive* primitive : node->mesh->primitives)
			{
				// @NOTE: Propagate a copy of the index for render object manager to use
				primitive->animatorSkinIndexPropagatedCopy = node->mesh->animatorSkinIndex;
				collection.push_back(primitive);
			}

		for (auto& child : node->children)
			recurseFetchAllPrimitivesInOrderInNode(collection, child);
	}

	std::vector<Primitive*> Model::getAllPrimitivesInOrder()
	{
		std::vector<Primitive*> allPrimitives;

		for (auto& node : nodes)  // @COPYPASTA: from `drawNode()`
			recurseFetchAllPrimitivesInOrderInNode(allPrimitives, node);

		return allPrimitives;
	}

	Node* Model::findNode(Node* parent, uint32_t index)
	{
		Node* nodeFound = nullptr;
		if (parent->index == index)
		{
			return parent;
		}
		for (auto& child : parent->children)
		{
			nodeFound = findNode(child, index);
			if (nodeFound)
			{
				break;
			}
		}
		return nodeFound;
	}

	Node* Model::nodeFromIndex(uint32_t index)
	{
		Node* nodeFound = nullptr;
		for (auto& node : nodes)
		{
			nodeFound = findNode(node, index);
			if (nodeFound)
			{
				break;
			}
		}
		return nodeFound;
	}

	std::vector<Node*> Model::fetchAllNodesWithAMesh()
	{
		std::vector<Node*> nodesWithAMesh;
		for (auto& node : linearNodes)
		{
			if (node->mesh)
			{
				nodesWithAMesh.push_back(node);
			}
		}
		return nodesWithAMesh;
	}

	//
	// Animator
	//
	Animator::GPUAnimatorNode Animator::uniformBlocks[RENDER_OBJECTS_MAX_CAPACITY];
	Animator::AnimatorNodeCollectionBuffer Animator::nodeCollectionBuffer;
	std::vector<size_t> Animator::reservedNodeCollectionIndices;

	Animator::Animator(vkglTF::Model* model, std::vector<AnimatorCallback>& eventCallbacks) : model(model), eventCallbacks(eventCallbacks), twitchAngle(0.0f)
	{
		if (model == nullptr)
			return;  // @NOTE: emptyAnimator does this on purpose
			// @REPLY: Hey... wtf is `emptyAnimator`???!!??!?  -Timo 2022/11/19

		engine               = model->engine;
		animStateMachineCopy = StateMachine(model->animStateMachine);  // Make a copy to play with here

		for (auto& node : model->linearNodes)
			if (node->mesh)
				node->mesh->animatorSkinIndex = 0;  // Reset all mesh nodes to be assigned to the empty animator skin by default (@NOTE later mesh nodes will be assigned the correct skin, but this line is to prevent danglers).

		for (auto skin : model->skins)
		{
			GPUAnimatorNode newAnimatorNode = {};
			if (skin->skeletonRoot)
				skin->skeletonRoot->getMatrix(newAnimatorNode.matrix);

			// Reserve new node index
			size_t reserveIndexCandidate = (reservedNodeCollectionIndices.back() + 1) % RENDER_OBJECTS_MAX_CAPACITY;
			while (true)
			{
				bool unreserved = true;
				for (auto index : reservedNodeCollectionIndices)
					if (reserveIndexCandidate == index)
					{
						unreserved = false;
						reserveIndexCandidate = (reserveIndexCandidate + 1) % RENDER_OBJECTS_MAX_CAPACITY;
						break;
					}

				if (unreserved)
					break;  // Success! Found an empty node index
			}

			reservedNodeCollectionIndices.push_back(reserveIndexCandidate);
			myReservedNodeCollectionIndices.push_back(reserveIndexCandidate);
			for (auto& node : model->linearNodes)
				if (node->mesh && node->skin == skin)
					node->mesh->animatorSkinIndex = myReservedNodeCollectionIndices.size() - 1;
			uniformBlocks[reserveIndexCandidate] = newAnimatorNode;

			memcpy(nodeCollectionBuffer.mapped + reserveIndexCandidate, &newAnimatorNode, sizeof(GPUAnimatorNode));
		}

		// Calculate Initial Pose
		if (animStateMachineCopy.loaded)
		{
			size_t i = 0;
			for (auto& mask : animStateMachineCopy.masks)
			{
				playAnimation(i++, mask.states[0].animationIndex, mask.states[0].loop);
			}
		}
		else
			playAnimation(0, 0, true);
		updateAnimation();

		//
		// Compile the event callbacks to this copy of the animStateMachine
		//
		for (auto& mask : animStateMachineCopy.masks)
		{
			for (auto& state : mask.states)
			{
				for (auto& event : state.events)
				{
					for (size_t i = 0; i < eventCallbacks.size(); i++)
					{
						// @TODO: add warnings for callbacks that aren't defined
						if (eventCallbacks[i].eventName == event.eventName)
						{
							event.eventIndex = i;
						}
					}
				}
			}
		}
	}

	Animator::~Animator()
	{
		// Unreserve all reserved collection indices
		for (auto index : myReservedNodeCollectionIndices)
		{
			for (int32_t i = (int32_t)reservedNodeCollectionIndices.size() - 1; i >= 0; i--)
				if (reservedNodeCollectionIndices[i] == index)
				{
					reservedNodeCollectionIndices.erase(reservedNodeCollectionIndices.begin() + i);
					break;
				}
		}
	}

	void Animator::initializeEmpty(VulkanEngine* engine)  // @TODO: rename this to "initialize animator descriptor set/buffer"
	{
		//
		// @SPECIAL: create an empty animator and don't update the animation
		//
		nodeCollectionBuffer.buffer = engine->createBuffer(sizeof(GPUAnimatorNode) * RENDER_OBJECTS_MAX_CAPACITY, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

		VkDescriptorBufferInfo nodeCollectionBufferInfo = {
			.buffer = nodeCollectionBuffer.buffer._buffer,
			.offset = 0,
			.range = sizeof(GPUAnimatorNode) * RENDER_OBJECTS_MAX_CAPACITY,
		};

		vkutil::DescriptorBuilder::begin()
			.bindBuffer(0, &nodeCollectionBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
			.build(nodeCollectionBuffer.descriptorSet, engine->_skeletalAnimationSetLayout);

		// Copy non-skinned default animator
		GPUAnimatorNode defaultAnimatorNode = {};
		glm_mat4_identity(defaultAnimatorNode.matrix);

		reservedNodeCollectionIndices.push_back(0);

		void* mappedMem;
		vmaMapMemory(engine->_allocator, nodeCollectionBuffer.buffer._allocation, &mappedMem);
		nodeCollectionBuffer.mapped = (GPUAnimatorNode*)mappedMem;
		memcpy(nodeCollectionBuffer.mapped, &defaultAnimatorNode, sizeof(GPUAnimatorNode));
	}

	void Animator::destroyEmpty(VulkanEngine* engine)
	{
		vmaUnmapMemory(engine->_allocator, nodeCollectionBuffer.buffer._allocation);
		vmaDestroyBuffer(engine->_allocator, nodeCollectionBuffer.buffer._buffer, nodeCollectionBuffer.buffer._allocation);
	}

	VkDescriptorSet* Animator::getGlobalAnimatorNodeCollectionDescriptorSet()
	{
		return &nodeCollectionBuffer.descriptorSet;
	}

	void Animator::playAnimation(size_t maskIndex, uint32_t animationIndex, bool loop, float_t time)
	{
		if (model->animations.empty())
		{
			std::cout << ".glTF does not contain animation." << std::endl;
			return;
		}
		if (maskIndex > animStateMachineCopy.masks.size() - 1)
		{
			std::cout << "No mask with index " << maskIndex << std::endl;
			return;
		}
		if (animationIndex > static_cast<uint32_t>(model->animations.size()) - 1)
		{
			std::cout << "No animation with index " << animationIndex << std::endl;
			return;
		}

		animStateMachineCopy.maskPlayers[maskIndex].animationIndex = animationIndex;
		animStateMachineCopy.maskPlayers[maskIndex].loop           = loop;
		animStateMachineCopy.maskPlayers[maskIndex].time           = time;

		// @TODO: Do we need to hit updateAnimation()? This playAnimation() function will be run likely in the entity updates, so it's not like the update will be a frame late. I'm just gonna not worry about it  -Timo 2022/11/5
	}

	void Animator::update(const float_t& deltaTime)
	{
		for (auto& mp : animStateMachineCopy.maskPlayers)
		{
			mp.timeRange[0] = mp.time;
			mp.time += deltaTime;
			mp.timeRange[1] = mp.time;  // @NOTE: this has to be pre-clamped/pre-repeat because the 2nd time is exclusive in the check

			mp.animEndedThisFrame = false;
			mp.animDuration = model->animations[mp.animationIndex].end;
			if (mp.loop)
			{
				// Loop animation
				if (mp.time > mp.animDuration)
				{
					mp.time -= mp.animDuration;
					mp.animEndedThisFrame = true;
				}
			}
			else
			{
				// Clamp animation
				if (mp.time > mp.animDuration)
				{
					mp.time = mp.animDuration;
					mp.animEndedThisFrame = true;
				}
			}
		}

		// Update the state machine
		if (animStateMachineCopy.loaded)
		{
			//
			// Execute events if the time is crossed [tx-1, tx)
			//
			for (size_t i = 0; i < animStateMachineCopy.masks.size(); i++)
			{
				auto& mask = animStateMachineCopy.masks[i];
				auto& mp   = animStateMachineCopy.maskPlayers[i];

				auto& currentState = mask.states[mask.asmStateIndex];
				// glm_vec2_scale(mp.timeRange, 1.0f / mp.animDuration, mp.timeRange);  @NOTE: remove this because I want animation events to play according to the time elapsed, not a percentage of the duration.  -Timo 2023/08/09
				for (auto& event : currentState.events)
				{
					if (mp.timeRange[0] <= event.eventCallAt && event.eventCallAt < mp.timeRange[1])
					{
						std::cout << "CALLING " << event.eventName << " @ " << event.eventCallAt << std::endl;
						if (event.eventIndex >= eventCallbacks.size())
							std::cerr << "[ANIMATOR UPDATE]" << std::endl
								<< "ERROR: event \"" << event.eventName << "\" which was just referenced does not exist in the list of callbacks. Expect a crash." << std::endl;
						eventCallbacks[event.eventIndex].callback();  // This can crash... but we want that
					}
				}
			}

			//
			// The implementatino of the animatino state machine:
			//        - Have the triggers be there and have a flag bool resettriggers and set to true unless if
			//          one of the triggers actually processes to do something... and if it does, then turn
			//          off that trigger, and keep going until either all triggers are off or the state doesn't
			//          use any of the active triggers, then turn off all the triggers in that case and move on!
			//        - HOWEVER, make sure to give priority to the onFinish event!!!!! over the Transitions
			//
			bool keepLooking;
			for (size_t i = 0; i < animStateMachineCopy.masks.size(); i++)
			{
				auto& mask = animStateMachineCopy.masks[i];
				auto& mp   = animStateMachineCopy.maskPlayers[i];

				bool stateChanged = false;

				do
				{
					keepLooking = false;

					auto& currentState = mask.states[mask.asmStateIndex];
				
					// First priority, see if onFinish should get triggered
					if (mp.animEndedThisFrame && currentState.onFinish.useOnFinish)
					{
						mask.asmStateIndex = currentState.onFinish.toStateIndex;
						stateChanged = true;
						keepLooking = true;
						mp.animEndedThisFrame = false;  // New state entered; anim just started so reset this!
						continue;
					}

					// Second priority, everything else (special transitions, triggers)
					else
					{
						bool end = false;

						// Special transitions
						for (auto& transition : currentState.transitions)
						{
							// @NOTE: @TODO: @CHECK: I just noticed that the special CURRENT_STATE and NOT_CURRENT_STATE transitions happen
							//                       one frame after the trigger transformations, as in they're not reliable to happen on the
							//                       same frame that the transition happened on a different mask. Maybe this code should only
							//                       run when the state actually changes for a layer? Or... it's not actually a problem and it
							//                       can be left the way it is right now. Idk but for now I'm leaving it like this.
							//                         -Timo 2022/11/20
							switch (transition.type)
							{
							case StateMachine::TransitionType::CURRENT_STATE:
							{
								if (animStateMachineCopy.masks[transition.checkingMaskIndex].asmStateIndex == transition.checkingStateIndex)
								{
									// Apply transition
									mask.asmStateIndex = transition.toStateIndex;
									stateChanged = true;
									keepLooking = true;
									mp.animEndedThisFrame = false;

									end = true;
								}
								break;
							}

							case StateMachine::TransitionType::NOT_CURRENT_STATE:
							{
								if (animStateMachineCopy.masks[transition.checkingMaskIndex].asmStateIndex != transition.checkingStateIndex)
								{
									// Apply transition @COPYPASTA
									mask.asmStateIndex = transition.toStateIndex;
									stateChanged = true;
									keepLooking = true;
									mp.animEndedThisFrame = false;

									end = true;
								}
								break;
							}

							case StateMachine::TransitionType::TRIGGER_ACTIVATED:
								break;  // Skip this (triggers are handled separately)
							}

							if (end)
								break;
						}

						if (end)
							continue;

						// Triggers
						for (size_t i = 0; i < animStateMachineCopy.triggers.size(); i++)
						{
							if (!animStateMachineCopy.triggers[i].activated)
								continue;

							for (auto& transition : currentState.transitions)
							{
								if (transition.triggerIndex != i)
									continue;

								// Apply transition via trigger
								mask.asmStateIndex = transition.toStateIndex;
								animStateMachineCopy.triggers[i].activated = false;  // Reset that one trigger used
								stateChanged = true;
								keepLooking = true;
								mp.animEndedThisFrame = false;  // New state entered; anim just started so reset this!

								end = true;
								break;
							}

							if (end)
								break;
						}
					}
				}
				while (keepLooking);

				// Apply new animation if changed
				if (stateChanged)
				{
					// @DEBUG: for testing when the animator state changes
					std::cout << "[ANIMATOR RUN EVENT]" << std::endl
						<< "INFO: ASM: State changed to " << mask.states[mask.asmStateIndex].stateName << std::endl;

					auto& state = mask.states[mask.asmStateIndex];
					playAnimation(i, state.animationIndex, state.loop);
				}
			}

			// Reset triggers
			for (auto& trigger : animStateMachineCopy.triggers)
				trigger.activated = false;
		}

		// Process animation
		updateAnimation();
	}

	void Animator::runEvent(const std::string& eventName)
	{
		for (auto& eventCallback : eventCallbacks)
		{
			if (eventCallback.eventName == eventName)
			{
				eventCallback.callback();
				return;
			}
		}

		std::cerr << "[ANIMATOR RUN EVENT]" << std::endl
			<< "WARNING: Event name \"" << eventName << "\" not found in list of event callbacks" << std::endl;
	}

	void Animator::setState(const std::string& stateName)
	{
		// @TODO: It seems like since there will be a butt-ton of animator states, there should be a hash map.
		//        Though, for now it's just a linear search. Change it when you feel like it.  -Timo 2023/07/31
		for (size_t i = 0; i < animStateMachineCopy.masks.size(); i++)
		{
			auto& mask = animStateMachineCopy.masks[i];
			for (auto& state : mask.states)
			{
				if (state.stateName == stateName)
				{
					playAnimation(i, state.animationIndex, state.loop);

					// Turn off all triggers
					// @NOTE: this is to prevent a trigger changing the state after the state was just changed with this function!  -Timo 2023/08/08
					for (auto& trigger : animStateMachineCopy.triggers)
						trigger.activated = false;

					return;
				}
			}
		}
	}

	void Animator::setTrigger(const std::string& triggerName)
	{
		if (animStateMachineCopy.triggerNameToIndex.find(triggerName) == animStateMachineCopy.triggerNameToIndex.end())
		{
			std::cerr << "[ANIMATOR SET TRIGGER]" << std::endl
				<< "WARNING: Trigger name \"" << triggerName << "\" not found in animator" << std::endl;
			return;
		}

		size_t triggerIndex = animStateMachineCopy.triggerNameToIndex[triggerName];
		animStateMachineCopy.triggers[triggerIndex].activated = true;
	}

	void Animator::setMask(const std::string& maskName, bool enabled)
	{
		// @NOTE: there will be only 2-3 masks, so I don't see the point of making a hash map,
		//        that's why it's just a simple linear search.  -Timo 2022/11/20
		for (auto& mask : animStateMachineCopy.masks)
		{
			if (mask.maskName == maskName)
			{
				mask.enabled = enabled;
				return;
			}
		}

		std::cerr << "[ANIMATOR SET MASK]" << std::endl
			<< "WARNING: mask name \"" << maskName << "\" not found. Nothing was changed." << std::endl;
	}

	void Animator::setTwitchAngle(float_t radians)
	{
		twitchAngle = radians;
	}

	void Animator::updateAnimation()
	{
		bool updated = false;
		for (size_t i = 0; i < animStateMachineCopy.masks.size(); i++)
		{
			auto& mask = animStateMachineCopy.masks[i];
			auto& mp   = animStateMachineCopy.maskPlayers[i];
			Animation& animation = model->animations[mp.animationIndex];

			if (!mask.enabled)
				continue;

			for (auto& channel : animation.channels)
			{
				if (!mask.boneRefList.empty())
				{
					// Check to make sure the channel node is applicable to the mask
					// @TODO: @IMPROVE: preconstruct a list that has each bone and an index of which mask it should be targeting so this can be checked quicker (rebuild the list when enabling/disabling masks)
					bool found = false;
					for (auto& boneRef : mask.boneRefList)
						if (boneRef == channel.node)
						{
							found = true;
							break;
						}
					if (!found)
						continue;
				}

				vkglTF::AnimationSampler& sampler = animation.samplers[channel.samplerIndex];
				if (sampler.inputs.size() > sampler.outputsVec4.size())
				{
					// @CHECK: What is this ignoring/continuing?
					continue;
				}

				for (size_t i = 0, inputsSizeSub1 = sampler.inputs.size() - 1; i < inputsSizeSub1; i++)
				{
					if (mp.time >= sampler.inputs[i] && mp.time <= sampler.inputs[i + 1])
					{
						float_t u = std::max(0.0f, mp.time - sampler.inputs[i]) / (sampler.inputs[i + 1] - sampler.inputs[i]);
						switch (channel.path)
						{
							case vkglTF::AnimationChannel::PathType::TRANSLATION:
							{
								vec4 translation;
								glm_vec4_lerp(sampler.outputsVec4[i].raw, sampler.outputsVec4[i + 1].raw, u, translation);
								glm_vec4_copy3(translation, channel.node->translation);
								break;
							}
							case vkglTF::AnimationChannel::PathType::SCALE:
							{
								vec4 scale;
								glm_vec4_lerp(sampler.outputsVec4[i].raw, sampler.outputsVec4[i + 1].raw, u, scale);
								glm_vec4_copy3(scale, channel.node->scale);
								break;
							}
							case vkglTF::AnimationChannel::PathType::ROTATION:
							{
								versor r0, r1;
								glm_quat_copy(sampler.outputsVec4[i].raw, r0);
								glm_quat_copy(sampler.outputsVec4[i + 1].raw, r1);
								r0[3] += twitchAngle;
								r1[3] += twitchAngle;
								glm_quat_nlerp(r0, r1, u, channel.node->rotation);
								break;
							}
						}
						updated = true;
					}
				}
			}
		}
		if (updated)
		{
			// Update the joint matrices.
			for (size_t i = 0; i < model->skins.size(); i++)
			{
				auto& skin = model->skins[i];
				mat4 m = GLM_MAT4_IDENTITY_INIT;
				if (skin->skeletonRoot)
					skin->skeletonRoot->getMatrix(m);
				updateJointMatrices(skinIndexToGlobalReservedNodeIndex(i), skin, m);
			}
		}
	}

	void Animator::updateJointMatrices(size_t globalNodeReservedIndex, vkglTF::Skin* skin, mat4& m)
	{
		auto& uniformBlock = uniformBlocks[globalNodeReservedIndex];
		glm_mat4_copy(m, uniformBlock.matrix);

		// Update join matrices
		mat4 inverseTransform;
		glm_mat4_inv(m, inverseTransform);
		size_t numJoints = std::min((uint32_t)skin->joints.size(), MAX_NUM_JOINTS);

		// @NOTE: did some performance testing, and here are the results (debug build):
		//        Singlethreaded 100x: avg. 0.340738ms
		//        Multithreaded 100x:  avg. 0.618805ms
		//
		//        So, obviously, do the singlethreaded workload (done with taskflow. Note that the recorded time is just executor.run().wait() section only for multithreaded).
		// @NOTE: the cpu on my system is i9-7980xe (18 cores, 36 threads).
#define MULTITHREADED_JOINT_MATRICES 0
#if MULTITHREADED_JOINT_MATRICES
		static tf::Executor executor;
		tf::Taskflow taskflow;
#endif

#define PERF_TEST 0
#if PERF_TEST
		static double_t totalTime = 0.0;
		static size_t times = 0;
		std::chrono::steady_clock::time_point timerS = std::chrono::high_resolution_clock::now();
#endif

#if MULTITHREADED_JOINT_MATRICES
		taskflow.for_each_index(0, (int32_t)numJoints, 1, [&](int32_t i) {
#else
		for (size_t i = 0; i < numJoints; i++)
		{
#endif
			vkglTF::Node* jointNode = skin->joints[i];
			mat4 jointMat;
			jointNode->getMatrix(jointMat);
			glm_mat4_mul(jointMat, skin->inverseBindMatrices[i].raw, jointMat);
			glm_mat4_mul(inverseTransform, jointMat, uniformBlock.jointMatrix[i]);
#if MULTITHREADED_JOINT_MATRICES
		});

		timerS = std::chrono::high_resolution_clock::now();

		executor.run(taskflow).wait();
#else
		}
#endif

#if PERF_TEST
		totalTime += std::chrono::duration<double_t, std::milli>(std::chrono::high_resolution_clock::now() - timerS).count();
		times++;
		if (times >= 100)
		{
			std::cout << "Avg time over runs (ms): " << (totalTime / (double_t)times) << std::endl;
		}
#endif

		uniformBlock.jointcount = (float)numJoints;
		memcpy(nodeCollectionBuffer.mapped + globalNodeReservedIndex, &uniformBlock, sizeof(GPUAnimatorNode));
	}

	bool Animator::getJointMatrix(const std::string& jointName, mat4& out)
	{
		// Find the joint with this name
		// @IMPROVE: don't have this, instead have a jointindex param you put into this function instead of having to do a long string search
		for (auto& skins : model->skins)
		{
			for (size_t i = 0; i < skins->joints.size(); i++)
			{
				auto& joint = skins->joints[i];
				if (joint->name == jointName)
				{
					joint->getMatrix(out);
					return true;
				}
			}
		}

		std::cerr << "[GET JOINT MATRIX]" << std::endl
			<< "WARNING: joint matrix \"" << jointName << "\" not found. Returning identity matrix" << std::endl;
		return false;
	}

	size_t Animator::skinIndexToGlobalReservedNodeIndex(size_t skinIndex)
	{
		return myReservedNodeCollectionIndices[skinIndex];
	}
}