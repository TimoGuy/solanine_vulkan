// PBR shader based on the Khronos WebGL PBR implementation
// See https://github.com/KhronosGroup/glTF-WebGL-PBR
// Supports both metallic roughness and specular glossiness inputs

#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout (location = 0) in vec3 inWorldPos;
layout (location = 1) in vec3 inViewPos;
layout (location = 2) in vec3 inNormal;
layout (location = 3) in vec3 inObjectPos;
layout (location = 4) in vec3 inObjectNormal;
layout (location = 5) flat in uint baseInstanceID;

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
struct MaterialParam
{
	// Texture map references
	uint  textureMapIndex;
	uint  pad0;
	uint  pad1;
	uint  pad2;

	// Material properties
	vec4  standableColor;
	vec4  steepColor;
	float slopeAngleSin;
	float pad3;
	float pad4;
	float pad5;
};

#define MAX_NUM_MATERIALS 256
layout (std140, set = 3, binding = 0) readonly buffer MaterialCollection
{
	uint materialIDOffset;
	int pad0;
	int pad1;
	int pad2;
	MaterialParam params[MAX_NUM_MATERIALS];
} materialCollection;

layout (set = 3, binding = 1) uniform sampler2D textureMaps[];

//
// Voxel field lighting grid lightmaps
//
#define MAX_NUM_VOXEL_FIELD_LIGHTMAPS 8
layout (set = 4, binding = 1) uniform sampler3D voxelFieldLightmaps[MAX_NUM_VOXEL_FIELD_LIGHTMAPS];


mat3 rotateFromAtoB(vec3 a, vec3 b)
{
    // Ref: cglm: affine.h: glm_rotate_make().
    // Another Ref: https://stackoverflow.com/questions/34366655/glm-make-rotation-matrix-from-vec3 (see accepted answer).
    // Yet another Ref: https://iquilezles.org/articles/noacos/

    // @NOTE: assume a and b to be normalized vectors.
    vec3 v = cross(b, a);
    float c = dot(b, a);  // Cos of angle of rotation. 
    float k = 1.0 / (1.0 + c);

    return mat3( v.x * v.x * k + c,      v.y * v.x * k - v.z,    v.z * v.x * k + v.y,
                 v.x * v.y * k + v.z,    v.y * v.y * k + c,      v.z * v.y * k - v.x,
                 v.x * v.z * k - v.y,    v.y * v.z * k + v.x,    v.z * v.z * k + c    );
}

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
	MaterialParam material = materialCollection.params[instancePtrBuffer.pointers[baseInstanceID].materialID - materialCollection.materialIDOffset];

    // Calculate diffuse.
	vec3 n = normalize(inNormal);
	vec3 l = normalize(uboParams.lightDir.xyz);

	float NdotL = clamp(dot(n, l), 0.001, 1.0);
    float diffuse = NdotL * 0.8 + 0.2;  // FAKE! But oh well, player's never gonna see it, so SUCK IT!  -Dmitri 12/26/2023

    // Calculate world position based UV.
	vec2 calculatedUV;
    // calculatedUV = (rotateFromAtoB(n, vec3(0.0, 1.0, 0.0)) * inObjectPos).xz * material.standableColor.zw + material.standableColor.xy;
    calculatedUV = (rotateFromAtoB(n, vec3(0.0, 1.0, 0.0)) * inObjectPos).xz * vec2(1, 1) + vec2(0.1, 0.1);
	// outFragColor = vec4(calculatedUV, 0, 1);
	// return;



