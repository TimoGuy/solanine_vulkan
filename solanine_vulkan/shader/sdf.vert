#version 460

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec2 inUV;

layout (location = 0) out vec2 outUV;


// UI Camera Props
layout(set = 0, binding = 0) uniform UICameraBuffer
{
	mat4 projectionView;
	mat4 screenspaceOrthoView;
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
		gl_Position = cameraData.screenspaceOrthoView * pushConsts.modelMatrix * vec4(inPos.xyz, 1.0);
	}
	else
	{
		gl_Position = cameraData.projectionView * pushConsts.modelMatrix * vec4(inPos.xyz, 1.0);
	}
}
