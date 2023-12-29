#pragma once

class VulkanEngine;
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

    extern float_t timescale;

    extern float_t DOFFocusDepth;
    extern float_t DOFFocusExtent;
    extern float_t DOFBlurExtent;

    extern bool isEditingMode;

    struct SpawnPointData
    {
        void*   referenceSpawnPointEntity;
        vec3    position;
        float_t facingDirection;
    };
    extern std::vector<SpawnPointData> listOfSpawnPoints;

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

    struct HarvestableItemWithQuantity
    {
        size_t harvestableItemId;
        uint32_t quantity;
    };

    struct ScannableItemOption
    {
        std::string name;
        std::string modelName;
        AncientWeaponItemType type;
        std::vector<HarvestableItemWithQuantity> requiredMaterialsToMaterialize;

        struct WeaponStats  // @NOTE: garbage values if this is not a weapon.
        {
            std::string weaponType = "NULL";
            int32_t durability;
            int32_t attackPower;
            int32_t attackPowerWhenDulled;  // This is when durability hits 0.
        } weaponStats;
    };


    void initGlobalState(VulkanEngine* engine, SceneCamera& sc);
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
    size_t getSelectedScannableItemId();
    void setSelectedScannableItemId(size_t scannableItemId);
    bool selectNextCanMaterializeScannableItemId();
}
