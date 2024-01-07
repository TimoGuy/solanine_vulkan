#pragma once

#include "VkDataStructures.h"

class VulkanEngine;
struct DeletionQueue;


namespace ui
{
    struct UIQuad
    {
        bool visible     = true;
        Texture* texture = nullptr;
        VkDescriptorSet builtTextureSet;
        bool useNineSlicing = false;
        float_t nineSlicingSizeX = 1.0f;
        float_t nineSlicingSizeY = 1.0f;
        vec4 tint        = { 1.0f, 1.0f, 1.0f, 1.0f };

        vec3 position = GLM_VEC3_ZERO_INIT;
        versor rotation = GLM_QUAT_IDENTITY_INIT;
        vec3 scale = GLM_VEC3_ONE_INIT;
        float_t renderOrder = 0.0f;
    };

    void init(VulkanEngine* engine);
    void initPipeline(VkViewport& screenspaceViewport, VkRect2D& screenspaceScissor, DeletionQueue& deletionQueue);
    void cleanup();

    UIQuad* registerUIQuad(Texture* texture);
    void unregisterUIQuad(UIQuad* toDelete);
    void renderUIQuads(VkCommandBuffer cmd);
}