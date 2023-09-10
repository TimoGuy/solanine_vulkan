#version 460

layout (location = 0) in vec2 inUV;

layout (location = 0) out float outColor;

layout (push_constant) uniform BlurParams
{
	vec2 oneOverImageExtent;
} params;

layout (set = 0, binding = 0) uniform sampler2D image;

const vec2 gaussFilter[7] = vec2[](
	vec2(-3.0,   2.0/401.0),
	vec2(-2.0,  22.0/401.0),
	vec2(-1.0,  97.0/401.0),
	vec2( 0.0, 159.0/401.0),
	vec2( 1.0,  97.0/401.0),
	vec2( 2.0,  22.0/401.0),
	vec2( 3.0,   2.0/401.0)
);


void main()
{
	float color = 0.0;

	for (int i = 0; i < 7; i++)
	{
		vec2 coord = vec2(inUV.x, inUV.y + gaussFilter[i].r * params.oneOverImageExtent.y);
		color += texture(image, coord).r * gaussFilter[i].g;
	}

	outColor = color;
}
