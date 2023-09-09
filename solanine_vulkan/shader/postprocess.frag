#version 460

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outColor;


layout (set = 0, binding = 1) uniform UBOParams  // @TODO: just add in the global descriptor set
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
	vec3 linOut = pow(srgbIn.xyz,vec3(2.2));
	#else //SRGB_FAST_APPROXIMATION
	vec3 bLess = step(vec3(0.04045),srgbIn.xyz);
	vec3 linOut = mix( srgbIn.xyz/vec3(12.92), pow((srgbIn.xyz+vec3(0.055))/vec3(1.055),vec3(2.4)), bLess );
	#endif //SRGB_FAST_APPROXIMATION
	return vec4(linOut,srgbIn.w);
	#else //MANUAL_SRGB
	return srgbIn;
	#endif //MANUAL_SRGB
}

void main()
{
	

	vec4 combinedBloom =
		textureLod(bloomImage, inUV, 0.0) +
		textureLod(bloomImage, inUV, 1.0) +
		textureLod(bloomImage, inUV, 2.0) +
		textureLod(bloomImage, inUV, 3.0) +
		textureLod(bloomImage, inUV, 4.0);
	vec4 rawColor = mix(texture(mainImage, inUV), combinedBloom, 0.04);
	vec3 color = SRGBtoLINEAR(tonemap(rawColor)).rgb;
	vec4 uiColor = texture(uiImage, inUV);

	const float zNear = 1.0;
	const float zFar = 1000.0;
	float depth = texture(depthImage, inUV).r;
	depth = zNear * zFar / (zFar + depth * (zNear - zFar));

	// const float focusDepth = zFar * 0.5;
	// const float focusExtent = zFar * 0.5;
	const float focusDepth = 10.5;
	const float focusExtent = 7.5;
	const float blurExtent = focusExtent * 2.0;
	float depthRelativeToFocusDepth = abs(depth - focusDepth);
	if (depthRelativeToFocusDepth > focusExtent)
	{
		// color = vec3(0.0);  // @DEBUG: for seeing the in focus field.

		// float blurAmount = clamp((depthRelativeToFocusDepth - focusExtent) / blurExtent, 0.0, 1.0);
		// color = mix(color, combinedBloom.rgb, blurAmount);

		float blurAmount = (depthRelativeToFocusDepth - focusExtent) / blurExtent;
		color = mix(color, textureLod(bloomImage, inUV, clamp(blurAmount - 1.0, 0.0, 4.0)).rgb, clamp(blurAmount, 0.0, 1.0));
	}

	outColor = vec4(
		mix(color, uiColor.rgb, uiColor.a),
		1.0
	);
}
