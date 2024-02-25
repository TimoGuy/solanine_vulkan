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
	vec4 colorWFresnel;
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


void main()
{
	MaterialParam material = materialCollection.params[instancePtrBuffer.pointers[baseInstanceID].materialID - materialCollection.materialIDOffset];

    // Calculate fresnel.
	vec3 n = normalize(inNormal);
	vec3 vn = normalize(cameraData.cameraPosition - inWorldPos);
	float fresnel = clamp(1.0 - dot(n, vn), 0.0, 1.0);

	// Cutoff.
	if (fresnel < material.colorWFresnel.a)
		discard;

	vec3 useless = texture(textureMaps[nonuniformEXT(0)], vec2(0, 0)).rgb;  // @NOCHECKIN: for forcing `textureMaps` to appear as a runtime array in the reflection library.
	
	outFragColor = vec4(material.colorWFresnel.rgb, 1.0);
}
