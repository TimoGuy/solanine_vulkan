#version 460

layout (location = 0) in vec2 inUV;

layout (location = 0) out float outNearFieldEighthCoC;

layout (push_constant) uniform Params
{
	vec2 oneOverImageExtent;
} params;

layout (set = 0, binding = 0) uniform sampler2D CoCImage;


void main()
{
	// For LOD, here is my understanding:
	//     0.0: full res
	//     1.0: half res
	//     2.0: quarter res
	//     3.0: eighth res
	//
	// NOTE: this is using the mipmap MAX reduction mode sampler, which is why this works.
	//     -Timo 2023/09/10
	outNearFieldEighthCoC = textureLod(CoCImage, inUV + vec2(0.5) * params.oneOverImageExtent, 3.0).r;
}
