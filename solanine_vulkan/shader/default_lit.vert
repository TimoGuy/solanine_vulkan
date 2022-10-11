#version 460

layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec2 vUV0;
layout (location = 3) in vec2 vUV1;
layout (location = 4) in vec4 vJoint0;
layout (location = 5) in vec4 vWeight0;
layout (location = 6) in vec4 vColor0;

layout (location = 0) out vec4 outColor;
layout (location = 1) out vec2 outTexCoord;

layout(set = 0, binding = 0) uniform CameraBuffer
{
	mat4 view;
	mat4 projection;
	mat4 projectionView;
	vec3 cameraPosition;
} cameraData;

#define MAX_NUM_JOINTS 128

struct ObjectData
{
	mat4 modelMatrix;
};

// All Object Matrices
layout(std140, set = 1, binding = 0) readonly buffer ObjectBuffer
{
	ObjectData objects[];
} objectBuffer;

// Push Constants
layout (push_constant) uniform constants
{
	vec4 data;
	mat4 renderMatrix;
} PushConstants;


void main()
{
	mat4 modelMatrix = objectBuffer.objects[gl_BaseInstance].modelMatrix;
	mat4 transformMatrix = cameraData.projectionView * modelMatrix;
	gl_Position = transformMatrix * vec4(vPosition, 1.0);
	outColor = vColor0;
	outTexCoord = vUV0;
}
