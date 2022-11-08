#version 460

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outColor;


layout (set = 0, binding = 1) uniform UBOParams  // @TODO: just add in the global descriptor set
{
	vec4 lightDir;  // PAD
	float exposure;
	float gamma;
	//float prefilteredCubemapMipLevels;
	//float scaleIBLAmbient;
	//vec4 cascadeSplits;
	//mat4 cascadeViewProjMat[SHADOW_MAP_CASCADE_COUNT];
	//float zFarShadowZFarRatio;
	//float debugViewInputs;
	//float debugViewEquation;
} uboParams;

layout (set = 1, binding = 0) uniform sampler2D mainImage;


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
	vec3 color = SRGBtoLINEAR(tonemap(texture(mainImage, inUV))).rgb;
	outColor = vec4(color, 1.0);
}
