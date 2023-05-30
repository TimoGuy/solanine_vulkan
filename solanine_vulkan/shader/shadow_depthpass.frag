#version 460

layout (location = 0) in vec2 inUV0;
layout (location = 1) flat in uint baseInstanceID;

// Instance ID Pointers
// @COPYPASTA
struct InstancePointer
{
	uint objectID;
	uint materialID;
	uint animatorNodeID;
	uint pad;
};

layout(std140, set = 2, binding = 0) readonly buffer InstancePtrBuffer
{
	InstancePointer pointers[];
} instancePtrBuffer;


//
// Material bindings
// @COPYPASTA
//
#define MAX_NUM_MAPS 21
layout (set = 3, binding = 0) uniform sampler2D textureMaps[MAX_NUM_MAPS];

struct MaterialParam
{
	// Texture map references
	uint colorMapIndex;
	uint physicalDescriptorMapIndex;
	uint normalMapIndex;
	uint aoMapIndex;
	uint emissiveMapIndex;

	// Material properties
	vec4  baseColorFactor;
	vec4  emissiveFactor;
	vec4  diffuseFactor;
	vec4  specularFactor;
	float workflow;
	int   baseColorTextureSet;
	int   physicalDescriptorTextureSet;
	int   normalTextureSet;
	int   occlusionTextureSet;
	int   emissiveTextureSet;
	float metallicFactor;
	float roughnessFactor;
	float alphaMask;
	float alphaMaskCutoff;
};

#define MAX_NUM_MATERIALS 256
layout (std140, set = 3, binding = 1) readonly buffer MaterialCollection
{
	MaterialParam params[MAX_NUM_MATERIALS];
} materialCollection;


void main() 
{	
	float alpha = texture(textureMaps[materialCollection.params[instancePtrBuffer.pointers[baseInstanceID].materialID].colorMapIndex], inUV0).a;
	if (alpha < 0.5)
		discard;
}
