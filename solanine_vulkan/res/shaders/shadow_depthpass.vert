#version 460

layout (location = 0) in vec3 inPos;
// layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inUV0;
// layout (location = 3) in vec2 inUV1;
// layout (location = 4) in vec4 inColor0;
layout (location = 5) in uint inInstanceIDOffset;

layout (location = 0) out vec2 outUV0;
layout (location = 1) out uint baseInstanceID;


// Cascade View Matrices
#define SHADOW_MAP_CASCADE_COUNT 4  // @TODO: pass via specialization constant
layout (set = 0, binding = 0) uniform UBO
{
	mat4[SHADOW_MAP_CASCADE_COUNT] cascadeViewProjMat;
} ubo;


// All Object Matrices
struct ObjectData
{
	mat4 modelMatrix;
	vec4 boundingSphere;
};

layout(std140, set = 1, binding = 0) readonly buffer ObjectBuffer
{
	ObjectData objects[];
} objectBuffer;


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


// Push constant to show cascade index
layout (push_constant) uniform PushConsts
{
	uint cascadeIndex;
} pushConsts;



void main()
{
	uint instID = gl_BaseInstance + inInstanceIDOffset;
	mat4 modelMatrix = objectBuffer.objects[instancePtrBuffer.pointers[instID].objectID].modelMatrix;
	vec4 locPos = modelMatrix * vec4(inPos, 1.0);

	outUV0 = inUV0;
	baseInstanceID = instID;
	gl_Position = ubo.cascadeViewProjMat[pushConsts.cascadeIndex] * vec4(locPos.xyz / locPos.w, 1.0);
}
