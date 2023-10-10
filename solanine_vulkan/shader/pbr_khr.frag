// PBR shader based on the Khronos WebGL PBR implementation
// See https://github.com/KhronosGroup/glTF-WebGL-PBR
// Supports both metallic roughness and specular glossiness inputs

#version 460

layout (location = 0) in vec3 inWorldPos;
layout (location = 1) in vec3 inViewPos;
layout (location = 2) in vec3 inNormal;
layout (location = 3) in vec2 inUV0;
layout (location = 4) in vec2 inUV1;
layout (location = 5) in vec4 inColor0;
layout (location = 6) in vec3 voxelFieldLightingGridPos;
layout (location = 7) flat in uint baseInstanceID;

layout (location = 0) out vec4 outFragColor;

//
// Scene bindings
//
layout(set = 0, binding = 0) uniform CameraBuffer
{
	mat4 view;
	mat4 projection;
	mat4 projectionView;
	vec3 cameraPosition;
} cameraData;

#define SHADOW_MAP_CASCADE_COUNT 4
layout (set = 0, binding = 1) uniform UBOParams
{
	vec4  lightDir;
	float exposure;
	float gamma;
	float prefilteredCubemapMipLevels;
	float scaleIBLAmbient;
	vec4  cascadeSplits;
	mat4  cascadeViewProjMat[SHADOW_MAP_CASCADE_COUNT];
	float shadowMapScale;
	float shadowJitterMapXScale;
	float shadowJitterMapYScale;
	float shadowJitterMapOffsetScale;
	float debugViewInputs;
	float debugViewEquation;
} uboParams;

layout (set = 0, binding = 2) uniform samplerCube    samplerIrradiance;
layout (set = 0, binding = 3) uniform samplerCube    prefilteredMap;
layout (set = 0, binding = 4) uniform sampler2D      samplerBRDFLUT;
layout (set = 0, binding = 5) uniform sampler2DArray shadowMap;
layout (set = 0, binding = 6) uniform sampler3D      shadowJitterMap;
#define SMOOTH_SHADOWS_ON
// #define SMOOTH_SHADOWS_SAMPLES_SQRT 8  // 8x8 samples
// #define SMOOTH_SHADOWS_SAMPLES_COUNT 64
// #define SMOOTH_SHADOWS_INV_SAMPLES_COUNT (1.0 / SMOOTH_SHADOWS_SAMPLES_COUNT)
// #define SMOOTH_SHADOWS_SAMPLES_COUNT_DIV_2 32
// #define SMOOTH_SHADOWS_INV_SAMPLES_COUNT_DIV_2 (1.0 / SMOOTH_SHADOWS_SAMPLES_COUNT_DIV_2)

// #define SMOOTH_SHADOWS_SAMPLES_SQRT 6  // 6x6 samples
// #define SMOOTH_SHADOWS_SAMPLES_COUNT 36
// #define SMOOTH_SHADOWS_INV_SAMPLES_COUNT (1.0 / SMOOTH_SHADOWS_SAMPLES_COUNT)
// #define SMOOTH_SHADOWS_SAMPLES_COUNT_DIV_2 18
// #define SMOOTH_SHADOWS_INV_SAMPLES_COUNT_DIV_2 (1.0 / SMOOTH_SHADOWS_SAMPLES_COUNT_DIV_2)

// #define SMOOTH_SHADOWS_SAMPLES_SQRT 4  // 4x4 samples
// #define SMOOTH_SHADOWS_SAMPLES_COUNT 16
// #define SMOOTH_SHADOWS_INV_SAMPLES_COUNT (1.0 / SMOOTH_SHADOWS_SAMPLES_COUNT)
// #define SMOOTH_SHADOWS_SAMPLES_COUNT_DIV_2 8
// #define SMOOTH_SHADOWS_INV_SAMPLES_COUNT_DIV_2 (1.0 / SMOOTH_SHADOWS_SAMPLES_COUNT_DIV_2)

