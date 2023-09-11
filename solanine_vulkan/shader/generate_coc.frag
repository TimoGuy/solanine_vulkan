#version 460

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec2 outCoC;


layout (push_constant) uniform CoCParams
{
	float cameraZNear;
	float cameraZFar;
	float focusDepth;
	float focusExtent;
	float blurExtent;
} cocParams;

layout (set = 0, binding = 0) uniform sampler2D depthImage;


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
	outCoC = vec2(nearCoC, farCoC);
}
