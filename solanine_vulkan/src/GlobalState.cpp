#include "GlobalState.h"

#include "DataSerialization.h"
#include "Debug.h"
#include "StringHelper.h"
#include "Camera.h"


namespace globalState
{
    // Default values
    std::string savedActiveScene                = "sample_scene_simplified.ssdat";

    glm::vec3   savedPlayerPosition        = glm::vec3(0);    // Currently unused. @TODO
    float_t     savedPlayerFacingDirection = 0.0f;            // Currently unused. @TODO

    int32_t     savedPlayerHealth               = 100;
    int32_t     savedPlayerMaxHealth            = 100;

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
        savedActiveScene                                  = dsd.loadString();
        sceneCameraRef->gpuCameraData.cameraPosition = dsd.loadVec3();
        sceneCameraRef->facingDirection              = dsd.loadVec3();
        sceneCameraRef->fov                          = dsd.loadFloat();
        savedPlayerPosition                          = dsd.loadVec3();
        savedPlayerFacingDirection                   = dsd.loadFloat();
        savedPlayerHealth                                 = dsd.loadFloat();
        savedPlayerMaxHealth                              = dsd.loadFloat();

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
            outfile << dsd.loadString() << '\n';

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
