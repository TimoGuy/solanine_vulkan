#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout (location = 0) in vec2 inUV0;
layout (location = 1) in vec2 inUV1;
layout (location = 2) flat in uint baseInstanceID;

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

	// Material properties
	vec4  baseColorFactor;
	int   baseColorTextureSet;
	float alphaMask;
	float alphaMaskCutoff;
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
		float alpha = materialCollection.params[materialID].baseColorFactor.a;
		if (materialCollection.params[materialID].baseColorTextureSet > -1)
		{
			alpha *= texture(textureMaps[nonuniformEXT(materialCollection.params[materialID].colorMapIndex)], materialCollection.params[materialID].baseColorTextureSet == 0 ? inUV0 : inUV1).a;
		}
		if (alpha < materialCollection.params[materialID].alphaMaskCutoff)
		{
			discard;
		}
	}
}
