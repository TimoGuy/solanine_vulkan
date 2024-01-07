#version 460

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outNearFieldColorWithCoCAlpha;
layout (location = 1) out vec4 outFarFieldColorWithCoCAlpha;


layout (push_constant) uniform BlurParams
{
	vec2 oneOverImageExtent;
} params;

layout (set = 0, binding = 0) uniform sampler2D nearFieldImage;
layout (set = 0, binding = 1) uniform sampler2D farFieldImage;


void main()
{
	// @DEBUG: just pass the data along in the shader.
	// outNearFieldColorWithCoCAlpha = texture(nearFieldImage, inUV);
    // outFarFieldColorWithCoCAlpha = texture(farFieldImage, inUV);
	// return;

	vec4 maxNearField = vec4(0.0);
	float maxNearFieldLuma = 0.0;
	vec4 maxFarField = vec4(0.0);
	float maxFarFieldLuma = 0.0;

	int extent = 1;
	for (int i = -extent; i <= extent; i++)
	for (int j = -extent; j <= extent; j++)
	{
		vec2 sampleUV = inUV + (vec2(i, j) * params.oneOverImageExtent);
		vec4 nearFieldSample = texture(nearFieldImage, sampleUV);
		vec4 farFieldSample = texture(farFieldImage, sampleUV);

		// So I'm doing a flood fill again. But, this time, I'm calculating luma as what I'm maxing.
		// I tried the dot product of the samples.rgb*samples.a with luma coefficients `vec3(0.2126, 0.7152, 0.0722);`,
		// but this ended up with a noisy image. The best thing I found was just to max the nearFieldSample.a.
		// This gave a super clean image. So, unless you think you can do better!
		//     -Timo 2023/09/12
		float nearLuma = nearFieldSample.a;
		if (nearLuma > maxNearFieldLuma)
		{
			maxNearField = nearFieldSample;
			maxNearFieldLuma = nearLuma;
		}

		float farLuma = farFieldSample.a;
		if (farLuma > maxFarFieldLuma)
		{
			maxFarField = farFieldSample;
			maxFarFieldLuma = farLuma;
		}
	}

    outNearFieldColorWithCoCAlpha = maxNearField;
    outFarFieldColorWithCoCAlpha = maxFarField;
}


// #define KERNEL_SAMPLES 9
// const vec3 gaussianKernel[KERNEL_SAMPLES] = vec3[](
// 	vec3(-1.0,  1.0, 1.0/16.0),
// 	vec3( 1.0,  1.0, 1.0/16.0),
// 	vec3(-1.0, -1.0, 1.0/16.0),
// 	vec3( 1.0, -1.0, 1.0/16.0),
// 	vec3(-1.0,  0.0, 1.0/8.0),
// 	vec3( 1.0,  0.0, 1.0/8.0),
// 	vec3( 0.0, -1.0, 1.0/8.0),
// 	vec3( 0.0,  1.0, 1.0/8.0),
// 	vec3( 0.0,  0.0, 1.0/4.0)
// );


// void main()
// {
// 	vec4 nearFieldAccum = vec4(0.0);
// 	float nearFieldKernalAccum = 0.0;
// 	vec4 farFieldAccum = vec4(0.0);
// 	float farFieldKernalAccum = 0.0;
// 	for (int i = 0; i < KERNEL_SAMPLES; i++)
// 	{
// 		vec2 sampleUV = inUV + gaussianKernel[i].xy * params.oneOverImageExtent;
// 		float influence = gaussianKernel[i].z;
		
// 		vec4 nearFieldSample = texture(nearFieldImage, sampleUV) * influence;
// 		nearFieldAccum += nearFieldSample;
// 		if (nearFieldSample.a > 0.0)
// 			nearFieldKernalAccum += influence;

// 		vec4 farFieldSample = texture(farFieldImage, sampleUV) * influence;
// 		farFieldAccum += farFieldSample;
// 		if (farFieldSample.a > 0.0)
// 			farFieldKernalAccum += influence;
// 	}

// 	if (nearFieldKernalAccum > 0.000001)
// 		nearFieldAccum /= nearFieldKernalAccum;

// 	if (farFieldKernalAccum > 0.000001)
// 		farFieldAccum /= farFieldKernalAccum;

// 	outNearFieldColorWithCoCAlpha = nearFieldAccum;
// 	outFarFieldColorWithCoCAlpha = farFieldAccum;
// }


// void main()
// {
// 	vec4 maxNearField = vec4(0.0);
// 	float maxNearFieldIntensity = 0.0;
// 	vec4 maxFarField = vec4(0.0);
// 	float maxFarFieldIntensity = 0.0;

// 	const int extent = 1;
// 	for (int i = -extent; i <= extent; i++)
// 		for (int j = -extent; j <= extent; j++)
// 		{
// 			vec2 sampleUV = inUV + vec2(i, j) * params.oneOverImageExtent;
			
// 			vec4 nearField = texture(nearFieldImage, sampleUV);
// 			vec4 farField = texture(farFieldImage, sampleUV);

// 			const vec3 luma = vec3(0.2126, 0.7152, 0.0722);

// 			float nearIntensity = dot(luma, nearField.rgb * nearField.a);
// 			if (nearIntensity > maxNearFieldIntensity)
// 			{
// 				maxNearField = nearField;
// 				maxNearFieldIntensity = nearIntensity;
// 			}

// 			float farIntensity = dot(luma, farField.rgb * farField.a);
// 			if (farIntensity > maxFarFieldIntensity)
// 			{
// 				maxFarField = farField;
// 				maxFarFieldIntensity = farIntensity;
// 			}
// 		}

// 	outNearFieldColorWithCoCAlpha = maxNearField;
// 	outFarFieldColorWithCoCAlpha = maxFarField;
// }
