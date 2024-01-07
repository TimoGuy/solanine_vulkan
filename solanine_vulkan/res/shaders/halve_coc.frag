#version 460

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outNearFieldColor;
layout (location = 1) out vec4 outFarFieldColor;


layout (set = 0, binding = 0) uniform sampler2D mainImage;
layout (set = 0, binding = 1) uniform sampler2D CoCImage;


void main()
{
	vec2 CoC = texture(CoCImage, inUV).rg;  // @NOTE: not using MAX filter. Using NEAREST filter.

	vec3 color = texture(mainImage, inUV).rgb;
	outNearFieldColor = vec4((CoC.r > 0.0) ? color : vec3(0.0), CoC.r);
	outFarFieldColor = vec4((CoC.g > 0.0) ? color : vec3(0.0), CoC.g);
}