#define SMOOTH_SHADOWS_SAMPLES_SQRT 2  // 2x2 samples
#define SMOOTH_SHADOWS_SAMPLES_COUNT 4
#define SMOOTH_SHADOWS_INV_SAMPLES_COUNT (1.0 / SMOOTH_SHADOWS_SAMPLES_COUNT)
#define SMOOTH_SHADOWS_SAMPLES_COUNT_DIV_2 2
#define SMOOTH_SHADOWS_INV_SAMPLES_COUNT_DIV_2 (1.0 / SMOOTH_SHADOWS_SAMPLES_COUNT_DIV_2)


// Instance ID Pointers
// @COPYPASTA
struct InstancePointer
{
	uint objectID;
	uint materialID;
	uint animatorNodeID;
	uint voxelFieldLightingGridID;
};

layout(std140, set = 2, binding = 0) readonly buffer InstancePtrBuffer
{
	InstancePointer pointers[];
} instancePtrBuffer;


//
// Material bindings
//
#define MAX_NUM_MAPS 128
layout (set = 3, binding = 0) uniform sampler2D textureMaps[MAX_NUM_MAPS];

struct MaterialParam
{
	// Texture map references
	uint colorMapIndex;
	uint physicalDescriptorMapIndex;
	uint normalMapIndex;
	uint aoMapIndex;
	uint emissiveMapIndex;

	// Material properties
	vec4  baseColorFactor;
	vec4  emissiveFactor;
	vec4  diffuseFactor;
	vec4  specularFactor;
	float workflow;
	int   baseColorTextureSet;
	int   physicalDescriptorTextureSet;
	int   normalTextureSet;
	int   occlusionTextureSet;
	int   emissiveTextureSet;
	float metallicFactor;
	float roughnessFactor;
	float alphaMask;
	float alphaMaskCutoff;
};

#define MAX_NUM_MATERIALS 256
layout (std140, set = 3, binding = 1) readonly buffer MaterialCollection
{
	MaterialParam params[MAX_NUM_MATERIALS];
} materialCollection;

//
// Voxel field lighting grid lightmaps
//
#define MAX_NUM_VOXEL_FIELD_LIGHTMAPS 8
layout (set = 5, binding = 1) uniform sampler3D voxelFieldLightmaps[MAX_NUM_VOXEL_FIELD_LIGHTMAPS];

// Encapsulate the various inputs used by the various functions in the shading equation
// We store values in this struct to simplify the integration of alternative implementations
// of the shading terms, outlined in the Readme.MD Appendix.
struct PBRInfo
{
	float NdotL;                  // cos angle between normal and light direction
	float NdotV;                  // cos angle between normal and view direction
	float NdotH;                  // cos angle between normal and half vector
	float LdotH;                  // cos angle between light direction and half vector
	float VdotH;                  // cos angle between view direction and half vector
	float perceptualRoughness;    // roughness value, as authored by the model creator (input to shader)
	float metalness;              // metallic value at the surface
	vec3  reflectance0;            // full reflectance color (normal incidence angle)
	vec3  reflectance90;           // reflectance color at grazing angle
	float alphaRoughness;         // roughness mapped to a more linear change in the roughness (proposed by [2])
	vec3  diffuseColor;            // color contribution from diffuse lighting
	vec3  specularColor;           // color contribution from specular lighting
};

const float M_PI = 3.141592653589793;
const float c_MinRoughness = 0.04;

const float PBR_WORKFLOW_METALLIC_ROUGHNESS = 0.0;
const float PBR_WORKFLOW_SPECULAR_GLOSSINESS = 1.0f;


#define MANUAL_SRGB 1

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

// Find the normal for this fragment, pulling either from a predefined normal map
// or from the interpolated mesh normal and tangent attributes.
vec3 getNormal()
{
	// @COPYPASTA
	MaterialParam material = materialCollection.params[instancePtrBuffer.pointers[baseInstanceID].materialID];  // @TODO: figure out how to use the different material things. 

	// Perturb normal, see http://www.thetenthplanet.de/archives/1180
	vec3 tangentNormal = texture(textureMaps[material.normalMapIndex], material.normalTextureSet == 0 ? inUV0 : inUV1).xyz * 2.0 - 1.0;

	vec3 q1 = dFdx(inWorldPos);
	vec3 q2 = dFdy(inWorldPos);
	vec2 st1 = dFdx(inUV0);
	vec2 st2 = dFdy(inUV0);

	vec3 N = normalize(inNormal);
	vec3 T = normalize(q1 * st2.t - q2 * st1.t);
	vec3 B = -normalize(cross(N, T));
	mat3 TBN = mat3(T, B, N);

	return normalize(TBN * tangentNormal);
}

