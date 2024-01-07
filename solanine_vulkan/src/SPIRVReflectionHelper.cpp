#include "pch.h"

#include "SPIRVReflectionHelper.h"


namespace reflectionhelper
{
    VulkanEngine* engineRef;

    void init(VulkanEngine* engine)
    {
        engineRef = engine;
    }

    bool loadShaderModule(const char* filePath, spv_reflect::ShaderModule& outShaderModule)
    {
        std::cout << "[LOAD SHADER MODULE FOR REFLECTION]" << std::endl;

        // Open SPIRV file.
        std::ifstream file(filePath, std::ios::ate | std::ios::binary);
        if (!file.is_open())
        {
            std::cerr << "ERROR: could not open file " << filePath << std::endl;
            return false;
        }

        // Get the filesize and copy the whole thing into the correct sized buffer.
        size_t filesize = (size_t)file.tellg();
        std::vector<uint32_t> buffer(filesize / sizeof(uint32_t));
        file.seekg(0);
        file.read((char*)buffer.data(), filesize);
        file.close();

        // Load shader into reflection. Error check.
        outShaderModule = spv_reflect::ShaderModule(buffer);
        if (outShaderModule.GetResult() != SPV_REFLECT_RESULT_SUCCESS)
        {
            std::cerr << "ERROR: could not create shader module reflection for shader file " << filePath << std::endl;
            return false;
        }

        // Successful shader creation!
        std::cout << "Successfully created shader module reflection for shader file " << filePath << std::endl;
        return true;
    }

    std::vector<SpvReflectDescriptorBinding*> extractDescriptorBindingsSorted(const spv_reflect::ShaderModule& shaderModule)
    {
        uint32_t count;
        SpvReflectResult result = shaderModule.EnumerateDescriptorBindings(&count, nullptr);
        assert(result == SPV_REFLECT_RESULT_SUCCESS);

        std::vector<SpvReflectDescriptorBinding*> bindings;
        bindings.resize(count);
        result = shaderModule.EnumerateDescriptorBindings(&count, bindings.data());
        assert(result == SPV_REFLECT_RESULT_SUCCESS);

        std::sort(
            bindings.begin(),
            bindings.end(),
            [](SpvReflectDescriptorBinding* a, SpvReflectDescriptorBinding* b) -> bool {
            if (a->set != b->set)
                return a->set < b->set;
            return a->binding < b->binding;
        });

        return bindings;
    }

    bool findDescriptorBindingsWithName(const std::vector<SpvReflectDescriptorBinding*>& descriptorBindings, const std::vector<SetSearchEntry>& queries)
    {
        struct FoundSet
        {
            uint32_t setId = (uint32_t)-1;
            size_t numFound = 0;
        };
        std::vector<FoundSet> foundSets;
        foundSets.resize(queries.size(), {});  // Mark as unmatched.

        for (size_t i = 0; i < descriptorBindings.size(); i++)
        {
            auto descriptorBinding = descriptorBindings[i];
            for (size_t j = 0; j < queries.size(); j++)
            {
                auto& set = queries[j];
                for (auto& binding : set.bindings)
                {
                    if (std::string(descriptorBinding->name) == binding.bindingName &&
                        descriptorBinding->type_description->op == binding.bindingType)
                        if (binding.binding == descriptorBinding->binding &&
                            (foundSets[j].setId == (uint32_t)-1 || foundSets[j].setId == descriptorBinding->set))
                        {
                            foundSets[j].setId = descriptorBinding->set;
                            foundSets[j].numFound++;
                        }
                        else
                            foundSets[j].setId = (uint32_t)-2;  // Mark as mismatched.
                }
            }
        }

        for (size_t i = 0; i < foundSets.size(); i++)
        {
            auto s = foundSets[i];
            if (s.setId == (uint32_t)-1 ||
                s.setId == (uint32_t)-2 ||
                s.numFound != queries[i].bindings.size())
                return false;
        }
        return true;
    }
}
