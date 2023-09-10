#version 460

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outNearFieldColorWithCoCAlpha;
layout (location = 1) out vec4 outFarFieldColorWithCoCAlpha;


layout (push_constant) uniform GatherDOFParams
{
	float sampleRadiusMultiplier;
} gatherDOFParams;

layout (set = 0, binding = 0) uniform sampler2D nearFieldImage;
layout (set = 0, binding = 1) uniform sampler2D farFieldImage;
layout (set = 0, binding = 1) uniform sampler2D nearFieldDownsizedCoCImage;


vec4 gatherDOF(vec4 colorAndCoC, float sampleRadius)
{
	// @TODO: do Gather DOF here.
	return colorAndCoC;
}

void main()
{
	vec4 nearField = texture(nearFieldImage, inUV);
	vec4 farField = texture(farFieldImage, inUV);

	if (nearField.a > 0.0)
	{
		float nearFieldCoC = texture(nearFieldDownsizedCoCImage, inUV).r;
		nearField = gatherDOF(nearField, nearFieldCoC * gatherDOFParams.sampleRadiusMultiplier);
	}

	if (farField.a > 0.0)
	{
		farField = gatherDOF(farField, farField.a * gatherDOFParams.sampleRadiusMultiplier);
	}

	outNearFieldColorWithCoCAlpha = nearField;
	outFarFieldColorWithCoCAlpha = farField;
}
