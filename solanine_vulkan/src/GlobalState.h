#pragma once

#include "Imports.h"
struct SceneCamera;


namespace globalState
{
    extern std::string activeScene;

    extern glm::vec3   playerSavedPosition;
    extern float_t     playerSavedFacingDirection;

    extern int32_t     playerHealth;
    extern int32_t     playerMaxHealth;

    void initGlobalState(SceneCamera& sc);
    void launchAsyncWriteTask();
    void cleanupGlobalState();
}
