#version 460

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outColor;


layout (push_constant) uniform CoCParams
{
	float cameraZNear;
	float cameraZFar;
	float focusDepth;
	float focusExtent;
	float blurExtent;
} cocParams;

layout (set = 0, binding = 1) uniform UBOParams
{
	vec4  lightDir;  // PAD
	float exposure;
	float gamma;
	//float prefilteredCubemapMipLevels;
	//float scaleIBLAmbient;
	//vec4 cascadeSplits;
	//mat4 cascadeViewProjMat[SHADOW_MAP_CASCADE_COUNT];
	//float shadowMapScale;
	//float shadowJitterMapXScale;
	//float shadowJitterMapYScale;
	//float shadowJitterMapOffsetScale;
	//float debugViewInputs;
	//float debugViewEquation;
} uboParams;

layout (set = 1, binding = 0) uniform sampler2D mainImage;
layout (set = 1, binding = 1) uniform sampler2D uiImage;
layout (set = 1, binding = 2) uniform sampler2D bloomImage;
layout (set = 1, binding = 3) uniform sampler2D depthImage;
layout (set = 1, binding = 4) uniform sampler2D dofCoCImage;
layout (set = 1, binding = 5) uniform sampler2D dofNearImage;
layout (set = 1, binding = 6) uniform sampler2D dofFarImage;


#define MANUAL_SRGB 1

vec3 Uncharted2Tonemap(vec3 color)
{
	float A = 0.15;
	float B = 0.50;
	float C = 0.10;
	float D = 0.20;
	float E = 0.02;
	float F = 0.30;
	float W = 11.2;
	return ((color*(A*color+C*B)+D*E)/(color*(A*color+B)+D*F))-E/F;
}

vec4 tonemap(vec4 color)
{
	vec3 outcol = Uncharted2Tonemap(color.rgb * uboParams.exposure);
	outcol = outcol * (1.0f / Uncharted2Tonemap(vec3(11.2f)));
	return vec4(pow(outcol, vec3(1.0f / uboParams.gamma)), color.a);
}

vec4 SRGBtoLINEAR(vec4 srgbIn)
{
	#ifdef MANUAL_SRGB
	#ifdef SRGB_FAST_APPROXIMATION
	vec3 linOut = pow(srgbIn.xyz, vec3(2.2));
	#else //SRGB_FAST_APPROXIMATION
	vec3 bLess = step(vec3(0.04045), srgbIn.xyz);
	vec3 linOut = mix(srgbIn.xyz / vec3(12.92), pow((srgbIn.xyz + vec3(0.055)) / vec3(1.055), vec3(2.4)), bLess);
	#endif //SRGB_FAST_APPROXIMATION
	return vec4(linOut, srgbIn.w);
	#else //MANUAL_SRGB
	return srgbIn;
	#endif //MANUAL_SRGB
}

void main()
{
	vec4 rawColor = texture(mainImage, inUV);
	vec4 dofFarColorAndCoC = texture(dofFarImage, inUV);
	float farCoC = texture(dofCoCImage, inUV).g;  // @NOTE: using the NEAREST filter.
	rawColor.rgb = mix(rawColor.rgb, dofFarColorAndCoC.rgb, min(farCoC, dofFarColorAndCoC.a));
	vec4 dofNearColorAndCoC = texture(dofNearImage, inUV);
	rawColor.rgb = mix(rawColor.rgb, dofNearColorAndCoC.rgb, dofNearColorAndCoC.a);

	// @DEBUG: See DOF ranges.
	// if (dofNearColorAndCoC.a > 0.0)
	// 	rawColor.r = 1.0;
	// if (dofFarColorAndCoC.a > 0.0)
	// 	rawColor.g = 1.0;

	vec4 combinedBloom =
		textureLod(bloomImage, inUV, 0.0) +
		textureLod(bloomImage, inUV, 1.0) +
		textureLod(bloomImage, inUV, 2.0) +
		textureLod(bloomImage, inUV, 3.0) +
		textureLod(bloomImage, inUV, 4.0);
	rawColor = mix(rawColor, combinedBloom, /*0.04*/0.0);  // @NOTE: for now turn off bloom since I think it should be after DOF (which in the render pipeline in `VulkanEngine` it's before DOF), but it's here!

	vec3 color = SRGBtoLINEAR(tonemap(rawColor)).rgb;
	vec4 uiColor = texture(uiImage, inUV);

	outColor = vec4(
		mix(color, uiColor.rgb, uiColor.a),
		1.0
	);
}