#define SIN45 0.70710678118
// #define SIN45 1.0

	// NEW NEW NEW NEW STRAT!!!!

	// // FOCUS!!!  @DEBUG
	// if (n.x < 0.0 || n.y < 0.0 || n.z < 0.0)
	// {
	// 	outFragColor = vec4(vec3(0), 1);
	// 	return;
	// }
	
	// Calc dot product with different cases.
	float best = -10000000000.0;
	int bestIdx;

	float cur = dot(n, vec3(1, 0, 0));
	if (cur > best)
	{
		best = cur;
		bestIdx = 0;
	}

	cur = dot(n, vec3(0, 1, 0));
	if (cur > best)
	{
		best = cur;
		bestIdx = 1;
	}

	cur = dot(n, vec3(0, 0, 1));
	if (cur > best)
	{
		best = cur;
		bestIdx = 2;
	}

	cur = dot(n, normalize(vec3(1, 1, 0)));
	if (cur > best)
	{
		best = cur;
		bestIdx = 3;
	}

	cur = dot(n, normalize(vec3(0, 1, 1)));
	if (cur > best)
	{
		best = cur;
		bestIdx = 4;
	}

	cur = dot(n, normalize(vec3(1, 0, 1)));
	if (cur > best)
	{
		best = cur;
		bestIdx = 5;
	}

	cur = dot(n, normalize(vec3(1, 1, 1)));
	if (cur > best)
	{
		best = cur;
		bestIdx = 6;
	}

	switch (bestIdx)
	{
		case 0:
			outFragColor = vec4(1, 0, 0, 1);
			calculatedUV = inObjectPos.yz;
			break;

		case 1:
			outFragColor = vec4(0, 1, 0, 1);
			calculatedUV = inObjectPos.xz;
			break;

		case 2:
			outFragColor = vec4(0, 0, 1, 1);
			calculatedUV = inObjectPos.xy;
			break;

		case 3:
		{
			outFragColor = vec4(1, 1, 0, 1);
			vec3 mrDot = normalize(vec3(-1, 1, 0));
			calculatedUV.x = dot(mrDot, inObjectPos);
			calculatedUV.y = inObjectPos.z;
		} break;

		case 4:
		{
			outFragColor = vec4(0, 1, 1, 1);
			vec3 mrDot = normalize(vec3(0, 1, -1));
			calculatedUV.x = dot(mrDot, inObjectPos);
			calculatedUV.y = inObjectPos.x;

		} break;

		case 5:
		{
			outFragColor = vec4(1, 0, 1, 1);
			vec3 mrDot = normalize(vec3(-1, 0, 1));
			calculatedUV.x = dot(mrDot, inObjectPos);
			calculatedUV.y = inObjectPos.y;
		} break;

		case 6:
		{
			outFragColor = vec4(1, 1, 1, 1);
			vec3 mrDot = normalize(vec3(-1, 0, 1));
			calculatedUV.x = dot(mrDot, inObjectPos);
			vec3 mrDot2 = normalize(vec3(-1, 1, -1));
			calculatedUV.y = dot(mrDot2, inObjectPos);
		} break;
	}

	vec3 objN = normalize(inObjectNormal);

	vec3 right;
	if (abs(dot(objN, vec3(0, 1, 0))) > 0.999999)
		right = vec3(1, 0, 0);
	else
		right = normalize(cross(vec3(0, 1, 0), objN));
	vec3 forward = normalize(cross(objN, right));

	calculatedUV.x = dot(right, inObjectPos);
	calculatedUV.y = dot(forward, inObjectPos);


	// outFragColor = vec4(inObjectPos, 1);
	// if (inObjectPos.x < 0.0 && inObjectPos.y < 0.0 && inObjectPos.z < 0.0)
	// 	outFragColor = vec4(1, 1, 1, 1);
	// return;



	// float e = 1.9;  // @NOTE: this doesn't work bc it causes grid to grow out the center. Good idea tho.
	// calculatedUV =
	// 	inObjectPos.yz * pow(1.0 - n.x, e) +
	// 	inObjectPos.xz * pow(1.0 - n.y, e) +
	// 	inObjectPos.xy * pow(1.0 - n.z, e);







