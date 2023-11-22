#pragma once

#include "pch.h"


namespace texturecooker
{
    bool checkTextureCookNeeded(const std::filesystem::path& recipePath);
    bool cookTextureFromRecipe(const std::filesystem::path& recipePath);
}
