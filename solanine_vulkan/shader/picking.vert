#version 460

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inUV0;
layout (location = 3) in vec2 inUV1;
layout (location = 4) in vec4 inJoint0;
layout (location = 5) in vec4 inWeight0;
layout (location = 6) in vec4 inColor0;

layout (location = 0) out uint outID;


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


void main()
{
	//
	// Skin mesh @COPYPASTA
	//
	mat4 modelMatrix = objectBuffer.objects[instancePtrBuffer.pointers[gl_BaseInstance].objectID].modelMatrix;
	SkeletonAnimationNode node = nodeCollection.nodes[instancePtrBuffer.pointers[gl_BaseInstance].animatorNodeID];
	vec4 locPos;
	if (node.jointCount > 0.0)
	{
		// Is skinned
		mat4 skinMat =
			inWeight0.x * node.jointMatrix[int(inJoint0.x)] +
			inWeight0.y * node.jointMatrix[int(inJoint0.y)] +
			inWeight0.z * node.jointMatrix[int(inJoint0.z)] +
			inWeight0.w * node.jointMatrix[int(inJoint0.w)];

		locPos = modelMatrix * node.matrix * skinMat * vec4(inPos, 1.0);
	}
	else
	{
		// Not skinned
		locPos = modelMatrix * node.matrix * vec4(inPos, 1.0);
	}

	outID = uint(instancePtrBuffer.pointers[gl_BaseInstance].objectID);
	gl_Position =  cameraData.projectionView * vec4(locPos.xyz / locPos.w, 1.0);
}
