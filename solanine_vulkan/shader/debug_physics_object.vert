#version 460

layout (location = 0) in vec3 inPos;
layout (location = 1) in int  inObjIndex;


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


void main()
{
	mat4 modelMatrix = objectBuffer.objects[inObjIndex].modelMatrix;
	gl_Position = cameraData.projectionView * modelMatrix * vec4(inPos, 1.0);
}
