// Based off tutorial https://naorliron26.wixsite.com/naorgamedev/object-picking-in-vulkan
#version 460

layout (location = 0) flat in uint inID;

layout (location = 0) out float outFragColor;


#define RENDER_OBJECTS_MAX_CAPACITY 10000
layout (set = 3, binding = 0) buffer ShaderStorageBufferObject
{
	uint selectedId[RENDER_OBJECTS_MAX_CAPACITY];
	float selectedDepth[RENDER_OBJECTS_MAX_CAPACITY];
} ssbo;


void main()
{
	ssbo.selectedId[inID]    = inID + 1;		// This is only run once due to the dynamic state scissor, so we don't need to read in the texture, just write to the selectedID only once
	ssbo.selectedDepth[inID] = gl_FragCoord.z;
	outFragColor = float(inID + 1);   // @NOTE: This doesn't matter.....
}