float getVoxelFieldLightmapScaleIBLAmbient()
{
	// return 1.0;
	return texture(voxelFieldLightmaps[instancePtrBuffer.pointers[baseInstanceID].voxelFieldLightingGridID], voxelFieldLightingGridPos).x;
}

// Calculation of the lighting contribution from an optional Image Based Light source.
// Precomputed Environment Maps are required uniform inputs and are computed as outlined in [1].
// See our README.md on Environment Maps [3] for additional discussion.
vec3 getIBLContribution(PBRInfo pbrInputs, vec3 n, vec3 reflection)
{
	float lod = (pbrInputs.perceptualRoughness * uboParams.prefilteredCubemapMipLevels);
	// retrieve a scale and bias to F0. See [1], Figure 3
	vec3 brdf = (texture(samplerBRDFLUT, vec2(pbrInputs.NdotV, 1.0 - pbrInputs.perceptualRoughness))).rgb;
	vec3 diffuseLight = texture(samplerIrradiance, n).rgb;

	vec3 specularLight = textureLod(prefilteredMap, reflection, lod).rgb;

	vec3 diffuse = diffuseLight * pbrInputs.diffuseColor;
	vec3 specular = specularLight * (pbrInputs.specularColor * brdf.x + brdf.y);

	// For presentation, this allows us to disable IBL terms
	// For presentation, this allows us to disable IBL terms
	float lightmapScaleIBLAmbient = getVoxelFieldLightmapScaleIBLAmbient();
	diffuse *= uboParams.scaleIBLAmbient * lightmapScaleIBLAmbient;
	specular *= uboParams.scaleIBLAmbient * lightmapScaleIBLAmbient;

	return diffuse + specular;
}

// Basic Lambertian diffuse
// Implementation from Lambert's Photometria https://archive.org/details/lambertsphotome00lambgoog
// See also [1], Equation 1
vec3 diffuse(PBRInfo pbrInputs)
{
	return pbrInputs.diffuseColor / M_PI;
}

// The following equation models the Fresnel reflectance term of the spec equation (aka F())
// Implementation of fresnel from [4], Equation 15
vec3 specularReflection(PBRInfo pbrInputs)
{
	return pbrInputs.reflectance0 + (pbrInputs.reflectance90 - pbrInputs.reflectance0) * pow(clamp(1.0 - pbrInputs.VdotH, 0.0, 1.0), 5.0);
}

// This calculates the specular geometric attenuation (aka G()),
// where rougher material will reflect less light back to the viewer.
// This implementation is based on [1] Equation 4, and we adopt their modifications to
// alphaRoughness as input as originally proposed in [2].
float geometricOcclusion(PBRInfo pbrInputs)
{
	float NdotL = pbrInputs.NdotL;
	float NdotV = pbrInputs.NdotV;
	float r = pbrInputs.alphaRoughness;

	float attenuationL = 2.0 * NdotL / (NdotL + sqrt(r * r + (1.0 - r * r) * (NdotL * NdotL)));
	float attenuationV = 2.0 * NdotV / (NdotV + sqrt(r * r + (1.0 - r * r) * (NdotV * NdotV)));
	return attenuationL * attenuationV;
}

// The following equation(s) model the distribution of microfacet normals across the area being drawn (aka D())
// Implementation from "Average Irregularity Representation of a Roughened Surface for Ray Reflection" by T. S. Trowbridge, and K. P. Reitz
// Follows the distribution function recommended in the SIGGRAPH 2013 course notes from EPIC Games [1], Equation 3.
float microfacetDistribution(PBRInfo pbrInputs)
{
	float roughnessSq = pbrInputs.alphaRoughness * pbrInputs.alphaRoughness;
	float f = (pbrInputs.NdotH * roughnessSq - pbrInputs.NdotH) * pbrInputs.NdotH + 1.0;
	return roughnessSq / (M_PI * f * f);
}

