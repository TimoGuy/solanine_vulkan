#version 460

layout (location = 0) in vec3 inWorldPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inUV0;
layout (location = 3) in vec2 inUV1;
layout (location = 4) in vec4 inColor0;

layout (location = 0) out vec4 outFragColor;

// Scene Bindings

layout(set = 0, binding = 0) uniform CameraBuffer
{
	mat4 view;
	mat4 projection;
	mat4 projectionView;
	vec3 cameraPosition;
} cameraData;

layout (set = 0, binding = 1) uniform UBOParams
{
	vec4 lightDir;
	float exposure;
	float gamma;
	float prefilteredCubemapMipLevels;
	float scaleIBLAmbient;
	float debugViewInputs;
	float debugViewEquation;
} uboParams;

layout (set = 0, binding = 2) uniform samplerCube samplerIrradiance;
layout (set = 0, binding = 3) uniform samplerCube prefilteredMap;
layout (set = 0, binding = 4) uniform sampler2D samplerBRDFLUT;

layout (set = 2, binding = 0) uniform sampler2D tex1;


void main()
{
	//outFragColor = vec4(inColor + sceneData.ambientColor.xyz, 1.0);
	//outFragColor = vec4(inTexCoord.x, inTexCoord.y, 0.5, 1.0);


	// Am I in love?
	float l = texture(samplerIrradiance, vec3(0, 1, 0)).x;
	float o = texture(prefilteredMap, vec3(0, 1, 0)).x;
	float v = texture(samplerBRDFLUT, vec2(0.5, 0.5)).x;
	float e = l+o+v;
	// Call me Dmitri


	vec3 color = texture(tex1, inUV0).xyz;
	outFragColor = vec4(color, 1.0);
}
