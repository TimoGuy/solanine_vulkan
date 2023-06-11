#include "GlobalState.h"

#include "DataSerialization.h"
#include "Debug.h"
#include "StringHelper.h"
#include "Camera.h"


namespace globalState
{
    // Default values
    std::string savedActiveScene                = "sample_scene_simplified.ssdat";

    vec3    savedPlayerPosition        = GLM_VEC3_ZERO_INIT;    // Currently unused. @TODO
    float_t savedPlayerFacingDirection = 0.0f;                  // Currently unused. @TODO

    int32_t savedPlayerHealth          = 100;
    int32_t savedPlayerMaxHealth       = 100;

    std::string playerGUID = "";
    vec3* playerPositionRef = nullptr;

    SceneCamera* sceneCameraRef = nullptr;

    // Harvestable items (e.g. materials, raw ores, etc.)
    std::vector<HarvestableItemOption> allHarvestableItems = {
        HarvestableItemOption{.name = "sheet metal", .modelName = "Box" },
        HarvestableItemOption{.name = "TEST slime", .modelName = "Box" },
    };

    std::vector<uint16_t> harvestableItemQuantities;  // This is the inventory data for collectable/ephemeral items.

    // Scannable items
    std::vector<ScannableItemOption> allAncientWeaponItems = {
        ScannableItemOption{
            .name = "Wing Blade",
            .modelName = "WingWeapon",
            .type = WEAPON,
            .requiredMaterialsToMaterialize = {
                { .harvestableItemId = 0, .quantity = 1 }
            }
        },
        ScannableItemOption{
            .name = "TEST Slime girl",
            .modelName = "SlimeGirl",
            .type = FOOD,
            .requiredMaterialsToMaterialize = {
                { .harvestableItemId = 1, .quantity = 2 },
            }
        },
    };

    std::vector<bool> scannableItemCanMaterializeFlags;  // This is the list of materializable items.  @FUTURE: make this into a more sophisticated data structure for doing the "memory" system of aligning the data and overwriting previously written data.
    size_t selectedScannableItemId = 0;                  // This is the item selected to be materialized if LMB is pressed.


    //
    // Global state writing brain
    //
    const std::string gsFname = "global_state.hgs";
    tf::Executor tfExecutor(1);
    tf::Taskflow tfTaskAsyncWriting;

    void loadGlobalState()
    {
        // @TODO: for now it's just the dataserialization dump. I feel like getting the data into unsigned chars would be best though  -Timo
        std::ifstream infile(gsFname);
        if (!infile.is_open())
        {
            debug::pushDebugMessage({
                .message = "Could not open file \"" + gsFname + "\" for reading global state (using default values)",
                .type = 1,
            });
            return;
        }

        DataSerializer ds;
        std::string line;
        while (std::getline(infile, line))
        {
            trim(line);
            if (line.empty())
                continue;
            ds.dumpString(line);
        }

        DataSerialized dsd = ds.getSerializedData();
        dsd.loadString(savedActiveScene);
        dsd.loadVec3(sceneCameraRef->gpuCameraData.cameraPosition);
        dsd.loadVec3(sceneCameraRef->facingDirection);
        dsd.loadFloat(sceneCameraRef->fov);
        dsd.loadVec3(savedPlayerPosition);
        dsd.loadFloat(savedPlayerFacingDirection);

        float_t lf1, lf2;
        dsd.loadFloat(lf1);
        dsd.loadFloat(lf2);
        savedPlayerHealth = (int32_t)lf1;
        savedPlayerMaxHealth = (int32_t)lf2;

        debug::pushDebugMessage({
            .message = "Successfully read state from \"" + gsFname + "\"",
        });
    }

    void saveGlobalState()
    {
        // @TODO: for now it's just the dataserialization dump. I feel like getting the data into unsigned chars would be best though  -Timo
        std::ofstream outfile(gsFname);
        if (!outfile.is_open())
        {
            debug::pushDebugMessage({
                .message = "Could not open file \"" + gsFname + "\" for writing global state",
                .type = 2,
            });
            return;
        }

        DataSerializer ds;
        ds.dumpString(savedActiveScene);
        ds.dumpVec3(sceneCameraRef->gpuCameraData.cameraPosition);
        ds.dumpVec3(sceneCameraRef->facingDirection);
        ds.dumpFloat(sceneCameraRef->fov);
        ds.dumpVec3(savedPlayerPosition);
        ds.dumpFloat(savedPlayerFacingDirection);
        ds.dumpFloat(savedPlayerHealth);
        ds.dumpFloat(savedPlayerMaxHealth);

        DataSerialized dsd = ds.getSerializedData();
        size_t count = dsd.getSerializedValuesCount();
        for (size_t i = 0; i < count; i++)
        {
            std::string s;
            dsd.loadString(s);
            outfile << s << '\n';
        }

        debug::pushDebugMessage({
            .message = "Successfully wrote state to \"" + gsFname + "\"",
        });
    }

    void initGlobalState(SceneCamera& sc)
    {
        sceneCameraRef = &sc;

        // Initial values for inventory and list of materializable items.
        harvestableItemQuantities.resize(allHarvestableItems.size(), 0);
        scannableItemCanMaterializeFlags.resize(allAncientWeaponItems.size(), false);

        loadGlobalState();

        tfTaskAsyncWriting.emplace([&]() {
            saveGlobalState();
        });
    }

    void launchAsyncWriteTask()
    {
        tfExecutor.wait_for_all();
        tfExecutor.run(tfTaskAsyncWriting);
    }

    void cleanupGlobalState()
    {
        launchAsyncWriteTask();  // Run the task one last time before cleanup

        // Lol, no cleanup. Thanks Dmitri!
    }

    HarvestableItemOption* getHarvestableItemByIndex(size_t index)
    {
        return &allHarvestableItems[index];
    }

    uint16_t getInventoryQtyOfHarvestableItemByIndex(size_t harvestableItemId)
    {
        return harvestableItemQuantities[harvestableItemId];
    }

    void changeInventoryItemQtyByIndex(size_t harvestableItemId, int16_t changeInQty)
    {
        // Clamp all item quantities in the range [0-999]
        harvestableItemQuantities[harvestableItemId] = (uint16_t)std::max(0, std::min(999, (int32_t)harvestableItemQuantities[harvestableItemId] + changeInQty));
    }

    size_t getNumHarvestableItemIds()
    {
        return allHarvestableItems.size();
    }
    
    std::string ancientWeaponItemTypeToString(AncientWeaponItemType awit)
    {
        switch (awit)
        {
            case WEAPON: return "weapon";
            case FOOD:   return "food";
            case TOOL:   return "tool";
            default:     return "NO ITEM TYPE TO STRING CONVERSTION AVAILABLE";
        }
    }

    ScannableItemOption* getAncientWeaponItemByIndex(size_t index)
    {
        return &allAncientWeaponItems[index];
    }

    bool getCanMaterializeScannableItemByIndex(size_t scannableItemId)
    {
        return scannableItemCanMaterializeFlags[scannableItemId];
    }

    void flagScannableItemAsCanMaterializeByIndex(size_t scannableItemId, bool flag)
    {
        scannableItemCanMaterializeFlags[scannableItemId] = flag;
    }

    size_t getNumScannableItemIds()
    {
        return allAncientWeaponItems.size();
    }

    size_t getSelectedScannableItemId()
    {
        return selectedScannableItemId;
    }

    void setSelectedScannableItemId(size_t scannableItemId)
    {
        selectedScannableItemId = scannableItemId;
    }
}
