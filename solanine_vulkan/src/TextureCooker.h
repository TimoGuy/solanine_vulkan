#pragma once

#include "pch.h"


namespace texturecooker
{
    bool checkHalfStepNeeded(const std::filesystem::path& path);
    bool cookHalfStepFromRecipe(const std::filesystem::path& path);

    bool checkTextureCookNeeded(const std::filesystem::path& recipePath);
    bool cookTextureFromRecipe(const std::filesystem::path& recipePath);
}
