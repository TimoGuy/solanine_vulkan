#version 460

layout (location = 0) in vec3 inPos;
//layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inUV0;
//layout (location = 3) in vec2 inUV1;
layout (location = 4) in vec4 inJoint0;
layout (location = 5) in vec4 inWeight0;
//layout (location = 6) in vec4 inColor0;

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


// Push constant to show cascade index
layout (push_constant) uniform PushConsts
{
	uint cascadeIndex;
} pushConsts;



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
	}
	else
	{
		// Not skinned
		locPos = modelMatrix * nodeMatrix * vec4(inPos, 1.0);
	}

	outUV0 = inUV0;
	baseInstanceID = gl_BaseInstance;
	gl_Position =  ubo.cascadeViewProjMat[pushConsts.cascadeIndex] * vec4(locPos.xyz / locPos.w, 1.0);
}
