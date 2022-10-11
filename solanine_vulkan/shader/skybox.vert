#version 460

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inUV0;

layout (location = 0) out vec3 outUVW;

layout(set = 0, binding = 0) uniform CameraBuffer
{
	mat4 view;
	mat4 projection;
	mat4 projectionView;
	vec3 cameraPosition;
} cameraData;


out gl_PerVertex 
{
	vec4 gl_Position;
};

void main()
{
	outUVW = inPos;
	gl_Position = cameraData.projection * mat4(mat3(cameraData.view)) * vec4(inPos, 1.0);
}
