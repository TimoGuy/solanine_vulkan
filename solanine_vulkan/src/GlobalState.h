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

    struct HarvestableItemOption
    {
        std::string name;
        std::string modelName;
    };

    struct HarvestableMaterialWithQuantity
    {
        HarvestableItemOption* material;
        uint32_t quantity;
    };

    struct ScannableItemOption
    {
        std::string name;
        std::string modelName;
        AncientWeaponItemType type;
        std::vector<HarvestableMaterialWithQuantity> requiredMaterialsToMaterialize;
    };


    void initGlobalState(SceneCamera& sc);
    void launchAsyncWriteTask();  // @NOTE: this is simply for things that are marked saved
    void cleanupGlobalState();

    HarvestableItemOption* getHarvestableItemByIndex(size_t index);
    uint16_t getInventoryQtyOfHarvestableItemByIndex(size_t harvestableItemId);
    void changeInventoryItemQtyByIndex(size_t harvestableItemId, int16_t changeInQty);
    size_t getNumHarvestableItemIds();

    std::string ancientWeaponItemTypeToString(AncientWeaponItemType awit);
    ScannableItemOption* getAncientWeaponItemByIndex(size_t index);
    bool getCanMaterializeScannableItemByIndex(size_t scannableItemId);
    void flagScannableItemAsCanMaterializeByIndex(size_t scannableItemId, bool flag);
    size_t getNumScannableItemIds();
}
