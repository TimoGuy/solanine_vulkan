#version 460

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outNearFieldColorWithCoCAlpha;
layout (location = 1) out vec4 outFarFieldColorWithCoCAlpha;
layout (location = 2) out float outNearFieldCoC;


layout (push_constant) uniform CoCParams
{
	float cameraZNear;
	float cameraZFar;
	float focusDepth;
	float focusExtent;
	float blurExtent;
} cocParams;

layout (set = 0, binding = 0) uniform sampler2D mainImage;
layout (set = 0, binding = 1) uniform sampler2D depthImage;


float calculateCoC()
{
	float depth = texture(depthImage, inUV).r;
	depth =
		cocParams.cameraZNear * cocParams.cameraZFar /
		(cocParams.cameraZFar +
			depth * (cocParams.cameraZNear - cocParams.cameraZFar));

	float depthRelativeToFocusDepth = depth - cocParams.focusDepth;
	if (abs(depthRelativeToFocusDepth) < cocParams.focusExtent)
		return 0.0;
	return (depthRelativeToFocusDepth - cocParams.focusExtent * sign(depthRelativeToFocusDepth)) / cocParams.blurExtent;
}

void main()
{
	float CoC = calculateCoC();  // 0.0 is completely in focus. 1.0 is completely out of focus.
	float nearCoC = clamp(-CoC, 0.0, 1.0);
	float farCoC = clamp(CoC, 0.0, 1.0);

	if (CoC == 0.0)
	{
		outNearFieldColorWithCoCAlpha = vec4(vec3(0.0), 0.0);
		outFarFieldColorWithCoCAlpha = vec4(vec3(0.0), 0.0);
		outNearFieldCoC = 0.0;
		return;
	}

	vec3 color = texture(mainImage, inUV).rgb;
	outNearFieldColorWithCoCAlpha = vec4((nearCoC > 0.0) ? color : vec3(0.0), nearCoC);
	outFarFieldColorWithCoCAlpha = vec4((farCoC > 0.0) ? color : vec3(0.0), farCoC);
	outNearFieldCoC = nearCoC;
}
