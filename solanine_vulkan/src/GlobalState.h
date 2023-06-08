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

    enum AncientWeaponItemType
    {
        WEAPON,
        FOOD,
        TOOL,
    };

    struct HarvestableMaterial
    {
        std::string name;
        std::string modelName;
    };

    struct HarvestableMaterialWithQuantity
    {
        HarvestableMaterial* material;
        uint32_t quantity;
    };

    struct AncientWeaponItem
    {
        std::string name;
        std::string modelName;
        AncientWeaponItemType type;
        std::vector<HarvestableMaterialWithQuantity> requiredMaterialsToMaterialize;
    };


    void initGlobalState(SceneCamera& sc);
    void launchAsyncWriteTask();  // @NOTE: this is simply for things that are marked saved
    void cleanupGlobalState();

    std::string ancientWeaponItemTypeToString(AncientWeaponItemType awit);
    AncientWeaponItem* getAncientWeaponItemByIndex(size_t index);
}
