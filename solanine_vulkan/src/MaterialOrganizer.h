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
    std::string umbIdxToUniqueMaterialName(size_t umbIdx);

    std::vector<std::string> getListOfDerivedMaterials();
    bool makeDMPSFileCopy(size_t dmpsIdx, const std::string& newFile);
    bool isDMPSDirty();
    void clearDMPSDirtyFlag();
    bool saveDMPSToFile(size_t dmpsIdx);
    std::string getMaterialName(size_t dmpsIdx);

    void renderImGuiForMaterial(size_t umbIdx, size_t dmpsIdx);
}
