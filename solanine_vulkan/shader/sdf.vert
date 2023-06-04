#version 460

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec2 inUV;

layout (location = 0) out vec2 outUV;


// Camera Props
layout(set = 0, binding = 0) uniform CameraBuffer
{
	mat4 view;
	mat4 projection;
	mat4 projectionView;
	vec3 cameraPosition;
} cameraData;

// Push constant for model matrix
layout (push_constant) uniform PushConsts
{
	mat4 modelMatrix;
	float renderInScreenspace;
} pushConsts;


void main() 
{
	outUV = inUV;
	if (pushConsts.renderInScreenspace > 0.0)
	{
		gl_Position = pushConsts.modelMatrix * vec4(inPos.xyz, 1.0);
	}
	else
	{
		gl_Position = cameraData.projectionView * pushConsts.modelMatrix * vec4(inPos.xyz, 1.0);
	}
}
