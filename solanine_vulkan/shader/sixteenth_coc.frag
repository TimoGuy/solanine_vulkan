#version 460

layout (location = 0) in vec2 inUV;

layout (location = 0) out float outNearFieldSixteenthCoC;

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
	//     4.0: sixteenth res
	//
	// NOTE: this is using the mipmap MAX reduction mode sampler, which is why this works.
	//     -Timo 2023/09/10
	// outNearFieldSixteenthCoC = textureLod(CoCImage, inUV * params.oneOverImageExtent, 4.0).r;
	// return;

	// @REPLY: okay, so the MAX reduction mode just takes the mip-1's pixels and max() them... is what I'm observing.
	//         It's not possible (as far as my gpu: RTX 2080 Ti) to go down multiple mips and max(). So what I wrote
	//         is doing that but via software/manually.  -Timo 2023/09/12
	float newCoC = 0.0;
	float eighth = 1.0 / 8.0;
	for (int i = 0; i < 8; i++)
		for (int j = 0; j < 8; j++)
			newCoC = max(newCoC, textureLod(CoCImage, inUV + ((vec2(i, j)) * params.oneOverImageExtent * eighth), 1.0).x);
    outNearFieldSixteenthCoC = newCoC;
}
