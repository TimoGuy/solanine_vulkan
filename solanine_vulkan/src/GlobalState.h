#pragma once

#include "Imports.h"
struct SceneCamera;


namespace globalState
{
    //
    // Saved
    //
    extern std::string savedActiveScene;

    extern vec3    savedPlayerPosition;
    extern float_t savedPlayerFacingDirection;

    extern int32_t savedPlayerHealth;
    extern int32_t savedPlayerMaxHealth;

    extern std::string playerGUID;
    extern vec3* playerPositionRef;

    void initGlobalState(SceneCamera& sc);
    void launchAsyncWriteTask();  // @NOTE: this is simply for things that are marked saved
    void cleanupGlobalState();
}
