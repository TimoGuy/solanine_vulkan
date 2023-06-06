#version 460

layout(location = 0) out vec4 outFragColor;


layout(push_constant) uniform Color
{
	layout(offset = 80) vec4 color;
} color;


void main()
{
	outFragColor = color.color;
}
