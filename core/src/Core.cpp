//
// Load in TinyGLTF loader (v2.6.0) with STB image
//
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_USE_CPP14
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
// #define TINYGLTF_NOEXCEPTION // optional. disable exception handling.
#define STBI_MSC_SECURE_CRT
#include <tiny_gltf.h>

//
// Load in VMA
//
#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>
