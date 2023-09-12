#version 460

layout (location = 0) in vec2 inUV;

layout (location = 0) out float outColor;

layout (push_constant) uniform BlurParams
{
	vec2 oneOverImageExtent;
} params;

layout (set = 0, binding = 0) uniform sampler2D image;

// const vec2 gaussFilter[11] = vec2[](
// 	vec2(-5.0,  3.0/133.0),
// 	vec2(-4.0,  6.0/133.0),
// 	vec2(-3.0, 10.0/133.0),
// 	vec2(-2.0, 15.0/133.0),
// 	vec2(-1.0, 20.0/133.0),
// 	vec2( 0.0, 25.0/133.0),
// 	vec2( 1.0, 20.0/133.0),
// 	vec2( 2.0, 15.0/133.0),
// 	vec2( 3.0, 10.0/133.0),
// 	vec2( 4.0,  6.0/133.0),
// 	vec2( 5.0,  3.0/133.0)
// );

const vec2 gaussFilter[5] = vec2[](
	vec2(-2.0,  7.0/107.0),
	vec2(-1.0, 26.0/107.0),
	vec2( 0.0, 41.0/107.0),
	vec2( 1.0, 26.0/107.0),
	vec2( 2.0,  7.0/107.0)
);


void main()
{
	float color = 0.0;

	for (int i = 0; i < 5; i++)
	{
		vec2 coord = vec2(inUV.x, inUV.y + gaussFilter[i].r * params.oneOverImageExtent.y);
		float colorSample = texture(image, coord).r * gaussFilter[i].g;
		color += colorSample;
	}

	outColor = color;
}
