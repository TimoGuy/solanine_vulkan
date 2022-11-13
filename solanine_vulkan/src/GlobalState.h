#pragma once

#include "Imports.h"
struct SceneCamera;


namespace globalState
{
    extern std::string activeScene;

    extern glm::vec3   playerSavedPosition;
    extern float_t     playerSavedFacingDirection;

    void initGlobalState(SceneCamera& sc);
    void launchAsyncWriteTask();
    void cleanupGlobalState();
}
