#version 460

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inUV0;
layout (location = 3) in vec2 inUV1;
layout (location = 4) in vec4 inColor0;
layout (location = 5) in uint inInstanceIDOffset;

layout (location = 0) out vec2 outUV0;
layout (location = 1) out vec2 outUV1;
layout (location = 2) out uint baseInstanceID;


// Camera Props
layout(set = 0, binding = 0) uniform CameraBuffer
{
	mat4 view;
	mat4 projection;
	mat4 projectionView;
	vec3 cameraPosition;
} cameraData;


// All Object Matrices
struct ObjectData
{
	mat4 modelMatrix;
};

layout(std140, set = 1, binding = 0) readonly buffer ObjectBuffer
{
	ObjectData objects[];
} objectBuffer;


// Instance ID Pointers
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


void main()
{
	uint instID = gl_BaseInstance + inInstanceIDOffset;
	mat4 modelMatrix = objectBuffer.objects[instancePtrBuffer.pointers[instID].objectID].modelMatrix;
	vec4 locPos = modelMatrix * vec4(inPos, 1.0);

	outUV0 = inUV0;
	outUV1 = inUV1;
	baseInstanceID = instID;
	vec3 worldPos = locPos.xyz / locPos.w;
	gl_Position = cameraData.projectionView * vec4(worldPos, 1.0);
}

