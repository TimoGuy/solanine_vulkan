#pragma once

#include <string>
#include <vector>
#include <functional>
#include <vulkan/vulkan.h>
#include "ImportGLM.h"

class VulkanEngine;
struct DeletionQueue;


namespace textbox
{
    extern vec3 mainRenderPosition;
    extern vec3 mainRenderExtents;
    extern vec3 querySelectionsRenderPosition;

    struct TextboxUserQuery
    {
        std::vector<std::string> queryOptions;
        std::function<void(uint32_t)> querySelectedCallback;  // `uint32_t` is the query selection selected.
    };

    struct TextboxMessage
    {
        std::vector<std::string> texts;
        bool useEndingQuery = false;
        TextboxUserQuery endingQuery;
    };

    void init(VulkanEngine* engine);
    void initPipeline(VkViewport& screenspaceViewport, VkRect2D& screenspaceScissor, DeletionQueue& deletionQueue);
    void cleanup();

    void update(const float_t& unscaledDeltaTime);

    bool isProcessingMessage();  // @NOTE: this should be for checking whether to do input at the current time for the player.
    void sendTextboxMessage(TextboxMessage message);
    void renderTextbox(VkCommandBuffer cmd);
}