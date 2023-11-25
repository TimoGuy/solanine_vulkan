#version 460

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inUV0;
layout (location = 3) in vec2 inUV1;
layout (location = 4) in vec4 inJoint0;
layout (location = 5) in vec4 inWeight0;
layout (location = 6) in vec4 inColor0;

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


// Skeletal Animation/Skinning
#define MAX_NUM_JOINTS 128
struct SkeletonAnimationNode
{
	mat4 matrix;
	mat4 jointMatrix[MAX_NUM_JOINTS];
	float jointCount;
};

layout (std140, set = 4, binding = 0) readonly buffer SkeletonAnimationNodeCollection
{
	SkeletonAnimationNode nodes[];
} nodeCollection;


// Voxel field lighting grid Transforms
struct VoxelFieldLightingGrid
{
	mat4 transform;
};

layout(std140, set = 5, binding = 0) readonly buffer VoxelFieldLightingGridBuffer
{
	VoxelFieldLightingGrid gridTransforms[];
} lightingGridBuffer;


void main()
{
	//
	// Skin mesh
	//
	mat4 modelMatrix = objectBuffer.objects[instancePtrBuffer.pointers[gl_BaseInstance].objectID].modelMatrix;
	uint animatorNodeID = instancePtrBuffer.pointers[gl_BaseInstance].animatorNodeID;
	mat4 nodeMatrix = nodeCollection.nodes[animatorNodeID].matrix;
	vec4 locPos;
	if (nodeCollection.nodes[animatorNodeID].jointCount > 0.0)
	{
		// Is skinned
		mat4 skinMat =
			inWeight0.x * nodeCollection.nodes[animatorNodeID].jointMatrix[int(inJoint0.x)] +
			inWeight0.y * nodeCollection.nodes[animatorNodeID].jointMatrix[int(inJoint0.y)] +
			inWeight0.z * nodeCollection.nodes[animatorNodeID].jointMatrix[int(inJoint0.z)] +
			inWeight0.w * nodeCollection.nodes[animatorNodeID].jointMatrix[int(inJoint0.w)];

		locPos = modelMatrix * nodeMatrix * skinMat * vec4(inPos, 1.0);
		outNormal = normalize(transpose(inverse(mat3(modelMatrix * nodeMatrix * skinMat))) * inNormal);
	}
	else
	{
		// Not skinned
		locPos = modelMatrix * nodeMatrix * vec4(inPos, 1.0);
		outNormal = normalize(transpose(inverse(mat3(modelMatrix * nodeMatrix))) * inNormal);
	}

	outWorldPos = locPos.xyz / locPos.w;
	outUV0 = inUV0;
	outUV1 = inUV1;
	outColor0 = inColor0;
	outViewPos = (cameraData.view * vec4(outWorldPos, 1.0)).xyz;
	voxelFieldLightingGridPos = (lightingGridBuffer.gridTransforms[instancePtrBuffer.pointers[gl_BaseInstance].voxelFieldLightingGridID].transform * vec4(outWorldPos, 1.0)).xyz;
	baseInstanceID = gl_BaseInstance;
	gl_Position =  cameraData.projectionView * vec4(outWorldPos, 1.0);
}
