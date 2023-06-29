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
layout (location = 6) out uint baseInstanceID;


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

layout (std140, set = 3, binding = 0) readonly buffer SkeletonAnimationNodeCollection
{
	SkeletonAnimationNode nodes[];
} nodeCollection;


void main()
{
	//
	// Skin mesh
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
		outNormal = normalize(transpose(inverse(mat3(modelMatrix * node.matrix * skinMat))) * inNormal);
	}
	else
	{
		// Not skinned
		locPos = modelMatrix * node.matrix * vec4(inPos, 1.0);
		outNormal = normalize(transpose(inverse(mat3(modelMatrix * node.matrix))) * inNormal);
	}

	outWorldPos = locPos.xyz / locPos.w;
	outUV0 = inUV0;
	outUV1 = inUV1;
	outColor0 = inColor0;
	outViewPos = (cameraData.view * vec4(outWorldPos, 1.0)).xyz;
	baseInstanceID = gl_BaseInstance;
	gl_Position =  cameraData.projectionView * vec4(outWorldPos, 1.0);
}
