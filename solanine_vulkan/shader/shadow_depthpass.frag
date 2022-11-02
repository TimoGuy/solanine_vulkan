#version 460

layout (location = 0) in vec2 inUV0;

layout (set = 3, binding = 0) uniform sampler2D colorMap;


void main() 
{	
	float alpha = texture(colorMap, inUV0).a;
	if (alpha < 0.5)
		discard;
}
