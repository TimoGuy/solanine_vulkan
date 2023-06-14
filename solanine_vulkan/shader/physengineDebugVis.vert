#version 460

layout (location = 0) in vec3 inPos;
layout (location = 1) in int inPointSpace;

layout (location = 0) out vec3 outColor;


layout(set = 0, binding = 0) uniform VisCameraBuffer
{
	mat4 projectionView;
} cameraData;

layout(push_constant) uniform InstancePushConst
{
	vec4  color;
	vec4  pt1;
	vec4  pt2;
	float capsuleRadius;
} instanceData;


void main()
{
	outColor = instanceData.color.rgb;
	vec3 position = inPos.xyz * instanceData.capsuleRadius + (inPointSpace == 0 ? instanceData.pt1.xyz : instanceData.pt2.xyz);
	gl_Position = cameraData.projectionView * vec4(position, 1.0);
}
