//
// Load in TinyGLTF loader (v2.6.0) with STB image
//
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_USE_CPP14
#define TINYGLTF_USE_RAPIDJSON
#define TINYGLTF_NOEXCEPTION
// #define TINYGLTF_NO_EXTERNAL_IMAGE  // @NOTE: only doesn't load image if it's an external file. If it's an internal uri then it will still parse it.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_MSC_SECURE_CRT
#include "tiny_gltf.h"

//
// Load in VMA
//
#ifdef _DEVELOP
#define VMA_DEBUG_LOG(format, ...) do { \
        printf(format, __VA_ARGS__); \
        printf("\n"); \
    } while(false)
#endif
#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>
