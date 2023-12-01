#pragma once

#include "pch.h"

class VulkanEngine;


namespace materialorganizer
{
    void init(VulkanEngine* engine);

    bool checkMaterialBaseReloadNeeded(const std::filesystem::path& path);
    bool loadMaterialBase(const std::filesystem::path& path);

    bool checkDerivedMaterialParamReloadNeeded(const std::filesystem::path& path);
    bool loadDerivedMaterialParam(const std::filesystem::path& path);

    void cookTextureIndices();
    size_t derivedMaterialNameToUMBIdx(std::string derivedMatName);
    size_t derivedMaterialNameToDMPSIdx(std::string derivedMatName);
}
