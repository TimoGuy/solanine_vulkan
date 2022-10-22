// Based off tutorial https://naorliron26.wixsite.com/naorgamedev/object-picking-in-vulkan
#version 460

layout (location = 0) flat in uint inID;

layout (location = 0) out float outFragColor;


layout (set = 3, binding = 0) buffer ShaderStorageBufferObject
{
    uint selectedId;
} ssbo;


void main()
{
	ssbo.selectedId = inID + 1;		// This is only run once due to the dynamic state scissor, so we don't need to read in the texture, just write to the selectedID only once
	outFragColor = float(inID + 1);   // @NOTE: This doesn't matter.....
}
