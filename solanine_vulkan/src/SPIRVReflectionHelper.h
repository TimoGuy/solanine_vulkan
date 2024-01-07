#pragma once

#include "pch.h"

#include "spirv_reflect.h"
class VulkanEngine;


namespace reflectionhelper
{
    void init(VulkanEngine* engine);

    bool loadShaderModule(const char* filePath, spv_reflect::ShaderModule& outShaderModule);
    std::vector<SpvReflectDescriptorBinding*> extractDescriptorBindingsSorted(const spv_reflect::ShaderModule& shaderModule);

    struct SetSearchEntry
    {
        struct BindingSearchEntry
        {
            std::string bindingName;
            SpvOp_ bindingType;
            uint32_t binding;
        };
        std::vector<BindingSearchEntry> bindings;
    };
    bool findDescriptorBindingsWithName(const std::vector<SpvReflectDescriptorBinding*>& descriptorBindings, const std::vector<SetSearchEntry>& queries);
}
