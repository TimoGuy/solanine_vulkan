#version 460

layout (location = 0) in vec2 inUV;

layout (location = 0) out float outNearFieldIncrementalReductionHalveCoC;

layout (set = 0, binding = 0) uniform sampler2D CoCImage;


void main()
{
	// For LOD, here is my understanding:
	//     0.0: full res
	//     1.0: half res
	//     2.0: quarter res
	//     3.0: eighth res
	//     4.0: incrementalReductionHalve res
	//
	// NOTE: this is using the mipmap MAX reduction mode sampler, which is why this works.
	//     -Timo 2023/09/10
	// outNearFieldIncrementalReductionHalveCoC = textureLod(CoCImage, inUV * params.oneOverImageExtent, 4.0).r;
	// return;

	// @REPLY: okay, so the MAX reduction mode just takes the mip-1's pixels and max() them... is what I'm observing.
	//         It's not possible (as far as my gpu: RTX 2080 Ti) to go down multiple mips and max(). So what I wrote
	//         is doing that but via software/manually.  -Timo 2023/09/12
	// float newCoC = 0.0;
	// float eighth = 1.0 / 8.0;
	// for (int i = 0; i < 8; i++)
	// 	for (int j = 0; j < 8; j++)
	// 		newCoC = max(newCoC, textureLod(CoCImage, inUV + ((vec2(i, j)) * params.oneOverImageExtent * eighth), 1.0).x);
    // outNearFieldIncrementalReductionHalveCoC = newCoC;
	// return;

	// @REPLY: and here is the final product. Going down by hardware using 4 render passes.
	//
	//         The metrics (captured on Nsight Graphics on RTX 2080 Ti):
	//         		OLD: 58.97 μs	L2: 21.6%	L1: 17.3%	CROP: 0.1%		VRAM: 5.4%
	//         		NEW: 34.75 μs	L2: 7.1%	L1: 6.8%	CROP: 16.3%		VRAM: 8.7%
	//
	//         It feels like it was a trade-off that really didn't make as much of a difference as I had thought.
	//         For getting the already small procedure (0.058ms) down to half the time is great, but maybe not worth
	//         the substantial CROP increase. Since L1 and L2 decreased so much, that was probably relieving the texture
	//         lookup registers, but then VRAM throughput went up a little. Either way, this really doesn't mean much
	//         though bc of how inconsequential this optimization was lol.
	//         I guess you could say the same about this long paragraph and the time to look into the GPU metrics for
	//         this 0.03ms process. Lol  -Timo 2023/09/13
    outNearFieldIncrementalReductionHalveCoC = textureLod(CoCImage, inUV, 1.0).r;  // @NOTE: my initial thought was to offset the sample position by 0.5,0.5 but this ended up bleeding the max filter to the upper-left.
}
