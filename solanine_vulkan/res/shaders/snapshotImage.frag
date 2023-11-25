#version 460

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0) uniform sampler2D snapshotImage;


void main()
{
    outColor = vec4(texture(snapshotImage, inUV).rgb, 1.0);
}