// #if 0
// 	// vec3 absNormal = normalize(abs(n));
// 	// vec2 calculatedUV;
// 	// float normalMax = max(absNormal.x, max(absNormal.y, absNormal.z));
// 	// if (absNormal.x == normalMax)
// 	// {
// 	// 	calculatedUV = inObjectPos.yz;
// 	// 	// outFragColor = vec4(1, 0, 0, 1);
// 	// 	// return;
// 	// }
// 	// else if (absNormal.y == normalMax)
// 	// {
// 	// 	calculatedUV = inObjectPos.xz;
// 	// 	// outFragColor = vec4(0, 1, 0, 1);
// 	// 	// return;
// 	// }
// 	// else if (absNormal.z == normalMax)
// 	// {
// 	// 	calculatedUV = inObjectPos.xy;
// 	// 	// outFragColor = vec4(0, 0, 1, 1);
// 	// 	// return;
// 	// }

// 	// // NEW STRAT.
// 	// vec2 calculatedUVs[] = {
// 	// 	inObjectPos.yz,
// 	// 	inObjectPos.xz,
// 	// 	inObjectPos.xy,
// 	// };
// 	// calculatedUV =
// 	// 	calculatedUVs[0] * absNormal.x +
// 	// 	calculatedUVs[1] * absNormal.y +
// 	// 	calculatedUVs[2] * absNormal.z;

// 	// NEW STRAT.
// 	// calculatedUV = inObjectPos.yz;
// 	// if (inObjectPos.x < 0.5)
// 	// {
// 	// 	// outFragColor = vec4(0, 1, 0, 1);
// 	// 	// return;
// 	// 	calculatedUV.x = inObjectPos.x;
// 	// 	calculatedUV.y = inObjectPos.z;
// 	// }

// 	vec3 absNormal = normalize(abs(n));  // PART OF NEW STRAT.
// 	vec3 normNorm = normalize(inNormal);
// 	float normalMax = max(absNormal.x, max(absNormal.y, absNormal.z));
// 	vec3 anchorVec;
// 	if (absNormal.x == normalMax)
// 	{
// 		// calculatedUV = inObjectPos.yz;
// 		anchorVec = vec3(0, (normNorm.y < 0.0 ? 1 : -1), 0);
// 		// outFragColor = vec4(1, 0, 0, 1);
// 		// return;
// 	}
// 	else if (absNormal.y == normalMax)
// 	{
// 		anchorVec = vec3((normNorm.x < 0.0 ? 1 : -1), 0, 0);
// 		// calculatedUV = inObjectPos.xz;
// 		// outFragColor = vec4(0, 1, 0, 1);
// 		// return;
// 	}
// 	else if (absNormal.z == normalMax)
// 	{
// 		anchorVec = vec3(0, (normNorm.y < 0.0 ? 1 : -1), 0);
// 		// calculatedUV = inObjectPos.xy;
// 		// outFragColor = vec4(0, 0, 1, 1);
// 		// return;
// 	}
// 	vec3 jojoVec = normalize(cross(anchorVec, normNorm));
// 	vec3 jojo2Vec = normalize(cross(jojoVec, normNorm));
// 	calculatedUV.x = dot(abs(jojoVec), inObjectPos);
// 	calculatedUV.y = dot(abs(jojo2Vec), inObjectPos);

// 	// outFragColor = vec4(jojoVec, 1);
// 	// outFragColor = vec4(jojo2Vec, 1);
// 	// outFragColor = vec4(calculatedUV, 0, 1);
// 	// return;
// #endif

    // Calculate shadow.
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

    // Combine colors.
    vec3 baseColor = SRGBtoLINEAR(texture(textureMaps[nonuniformEXT(material.textureMapIndex)], calculatedUV)).rgb;
    if (n.y > material.slopeAngleSin)
        baseColor *= vec3(0, 1, 0); //material.standableColor.rgb; @DEBUG: using standablecolor as an input now.
    else
        baseColor *= material.steepColor.rgb;

	// @DEBUG: to remove shading.
    baseColor *= diffuse * (shadow * 0.8 + 0.2);  // Also FAKE the shadow.

	// // @DEBUG: to focus.
	// if (n.x < 0.0 || n.y < 0.0 || n.z < 0.0)
	// {
	// 	baseColor *= 0.05;
	// }
	
	outFragColor = vec4(baseColor, 1.0);
}
