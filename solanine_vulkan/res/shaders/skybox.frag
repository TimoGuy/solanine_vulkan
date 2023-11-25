#version 460

layout (location = 0) in vec3 inUVW;

layout (location = 0) out vec4 outColor;

layout (set = 1, binding = 0) uniform samplerCube samplerEnv;


void main()
{
    outColor = vec4(texture(samplerEnv, inUVW).rgb, 1.0);
}
