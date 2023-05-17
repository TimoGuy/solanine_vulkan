#pragma once

#include "Imports.h"
struct SceneCamera;


namespace globalState
{
    //
    // Saved
    //
    extern std::string savedActiveScene;

    extern vec3   savedPlayerPosition;
    extern float_t     savedPlayerFacingDirection;

    extern int32_t     savedPlayerHealth;
    extern int32_t     savedPlayerMaxHealth;

    //
    // Non-saved
    //
    struct EntityInformation
    {
        vec3   position;
        std::string type;
        bool        isHidden = false;  // Just in case if a disappearing entity would be used like a rabbit that burrows. Then the enemy would be confused (@NOTE that the position still updates)
    };
    extern std::vector<EntityInformation*> enemysEnemiesEntInfo;

    void initGlobalState(SceneCamera& sc);
    void launchAsyncWriteTask();  // @NOTE: this is simply for things that are marked saved
    void cleanupGlobalState();
}
