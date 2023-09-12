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
	vec4 maxNearField = vec4(0.0);
	float maxNearFieldIntensity = 0.0;
	vec4 maxFarField = vec4(0.0);
	float maxFarFieldIntensity = 0.0;

	const int extent = 1;
	for (int i = -extent; i <= extent; i++)
		for (int j = -extent; j <= extent; j++)
		{
			vec2 sampleUV = inUV + vec2(i, j) * params.oneOverImageExtent;
			
			vec4 nearField = texture(nearFieldImage, sampleUV);
			vec4 farField = texture(farFieldImage, sampleUV);

			const vec3 luma = vec3(0.2126, 0.7152, 0.0722);

			float nearIntensity = dot(luma, nearField.rgb * nearField.a);
			if (nearIntensity > maxNearFieldIntensity)
			{
				maxNearField = nearField;
				maxNearFieldIntensity = nearIntensity;
			}

			float farIntensity = dot(luma, farField.rgb * farField.a);
			if (farIntensity > maxFarFieldIntensity)
			{
				maxFarField = farField;
				maxFarFieldIntensity = farIntensity;
			}
		}

	outNearFieldColorWithCoCAlpha = maxNearField;
	outFarFieldColorWithCoCAlpha = maxFarField;
}
