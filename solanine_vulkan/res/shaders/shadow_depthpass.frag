#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout (location = 0) in vec2 inUV0;
layout (location = 1) flat in uint baseInstanceID;

// Instance ID Pointers
// @COPYPASTA
struct InstancePointer
{
	uint objectID;
	uint materialID;
	uint animatorNodeID;
	uint voxelFieldLightingGridID;
};

layout(std140, set = 2, binding = 0) readonly buffer InstancePtrBuffer
{
	InstancePointer pointers[];
} instancePtrBuffer;


//
// Material bindings
// @COPYPASTA
//
struct MaterialParam
{
	// Texture map references
	uint colorMapIndex;
	// uint physicalDescriptorMapIndex;
	// uint normalMapIndex;
	// uint aoMapIndex;
	// uint emissiveMapIndex;

	// Material properties
	vec4  baseColorFactor;  // I think this should be used.
	// vec4  emissiveFactor;
	// vec4  diffuseFactor;
	// vec4  specularFactor;
	// float workflow;
	int   baseColorTextureSet;  // I think this should be used.
	// int   physicalDescriptorTextureSet;
	// int   normalTextureSet;
	// int   occlusionTextureSet;
	// int   emissiveTextureSet;
	// float metallicFactor;
	// float roughnessFactor;
	float alphaMask;
	float alphaMaskCutoff;  // I think this should be used.
};

#define MAX_NUM_MATERIALS 256
layout (std140, set = 3, binding = 0) readonly buffer MaterialCollection
{
	uint materialIDOffset;
	MaterialParam params[MAX_NUM_MATERIALS];
} materialCollection;

layout (set = 3, binding = 1) uniform sampler2D textureMaps[];


void main()
{
	uint materialID = instancePtrBuffer.pointers[baseInstanceID].materialID - materialCollection.materialIDOffset;
	if (materialCollection.params[materialID].alphaMask == 1.0)
	{
		float alpha = texture(textureMaps[nonuniformEXT(materialCollection.params[materialID].colorMapIndex)], inUV0).a;
		if (alpha < 0.5)
			discard;
	}
}
