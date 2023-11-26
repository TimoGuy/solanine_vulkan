#include "pch.h"

#include "MaterialOrganizer.h"


namespace materialorganizer
{
    bool checkMaterialBaseReloadNeeded(const std::filesystem::path& path)
    {
        return true;
    }

    bool loadMaterialBase(const std::filesystem::path& path)
    {
        return true;
    }

    bool checkDerivedMaterialParamReloadNeeded(const std::filesystem::path& path)
    {
        return true;
    }

    bool loadDerivedMaterialParam(const std::filesystem::path& path)
    {
        return true;
    }
}
