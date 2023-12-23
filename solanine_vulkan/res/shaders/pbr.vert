#version 460

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inUV0;
layout (location = 3) in vec2 inUV1;
layout (location = 4) in vec4 inColor0;
layout (location = 5) in uint inInstanceIDOffset;

layout (location = 0) out vec3 outWorldPos;
layout (location = 1) out vec3 outViewPos;
layout (location = 2) out vec3 outNormal;
layout (location = 3) out vec2 outUV0;
layout (location = 4) out vec2 outUV1;
layout (location = 5) out vec4 outColor0;
layout (location = 6) out vec3 voxelFieldLightingGridPos;
layout (location = 7) out uint baseInstanceID;


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
	vec4 boundingSphere;
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
	uint animatorNodeID;  // @TODO: take out! This will be used by the compute skinning process.
	uint voxelFieldLightingGridID;
};

layout(std140, set = 2, binding = 0) readonly buffer InstancePtrBuffer
{
	InstancePointer pointers[];
} instancePtrBuffer;


// Voxel field lighting grid Transforms
struct VoxelFieldLightingGrid
{
	mat4 transform;
};

layout(std140, set = 4, binding = 0) readonly buffer VoxelFieldLightingGridBuffer
{
	VoxelFieldLightingGrid gridTransforms[];
} lightingGridBuffer;


void main()
{
	uint instID = gl_BaseInstance + inInstanceIDOffset;
	mat4 modelMatrix = objectBuffer.objects[instancePtrBuffer.pointers[instID].objectID].modelMatrix;
	vec4 locPos = modelMatrix * vec4(inPos, 1.0);
	outNormal = normalize(transpose(inverse(mat3(modelMatrix))) * inNormal);

	outWorldPos = locPos.xyz / locPos.w;
	outUV0 = inUV0;
	outUV1 = inUV1;
	outColor0 = inColor0;
	outViewPos = (cameraData.view * vec4(outWorldPos, 1.0)).xyz;
	voxelFieldLightingGridPos = (lightingGridBuffer.gridTransforms[instancePtrBuffer.pointers[instID].voxelFieldLightingGridID].transform * vec4(outWorldPos, 1.0)).xyz;
	baseInstanceID = instID;
	gl_Position = cameraData.projectionView * vec4(outWorldPos, 1.0);
}
