#version 460

layout (location = 0) in vec3 inPos;
layout (location = 1) in int  inObjIndex;
layout (location = 2) in vec3 inColor;

layout (location = 0) out vec3 outColor;


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


void main()
{
	mat4 modelMatrix = inObjIndex < 0 ? mat4(1.0) : objectBuffer.objects[inObjIndex].modelMatrix;
	gl_Position = cameraData.projectionView * modelMatrix * vec4(inPos, 1.0);
	outColor = inColor;
}
