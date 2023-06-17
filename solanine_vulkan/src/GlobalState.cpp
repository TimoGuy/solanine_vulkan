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

    SceneCamera* sceneCameraRef = nullptr;

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
}