// Gets metallic factor from specular glossiness workflow inputs 
float convertMetallic(vec3 diffuse, vec3 specular, float maxSpecular)
{
	float perceivedDiffuse = sqrt(0.299 * diffuse.r * diffuse.r + 0.587 * diffuse.g * diffuse.g + 0.114 * diffuse.b * diffuse.b);
	float perceivedSpecular = sqrt(0.299 * specular.r * specular.r + 0.587 * specular.g * specular.g + 0.114 * specular.b * specular.b);
	if (perceivedSpecular < c_MinRoughness)
	{
		return 0.0;
	}
	float a = c_MinRoughness;
	float b = perceivedDiffuse * (1.0 - maxSpecular) / (1.0 - c_MinRoughness) + perceivedSpecular - 2.0 * c_MinRoughness;
	float c = c_MinRoughness - perceivedSpecular;
	float D = max(b * b - 4.0 * a * c, 0.0);
	return clamp((-b + sqrt(D)) / (2.0 * a), 0.0, 1.0);
}

// Shadow map sampling
const mat4 BIAS_MAT = mat4(
	0.5, 0.0, 0.0, 0.0,
	0.0, 0.5, 0.0, 0.0,
	0.0, 0.0, 1.0, 0.0,
	0.5, 0.5, 0.0, 1.0
);

float textureProj(vec4 shadowCoord, vec2 offset, uint cascadeIndex)
{
	float shadow = 1.0;
	float bias = 0.005;

	if (shadowCoord.z > -1.0 && shadowCoord.z < 1.0)
	{
		float dist = texture(shadowMap, vec3(shadowCoord.st + offset, cascadeIndex)).r;
		if (shadowCoord.w > 0 && dist < shadowCoord.z - bias)
			shadow = 0.0;
	}
	return shadow;
}

#ifdef SMOOTH_SHADOWS_ON
float smoothShadow(vec4 shadowCoord, uint cascadeIndex)
{
	float shadow = 0.0;
	vec3 jcoord = vec3(gl_FragCoord.x * uboParams.shadowJitterMapXScale, gl_FragCoord.y * uboParams.shadowJitterMapYScale, 0.0);

	// Cheap shadow samples first
	for (int i = 0; i < SMOOTH_SHADOWS_SAMPLES_SQRT / 2; i++)
	{
		vec4 offset = texture(shadowJitterMap, jcoord) * uboParams.shadowJitterMapOffsetScale;
		jcoord.z += SMOOTH_SHADOWS_INV_SAMPLES_COUNT_DIV_2;

		shadow += textureProj(shadowCoord, offset.xy * uboParams.shadowMapScale, cascadeIndex) / SMOOTH_SHADOWS_SAMPLES_SQRT;  // Two sets of offsets are stored in rg and ba channels of the textures.
		shadow += textureProj(shadowCoord, offset.zw * uboParams.shadowMapScale, cascadeIndex) / SMOOTH_SHADOWS_SAMPLES_SQRT;
	}

	if ((shadow - 1.0) * shadow != 0)
	{
		// If shadow is partial, then likely in penumbra. Do expensive samples!
		shadow *= 1.0 / SMOOTH_SHADOWS_SAMPLES_SQRT;

		for (int i = 0; i < SMOOTH_SHADOWS_SAMPLES_COUNT_DIV_2 - (SMOOTH_SHADOWS_SAMPLES_SQRT / 2); i++)
		{
			// @COPYPASTA.
			vec4 offset = texture(shadowJitterMap, jcoord) * uboParams.shadowJitterMapOffsetScale;
			jcoord.z += SMOOTH_SHADOWS_INV_SAMPLES_COUNT_DIV_2;

			shadow += textureProj(shadowCoord, offset.xy * uboParams.shadowMapScale, cascadeIndex) * SMOOTH_SHADOWS_INV_SAMPLES_COUNT;  // Two sets of offsets are stored in rg and ba channels of the textures.
			shadow += textureProj(shadowCoord, offset.zw * uboParams.shadowMapScale, cascadeIndex) * SMOOTH_SHADOWS_INV_SAMPLES_COUNT;
		}
	}

	return shadow;
}
#endif


