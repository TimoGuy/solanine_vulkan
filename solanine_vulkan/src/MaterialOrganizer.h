#pragma once

#include "pch.h"


namespace materialorganizer
{
    bool checkMaterialBaseReloadNeeded(const std::filesystem::path& path);
    bool loadMaterialBase(const std::filesystem::path& path);

    bool checkDerivedMaterialParamReloadNeeded(const std::filesystem::path& path);
    bool loadDerivedMaterialParam(const std::filesystem::path& path);
}
