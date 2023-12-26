#version 460
#extension GL_EXT_nonuniform_qualifier : require

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
struct MaterialParam
{
	// Texture map references
	uint textureMapIndex;

	// Material properties
	vec4  tint;
    vec4  offsetTiling;
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


void main()
{
	MaterialParam material = materialCollection.params[instancePtrBuffer.pointers[baseInstanceID].materialID - materialCollection.materialIDOffset];

    vec2 screenspaceUV = gl_FragCoord.xy;
    screenspaceUV *= material.offsetTiling.zw;
    screenspaceUV += material.offsetTiling.xy;
    screenspaceUV *= 1.0 / 256.0;

    vec3 color = SRGBtoLINEAR(texture(textureMaps[nonuniformEXT(material.textureMapIndex)], screenspaceUV)).rgb;
    color *= material.tint.rgb;
	outFragColor = vec4(color, 1.0);
}