void main()
{
	// outFragColor = vec4(vec3(inViewPos.z / uboParams.cascadeSplits.w), 1.0);
	// return;

	// outFragColor = vec4(voxelFieldLightingGridPos, 1.0);
	// return;

	// float amb = getVoxelFieldLightmapScaleIBLAmbient();
	// // amb = clamp(amb * 2.0, 0.0, 1.0);
	// // amb = clamp(amb * 2.0 - 1.0, 0.0, 1.0);
	// outFragColor = vec4(vec3(amb), 1.0);
	// return;

	MaterialParam material = materialCollection.params[instancePtrBuffer.pointers[baseInstanceID].materialID];  // @TODO: figure out how to use the different material things.

	float perceptualRoughness;
	float metallic;
	vec3 diffuseColor;
	vec4 baseColor;

	vec3 f0 = vec3(0.04);

	if (material.alphaMask == 1.0f)
	{
		if (material.baseColorTextureSet > -1)
		{
			baseColor = SRGBtoLINEAR(texture(textureMaps[material.colorMapIndex], material.baseColorTextureSet == 0 ? inUV0 : inUV1)) * material.baseColorFactor;
		} else {
			baseColor = material.baseColorFactor;
		}
		if (baseColor.a < material.alphaMaskCutoff)
		{
			discard;
		}
	}

	if (material.workflow == PBR_WORKFLOW_METALLIC_ROUGHNESS)
	{
		// Metallic and Roughness material properties are packed together
		// In glTF, these factors can be specified by fixed scalar values
		// or from a metallic-roughness map
		perceptualRoughness = material.roughnessFactor;
		metallic = material.metallicFactor;
		if (material.physicalDescriptorTextureSet > -1)
		{
			// Roughness is stored in the 'g' channel, metallic is stored in the 'b' channel.
			// This layout intentionally reserves the 'r' channel for (optional) occlusion map data
			vec4 mrSample = texture(textureMaps[material.physicalDescriptorMapIndex], material.physicalDescriptorTextureSet == 0 ? inUV0 : inUV1);
			perceptualRoughness = mrSample.g * perceptualRoughness;
			metallic = mrSample.b * metallic;
		} else {
			perceptualRoughness = clamp(perceptualRoughness, c_MinRoughness, 1.0);
			metallic = clamp(metallic, 0.0, 1.0);
		}
		// Roughness is authored as perceptual roughness; as is convention,
		// convert to material roughness by squaring the perceptual roughness [2].

		// The albedo may be defined from a base texture or a flat color
		if (material.baseColorTextureSet > -1)
		{
			baseColor = SRGBtoLINEAR(texture(textureMaps[material.colorMapIndex], material.baseColorTextureSet == 0 ? inUV0 : inUV1)) * material.baseColorFactor;
		} else {
			baseColor = material.baseColorFactor;
		}
	}

	if (material.workflow == PBR_WORKFLOW_SPECULAR_GLOSSINESS)
	{
		// Values from specular glossiness workflow are converted to metallic roughness
		if (material.physicalDescriptorTextureSet > -1)
		{
			perceptualRoughness = 1.0 - texture(textureMaps[material.physicalDescriptorMapIndex], material.physicalDescriptorTextureSet == 0 ? inUV0 : inUV1).a;
		} else {
			perceptualRoughness = 0.0;
		}

		const float epsilon = 1e-6;

		vec4 diffuse = SRGBtoLINEAR(texture(textureMaps[material.colorMapIndex], inUV0));
		vec3 specular = SRGBtoLINEAR(texture(textureMaps[material.physicalDescriptorMapIndex], inUV0)).rgb;

		float maxSpecular = max(max(specular.r, specular.g), specular.b);

		// Convert metallic value from specular glossiness inputs
		metallic = convertMetallic(diffuse.rgb, specular, maxSpecular);

		vec3 baseColorDiffusePart = diffuse.rgb * ((1.0 - maxSpecular) / (1 - c_MinRoughness) / max(1 - metallic, epsilon)) * material.diffuseFactor.rgb;
		vec3 baseColorSpecularPart = specular - (vec3(c_MinRoughness) * (1 - metallic) * (1 / max(metallic, epsilon))) * material.specularFactor.rgb;
		baseColor = vec4(mix(baseColorDiffusePart, baseColorSpecularPart, metallic * metallic), diffuse.a);

	}

	baseColor *= inColor0;

	diffuseColor = baseColor.rgb * (vec3(1.0) - f0);
	diffuseColor *= 1.0 - metallic;
		
	float alphaRoughness = perceptualRoughness * perceptualRoughness;

	vec3 specularColor = mix(f0, baseColor.rgb, metallic);

	// Compute reflectance.
	float reflectance = max(max(specularColor.r, specularColor.g), specularColor.b);

	// For typical incident reflectance range (between 4% to 100%) set the grazing reflectance to 100% for typical fresnel effect.
	// For very low reflectance range on highly diffuse objects (below 4%), incrementally reduce grazing reflecance to 0%.
	float reflectance90 = clamp(reflectance * 25.0, 0.0, 1.0);
	vec3 specularEnvironmentR0 = specularColor.rgb;
	vec3 specularEnvironmentR90 = vec3(1.0, 1.0, 1.0) * reflectance90;

	vec3 n = (material.normalTextureSet > -1) ? getNormal() : normalize(inNormal);
	vec3 v = normalize(cameraData.cameraPosition - inWorldPos);    // Vector from surface point to camera
	vec3 l = normalize(uboParams.lightDir.xyz);     // Vector from surface point to light
	vec3 h = normalize(l+v);                        // Half vector between both l and v
	vec3 reflection = -normalize(reflect(v, n));
	reflection.y *= -1.0f;

	float NdotL = clamp(dot(n, l), 0.001, 1.0);
	float NdotV = clamp(abs(dot(n, v)), 0.001, 1.0);
	float NdotH = clamp(dot(n, h), 0.0, 1.0);
	float LdotH = clamp(dot(l, h), 0.0, 1.0);
	float VdotH = clamp(dot(v, h), 0.0, 1.0);

	PBRInfo pbrInputs = PBRInfo(
		NdotL,
		NdotV,
		NdotH,
		LdotH,
		VdotH,
		perceptualRoughness,
		metallic,
		specularEnvironmentR0,
		specularEnvironmentR90,
		alphaRoughness,
		diffuseColor,
		specularColor
	);

	// Calculate the shading terms for the microfacet specular shading model
	vec3 F = specularReflection(pbrInputs);
	float G = geometricOcclusion(pbrInputs);
	float D = microfacetDistribution(pbrInputs);

	const vec3 u_LightColor = vec3(1.0);

	// Calculation of analytical lighting contribution
	vec3 diffuseContrib = (1.0 - F) * diffuse(pbrInputs);
	vec3 specContrib = F * G * D / (4.0 * NdotL * NdotV);

	float shadow = 1.0;
	if (inViewPos.z > uboParams.cascadeSplits[SHADOW_MAP_CASCADE_COUNT - 1])  // @NOTE: this feels really short, but it's the right amount. Any more, and corners will get cut off.  -Timo 2023/08/31
	{
		// Get shadow cascade index for the current fragment's view position
		uint cascadeIndex = 0;
		for (uint i = 0; i < SHADOW_MAP_CASCADE_COUNT - 1; i++)
			if (inViewPos.z < uboParams.cascadeSplits[i])
				cascadeIndex = i + 1;

		// @DEBUG: for seeing the cascade colors.
		// vec3 colors[] = {
		// 	vec3(1.0, 0.0, 0.0),
		// 	vec3(1.0, 1.0, 0.0),
		// 	vec3(1.0, 0.0, 1.0),
		// 	vec3(0.0, 1.0, 0.0),
		// };
		// outFragColor = vec4(colors[cascadeIndex], 1.0);
		// return;

		// Depth compare for shadowing
		vec4 shadowCoord = (BIAS_MAT * uboParams.cascadeViewProjMat[cascadeIndex]) * vec4(inWorldPos, 1.0);

		shadowCoord /= shadowCoord.w;
#ifdef SMOOTH_SHADOWS_ON
		float fadeBias[] = {
			10.0,
			2.85,
			2.0,
			1.0,
		};
		float shadowDistanceFade = (inViewPos.z - uboParams.cascadeSplits[cascadeIndex]) * fadeBias[cascadeIndex];
		shadow = smoothShadow(shadowCoord, cascadeIndex);

		float nextShadow = 1.0;
		if (shadowDistanceFade < 1.0 && cascadeIndex != SHADOW_MAP_CASCADE_COUNT)
		{
			vec4 nextShadowCoord = (BIAS_MAT * uboParams.cascadeViewProjMat[cascadeIndex + 1]) * vec4(inWorldPos, 1.0);
			nextShadow = smoothShadow(nextShadowCoord, cascadeIndex + 1);
			// @DEBUG: for seeing where the fade areas are.
			// outFragColor = vec4(1, 0, 0, 1);
			// return;
		}
		shadow = mix(nextShadow, shadow, min(1.0, shadowDistanceFade));
		// @DEBUG: for seeing where the fade areas are.
		// outFragColor = vec4(1, 1, 0, 1);
		// return;
#else
		shadow = textureProj(shadowCoord, vec2(0.0), cascadeIndex);
#endif
	}
	shadow = shadow * NdotL;

	// Obtain final intensity as reflectance (BRDF) scaled by the energy of the light (cosine law)
	vec3 color = NdotL * u_LightColor * shadow * (diffuseContrib + specContrib);

	// Calculate lighting contribution from image based lighting source (IBL)
	color += getIBLContribution(pbrInputs, n, reflection);

	const float u_OcclusionStrength = 1.0f;

	// Apply optional PBR terms for additional (optional) shading
	if (material.occlusionTextureSet > -1)
	{
		float ao = texture(textureMaps[material.aoMapIndex], (material.occlusionTextureSet == 0 ? inUV0 : inUV1)).r;
		color = mix(color, color * ao, u_OcclusionStrength);
	}

	const float u_EmissiveFactor = 1.0f;
	if (material.emissiveTextureSet > -1)
	{
		vec3 emissive = SRGBtoLINEAR(texture(textureMaps[material.emissiveMapIndex], material.emissiveTextureSet == 0 ? inUV0 : inUV1)).rgb * u_EmissiveFactor;
		color += emissive;
	}
	
	outFragColor = vec4(color, baseColor.a);

	// Shader inputs debug visualization
	if (uboParams.debugViewInputs > 0.0)
	{
		int index = int(uboParams.debugViewInputs);
		switch (index)
		{
			case 1:
				outFragColor.rgba = material.baseColorTextureSet > -1 ? texture(textureMaps[material.colorMapIndex], material.baseColorTextureSet == 0 ? inUV0 : inUV1) : vec4(1.0f);
				break;
			case 2:
				outFragColor.rgb = (material.normalTextureSet > -1) ? texture(textureMaps[material.normalMapIndex], material.normalTextureSet == 0 ? inUV0 : inUV1).rgb : normalize(inNormal);
				break;
			case 3:
				outFragColor.rgb = (material.occlusionTextureSet > -1) ? texture(textureMaps[material.aoMapIndex], material.occlusionTextureSet == 0 ? inUV0 : inUV1).rrr : vec3(0.0f);
				break;
			case 4:
				outFragColor.rgb = (material.emissiveTextureSet > -1) ? texture(textureMaps[material.emissiveMapIndex], material.emissiveTextureSet == 0 ? inUV0 : inUV1).rgb : vec3(0.0f);
				break;
			case 5:
				outFragColor.rgb = texture(textureMaps[material.physicalDescriptorMapIndex], inUV0).bbb;
				break;
			case 6:
				outFragColor.rgb = texture(textureMaps[material.physicalDescriptorMapIndex], inUV0).ggg;
				break;
		}
		outFragColor = SRGBtoLINEAR(outFragColor);
	}

	// PBR equation debug visualization
	// "none", "Diff (l,n)", "F (l,h)", "G (l,v,h)", "D (h)", "Specular"
	if (uboParams.debugViewEquation > 0.0)
	{
		int index = int(uboParams.debugViewEquation);
		switch (index)
		{
			case 1:
				outFragColor.rgb = diffuseContrib;
				break;
			case 2:
				outFragColor.rgb = F;
				break;
			case 3:
				outFragColor.rgb = vec3(G);
				break;
			case 4: 
				outFragColor.rgb = vec3(D);
				break;
			case 5:
				outFragColor.rgb = specContrib;
				break;				
		}
	}
}
