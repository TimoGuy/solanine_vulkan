#version 460

layout (location = 0) in vec2 inUV;

layout (location = 0) out float outDownsizedNearFieldCoC;

layout (set = 0, binding = 0) uniform sampler2D nearFieldCoC;


void main()
{
	// For LOD, here is my understanding:
	//     0.0: full res
	//     1.0: half res
	//     2.0: quarter res
	//
	// Since we're wanting the 1/8th of the size, we want 2.0 bc the
	// original texture size is half res already.
	// NOTE: this is using the mipmap MAX reduction mode sampler, which is why this works.
	//     -Timo 2023/09/10
	outDownsizedNearFieldCoC = textureLod(nearFieldCoC, inUV, 2.0);
}
