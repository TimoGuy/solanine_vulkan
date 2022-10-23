#pragma once

#include "Imports.h"
#include "VkglTFModel.h"
class VulkanEngine;


class ModelCache
{
    // @NOTE: everything is private here
    std::unordered_map<std::string, vkglTF::Model> _modelCache;
    vkglTF::Model* getModel(VulkanEngine* engine, const std::string& filename, float scale = 1.0f);

    friend class VulkanEngine;
};
